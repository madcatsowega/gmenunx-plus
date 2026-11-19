#include "playstats.h"
#include "utilities.h"
#include "debug.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <unistd.h>

using std::ifstream;
using std::ofstream;
using std::string;
using std::vector;

static string cleanField(string s) {
	s = strreplace(s, "\t", " ");
	s = strreplace(s, "\r", " ");
	s = strreplace(s, "\n", " ");
	return trim(s);
}

double PlayStats::readUptimeSeconds() {
	FILE *f = fopen("/proc/uptime", "r");
	if (!f) return 0.0;
	double up = 0.0;
	if (fscanf(f, "%lf", &up) != 1) up = 0.0;
	fclose(f);
	return up;
}

string PlayStats::makeKey(const string &exec, const string &params) {
	return lowercase(trim(exec) + "|" + trim(params));
}

string PlayStats::formatDuration(uint32_t secs) {
	if (secs < 60) return "<1m";
	uint32_t mins = secs / 60;
	if (mins < 60) return std::to_string(mins) + "m";
	uint32_t hrs = mins / 60;
	uint32_t rem = mins % 60;
	if (rem == 0) return std::to_string(hrs) + "h";
	return std::to_string(hrs) + "h " + std::to_string(rem) + "m";
}

void PlayStats::ensureLoaded() {
	if (!loaded) load();
}

void PlayStats::load() {
	entries.clear();
	loaded = true;
	string path = home_path("playstats.conf");
	ifstream f(path.c_str(), std::ios_base::in);
	if (!f.is_open()) return;

	string line;
	while (getline(f, line)) {
		if (line.empty()) continue;
		vector<string> cols;
		split(cols, line, "\t");
		if (cols.size() < 4) continue;

		PlayStatEntry e;
		e.key = cols[0];
		e.title = cols[1];
		e.totalSeconds = (uint32_t)strtoul(cols[2].c_str(), NULL, 10);
		e.launches = (uint32_t)strtoul(cols[3].c_str(), NULL, 10);
		e.lastPlayed = cols.size() > 4 ? (uint64_t)strtoull(cols[4].c_str(), NULL, 10) : 0;
		if (!e.key.empty()) {
			entries[e.key] = e;
		}
	}
	f.close();
}

void PlayStats::save() {
	string path = home_path("playstats.conf");
	ofstream f(path.c_str(), std::ios_base::out | std::ios_base::trunc);
	if (!f.is_open()) return;

	for (std::tr1::unordered_map<string, PlayStatEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		const PlayStatEntry &e = it->second;
		f << cleanField(e.key) << '\t'
		  << cleanField(e.title) << '\t'
		  << e.totalSeconds << '\t'
		  << e.launches << '\t'
		  << e.lastPlayed << '\n';
	}
	f.close();
	sync();
}

void PlayStats::beginSession(const string &exec, const string &params, const string &title) {
	if (exec.empty()) return;
	double up = readUptimeSeconds();
	uint64_t nowEpoch = (uint64_t)time(NULL);
	string key = makeKey(exec, params);

	string path = home_path("session.tmp");
	ofstream f(path.c_str(), std::ios_base::out | std::ios_base::trunc);
	if (!f.is_open()) return;
	f << (uint64_t)up << '\t'
	  << nowEpoch << '\t'
	  << cleanField(key) << '\t'
	  << cleanField(title) << '\n';
	f.close();
	sync();
}

void PlayStats::finishPendingSession() {
	ensureLoaded();
	string path = home_path("session.tmp");
	ifstream f(path.c_str(), std::ios_base::in);
	if (!f.is_open()) return;

	string line;
	getline(f, line);
	f.close();
	unlink(path.c_str());

	if (line.empty()) return;
	vector<string> cols;
	split(cols, line, "\t");
	if (cols.size() < 4) return;

	uint64_t startUp = strtoull(cols[0].c_str(), NULL, 10);
	uint64_t startEpoch = strtoull(cols[1].c_str(), NULL, 10);
	string key = cols[2];
	string title = cols[3];

	if (key.empty()) return;

	double nowUp = readUptimeSeconds();
	uint64_t nowEpoch = (uint64_t)time(NULL);

	// Prefer monotonic /proc/uptime so an unset RTC never corrupts playtime
	uint32_t elapsed = 0;
	if (nowUp > 0.0 && startUp > 0 && (uint64_t)nowUp >= startUp) {
		uint64_t diff = (uint64_t)nowUp - startUp;
		if (diff <= 86400) elapsed = (uint32_t)diff;
	} else if (nowEpoch >= startEpoch) {
		uint64_t diff = nowEpoch - startEpoch;
		if (diff <= 43200) elapsed = (uint32_t)diff;
	}

	// Ignore instantaneous crashes (< 3s) or invalid reboot wrap
	if (elapsed < 3) return;

	PlayStatEntry &e = entries[key];
	e.key = key;
	if (!title.empty()) e.title = title;
	e.totalSeconds += elapsed;
	e.launches += 1;
	// Monotonically increasing stamp even if RTC is unset
	uint64_t stamp = (nowEpoch > 100000) ? nowEpoch : (e.lastPlayed + 1);
	if (stamp <= e.lastPlayed) stamp = e.lastPlayed + 1;
	e.lastPlayed = stamp;

	INFO("PlayStats: recorded +%us for '%s' (total %us, %ux)", elapsed, e.title.c_str(), e.totalSeconds, e.launches);
	save();
}

const PlayStatEntry *PlayStats::get(const string &exec, const string &params) {
	ensureLoaded();
	string key = makeKey(exec, params);
	std::tr1::unordered_map<string, PlayStatEntry>::const_iterator it = entries.find(key);
	if (it == entries.end()) return NULL;
	return &it->second;
}

uint32_t PlayStats::getSeconds(const string &exec, const string &params) {
	const PlayStatEntry *e = get(exec, params);
	return e ? e->totalSeconds : 0;
}

uint64_t PlayStats::getLastPlayed(const string &exec, const string &params) {
	const PlayStatEntry *e = get(exec, params);
	return e ? e->lastPlayed : 0;
}

vector<PlayStatEntry> PlayStats::getTopPlayed(size_t maxCount) {
	ensureLoaded();
	vector<PlayStatEntry> out;
	out.reserve(entries.size());
	for (std::tr1::unordered_map<string, PlayStatEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		if (it->second.totalSeconds > 0) {
			out.push_back(it->second);
		}
	}
	std::sort(out.begin(), out.end(), [](const PlayStatEntry &a, const PlayStatEntry &b) {
		if (a.totalSeconds != b.totalSeconds) return a.totalSeconds > b.totalSeconds;
		return a.launches > b.launches;
	});
	if (out.size() > maxCount) out.resize(maxCount);
	return out;
}

uint32_t PlayStats::getTotalSeconds() {
	ensureLoaded();
	uint32_t total = 0;
	for (std::tr1::unordered_map<string, PlayStatEntry>::const_iterator it = entries.begin(); it != entries.end(); ++it) {
		total += it->second.totalSeconds;
	}
	return total;
}
