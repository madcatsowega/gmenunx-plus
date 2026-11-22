#include "searchdialog.h"
#include "linkapp.h"
#include "menu.h"
#include "messagebox.h"
#include "utilities.h"
#include "debug.h"

#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>

using std::ifstream;
using std::ofstream;

std::vector<RomDirStamp> SearchDialog::s_dirStamps;
std::vector<RomCacheEntry> SearchDialog::s_romCache;
bool SearchDialog::s_romCacheLoaded = false;

static SDL_TimerID search_marquee_timer = NULL;

static uint32_t searchMarqueeTick(uint32_t interval, void *param) {
	search_marquee_timer = NULL;
	InputManager::wakeUp(0, (void*)false);
	return 0;
}

static const int KBD_ROWS = 4;
static const int KBD_COLS = 10;

static const char *KBD_LABELS[KBD_ROWS][KBD_COLS] = {
	{"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"},
	{"Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P"},
	{"A", "S", "D", "F", "G", "H", "J", "K", "L", "<-"},
	{"Z", "X", "C", "V", "B", "N", "M", "_", "CLR", "GO"}
};

static string makeSystemBadge(const string &dir, const string &emuTitle) {
	string folder = base_name(dir, false);
	if (!folder.empty() && folder != "/" && lowercase(folder) != "roms") {
		string up = folder;
		for (size_t i = 0; i < up.size(); i++) up[i] = (char)toupper((unsigned char)up[i]);
		if (up.size() <= 7) return up;
	}
	string t = emuTitle;
	if (t.size() > 7) t = t.substr(0, 7);
	return t;
}

static string makeSectionBadge(const string &sec) {
	if (sec == "favourites") return "FAV";
	if (sec == "games") return "GAME";
	if (sec == "emulators") return "EMU";
	if (sec == "emulators.systems") return "SYS";
	if (sec == "applications") return "APP";
	if (sec == "recents") return "REC";
	string up = sec;
	for (size_t i = 0; i < up.size(); i++) up[i] = (char)toupper((unsigned char)up[i]);
	if (up.size() > 6) up = up.substr(0, 6);
	return up;
}

static bool isTooBroadDir(const string &d) {
	string rp = real_path(d);
	if (rp.empty() || rp == "/" || rp == "/home" || rp == "/media" || rp == "/mnt") return true;
	string home = real_path(home_path("../"));
	if (!home.empty() && rp == home) return true;
	return false;
}

static long long getDirMtime(const string &dir) {
	struct stat st;
	if (stat(dir.c_str(), &st) != 0) return -1;
	return (long long)st.st_mtime;
}

SearchDialog::SearchDialog(GMenu2X *gmenu2x)
: Dialog(gmenu2x, gmenu2x->tr["Quick Search"], "", "skin:imgs/search.png") {
	selChangedTick = SDL_GetTicks();
}

void SearchDialog::invalidateRomCache() {
	s_dirStamps.clear();
	s_romCache.clear();
	s_romCacheLoaded = false;
}

vector<SearchDialog::EmuRoot> SearchDialog::collectEmuRoots() {
	vector<EmuRoot> roots;
	unordered_map<string, bool> seenDirs;

	vector<int> secOrder;
	const vector<string> &secs = gmenu2x->menu->getSections();
	for (size_t i = 0; i < secs.size(); i++) {
		if (secs[i] == "emulators.systems") secOrder.push_back((int)i);
	}
	for (size_t i = 0; i < secs.size(); i++) {
		if (secs[i] == "emulators") secOrder.push_back((int)i);
	}
	for (size_t i = 0; i < secs.size(); i++) {
		if (secs[i] != "emulators.systems" && secs[i] != "emulators" &&
		    secs[i] != "settings" && secs[i] != "recents" && secs[i] != "favourites")
			secOrder.push_back((int)i);
	}

	for (size_t si = 0; si < secOrder.size(); si++) {
		linklist *ll = gmenu2x->menu->sectionLinks(secOrder[si]);
		if (!ll) continue;
		for (size_t li = 0; li < ll->size(); li++) {
			LinkApp *la = dynamic_cast<LinkApp *>(ll->at(li));
			if (!la) continue;
			string sdir = la->getSelectorDir();
			if (sdir.empty() || isTooBroadDir(sdir)) continue;

			string rp = real_path(sdir);
			if (rp.empty() || !dir_exists(rp)) continue;

			string dirKey = lowercase(rp);
			if (seenDirs.find(dirKey) != seenDirs.end()) continue;
			seenDirs[dirKey] = true;

			EmuRoot r;
			r.rootDir = rp;
			if (!la->getSelectorFilter().empty()) {
				split(r.exts, la->getSelectorFilter(), ",");
			}
			r.badge = makeSystemBadge(rp, la->getTitle());
			r.emuLinkFile = la->getFile();
			roots.push_back(r);
		}
	}
	return roots;
}

static inline string toLowerAscii(const char *s, size_t len) {
	string out(len, '\0');
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		out[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
	}
	return out;
}

static void drawSearchLoadingModal(GMenu2X *gmenu2x, const string &msg) {
	if (!gmenu2x || !gmenu2x->s) return;
	int w = gmenu2x->platform->w;
	int h = gmenu2x->platform->h;
	gmenu2x->s->box(0, 0, w, h, 0, 0, 0, 145);

	int textW = gmenu2x->font->getTextWidth(msg);
	int boxW = max(168, min(w - 28, textW + 36));
	int boxH = gmenu2x->font->height() + 26;
	int bx = (w - boxW) / 2;
	int by = (h - boxH) / 2;

	RGBAColor bg = gmenu2x->skinConfColor["messageBoxBg"];
	if (bg.a < 210) bg.a = 235;
	RGBAColor bd = gmenu2x->skinConfColor["messageBoxBorder"];
	if (bd.a == 0) bd = (RGBAColor){94, 140, 255, 220};

	gmenu2x->s->roundedBox(bx, by, boxW, boxH, bg, 7);
	gmenu2x->s->roundedRect(bx, by, boxW, boxH, bd, 7);
	gmenu2x->s->write(gmenu2x->font, msg, w / 2, by + boxH / 2, HAlignCenter | VAlignMiddle);
	gmenu2x->s->flip();
}

bool SearchDialog::loadRomCacheFile() {
	string cachePath = home_path("romindex.cache");
	FILE *f = fopen(cachePath.c_str(), "rb");
	if (!f) return false;

	char ioBuf[16384];
	setvbuf(f, ioBuf, _IOFBF, sizeof(ioBuf));

	char line[2048];
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return false;
	}
	size_t hlen = strlen(line);
	while (hlen > 0 && (line[hlen - 1] == '\n' || line[hlen - 1] == '\r' || line[hlen - 1] == ' '))
		line[--hlen] = '\0';
	if (strcmp(line, "v2") != 0) {
		fclose(f);
		return false;
	}

	s_dirStamps.clear();
	s_romCache.clear();
	s_dirStamps.reserve(128);
	s_romCache.reserve(4096);

	while (fgets(line, sizeof(line), f)) {
		size_t len = strlen(line);
		while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (len < 3 || line[1] != '\t') continue;

		char tag = line[0];
		char *cols[8];
		int ncols = 0;
		char *p = line + 2;
		while (ncols < 8) {
			cols[ncols++] = p;
			char *tab = strchr(p, '\t');
			if (!tab) break;
			*tab = '\0';
			p = tab + 1;
		}

		if (tag == 'D' && ncols >= 5) {
			RomDirStamp ds;
			ds.dir = cols[0];
			ds.rootDir = cols[1];
			ds.mtime = strtoll(cols[2], NULL, 10);
			ds.badge = cols[3];
			ds.emuLinkFile = cols[4];
			s_dirStamps.push_back(std::move(ds));
		} else if (tag == 'R' && ncols >= 6) {
			RomCacheEntry e;
			e.rootDir = cols[0];
			e.romDir = cols[1];
			e.romFile = cols[2];
			e.cleanTitle = cols[3];
			e.badge = cols[4];
			e.emuLinkFile = cols[5];
			e.lowerTitle = toLowerAscii(e.cleanTitle.data(), e.cleanTitle.size());
			e.lowerRaw = toLowerAscii(e.romFile.data(), e.romFile.size());
			e.lowerBadge = toLowerAscii(e.badge.data(), e.badge.size());
			s_romCache.push_back(std::move(e));
		}
	}
	fclose(f);
	s_romCacheLoaded = true;
	return true;
}

void SearchDialog::saveRomCacheFile() {
	string cachePath = home_path("romindex.cache");
	ofstream f(cachePath.c_str(), std::ios_base::out | std::ios_base::trunc);
	if (!f.is_open()) return;
	f << "v2\n";
	for (size_t i = 0; i < s_dirStamps.size(); i++) {
		const RomDirStamp &ds = s_dirStamps[i];
		f << "D\t" << ds.dir << '\t'
		  << ds.rootDir << '\t'
		  << ds.mtime << '\t'
		  << ds.badge << '\t'
		  << ds.emuLinkFile << '\n';
	}
	for (size_t i = 0; i < s_romCache.size(); i++) {
		const RomCacheEntry &e = s_romCache[i];
		f << "R\t" << e.rootDir << '\t'
		  << e.romDir << '\t'
		  << e.romFile << '\t'
		  << e.cleanTitle << '\t'
		  << e.badge << '\t'
		  << e.emuLinkFile << '\n';
	}
	f.close();
	sync();
}

void SearchDialog::scanRomDir(const string &rootDir, const string &dir,
                              const vector<string> &exts,
                              const string &badge, const string &emuLinkFile,
                              int depth, unordered_map<string, bool> &seen) {
	if (depth > 2) return;
	DIR *dp = opendir(dir.c_str());
	if (!dp) return;

	RomDirStamp stamp;
	stamp.dir = dir;
	stamp.rootDir = rootDir;
	stamp.mtime = getDirMtime(dir);
	stamp.badge = badge;
	stamp.emuLinkFile = emuLinkFile;
	s_dirStamps.push_back(stamp);

	struct dirent *ent;
	struct stat st;
	while ((ent = readdir(dp)) != NULL) {
		string name = ent->d_name;
		if (name.empty() || name[0] == '.') continue;

		string full = dir + "/" + name;
		unsigned char dtype = ent->d_type;
		if (!(dtype == DT_REG || dtype == DT_DIR)) {
			if (stat(full.c_str(), &st) != 0) continue;
			dtype = S_ISDIR(st.st_mode) ? DT_DIR : DT_REG;
		}

		if (dtype == DT_DIR) {
			string low = lowercase(name);
			if (low == "bios" || low == "saves" || low == "savestates" || low == "cheats" || low == ".images")
				continue;
			string subBadge = badge;
			if (depth == 0 && name.size() <= 6) {
				subBadge = makeSystemBadge(full, badge);
			}
			scanRomDir(rootDir, full, exts, subBadge, emuLinkFile, depth + 1, seen);
		} else if (dtype == DT_REG) {
			bool extOk = exts.empty();
			if (!extOk) {
				for (size_t k = 0; k < exts.size(); k++) {
					const string &ext = exts[k];
					if (ext.empty()) continue;
					if (name.size() >= ext.size() &&
					    !strcasecmp(name.c_str() + name.size() - ext.size(), ext.c_str())) {
						extOk = true;
						break;
					}
				}
			}
			if (!extOk) continue;

			string key = lowercase(dir + "/" + name);
			if (seen.find(key) != seen.end()) continue;
			seen[key] = true;

			RomCacheEntry e;
			e.rootDir = rootDir;
			e.romDir = dir;
			e.romFile = name;
			e.cleanTitle = clean_rom_title(name, true);
			if (e.cleanTitle.empty()) e.cleanTitle = base_name(name, true);
			e.lowerTitle = lowercase(e.cleanTitle);
			e.lowerRaw = lowercase(name);
			e.badge = badge;
			e.lowerBadge = lowercase(badge);
			e.emuLinkFile = emuLinkFile;
			s_romCache.push_back(e);
		}
	}
	closedir(dp);
}

void SearchDialog::refreshChangedDirs() {
	vector<EmuRoot> roots = collectEmuRoots();
	unordered_map<string, int> rootIdxByLower;
	for (size_t i = 0; i < roots.size(); i++) {
		rootIdxByLower[lowercase(roots[i].rootDir)] = (int)i;
	}

	unordered_map<string, bool> dirtyRoots;
	unordered_map<string, bool> presentCachedRoots;

	// Check every cached directory's mtime via stat() (~2ms total)
	for (size_t i = 0; i < s_dirStamps.size(); i++) {
		const RomDirStamp &ds = s_dirStamps[i];
		string rkey = lowercase(ds.rootDir);
		presentCachedRoots[rkey] = true;

		if (rootIdxByLower.find(rkey) == rootIdxByLower.end()) {
			dirtyRoots[rkey] = true; // emulator removed or path changed
			continue;
		}
		long long curMtime = getDirMtime(ds.dir);
		if (curMtime != ds.mtime) {
			dirtyRoots[rkey] = true;
		}
	}

	// Check for newly added emulator roots not yet in cache
	for (size_t i = 0; i < roots.size(); i++) {
		string rkey = lowercase(roots[i].rootDir);
		if (presentCachedRoots.find(rkey) == presentCachedRoots.end()) {
			dirtyRoots[rkey] = true;
		}
	}

	if (dirtyRoots.empty()) return;

	INFO("SearchDialog: detected %u changed ROM folder root(s), updating incrementally...", (unsigned)dirtyRoots.size());
	drawSearchLoadingModal(gmenu2x, gmenu2x->tr["Updating ROM index..."]);

	// Keep untouched directory stamps and ROM entries
	vector<RomDirStamp> keptStamps;
	keptStamps.reserve(s_dirStamps.size());
	for (size_t i = 0; i < s_dirStamps.size(); i++) {
		if (dirtyRoots.find(lowercase(s_dirStamps[i].rootDir)) == dirtyRoots.end()) {
			keptStamps.push_back(s_dirStamps[i]);
		}
	}
	s_dirStamps.swap(keptStamps);

	vector<RomCacheEntry> keptRoms;
	keptRoms.reserve(s_romCache.size());
	unordered_map<string, bool> seenFiles;
	for (size_t i = 0; i < s_romCache.size(); i++) {
		if (dirtyRoots.find(lowercase(s_romCache[i].rootDir)) == dirtyRoots.end()) {
			seenFiles[lowercase(s_romCache[i].romDir + "/" + s_romCache[i].romFile)] = true;
			keptRoms.push_back(s_romCache[i]);
		}
	}
	s_romCache.swap(keptRoms);

	// Re-scan only the dirty roots that still exist
	for (size_t i = 0; i < roots.size(); i++) {
		string rkey = lowercase(roots[i].rootDir);
		if (dirtyRoots.find(rkey) != dirtyRoots.end()) {
			scanRomDir(roots[i].rootDir, roots[i].rootDir, roots[i].exts, roots[i].badge, roots[i].emuLinkFile, 0, seenFiles);
		}
	}

	saveRomCacheFile();
}

void SearchDialog::ensureRomCache(bool forceRebuild) {
	if (!forceRebuild) {
		if (!s_romCacheLoaded) {
			drawSearchLoadingModal(gmenu2x, gmenu2x->tr["Loading game index..."]);
			loadRomCacheFile();
		}
		if (s_romCacheLoaded) {
			refreshChangedDirs();
			return;
		}
	}

	drawSearchLoadingModal(gmenu2x, gmenu2x->tr["Indexing ROM library..."]);

	s_dirStamps.clear();
	s_romCache.clear();
	unordered_map<string, bool> seenFiles;

	vector<EmuRoot> roots = collectEmuRoots();
	for (size_t i = 0; i < roots.size(); i++) {
		scanRomDir(roots[i].rootDir, roots[i].rootDir, roots[i].exts, roots[i].badge, roots[i].emuLinkFile, 0, seenFiles);
	}

	s_romCacheLoaded = true;
	saveRomCacheFile();
}

void SearchDialog::buildEntries() {
	allEntries.clear();
	unordered_map<string, LinkApp *> linkByFile;

	const vector<string> &secs = gmenu2x->menu->getSections();
	for (size_t si = 0; si < secs.size(); si++) {
		const string &sec = secs[si];
		if (sec == "settings") continue;
		linklist *ll = gmenu2x->menu->sectionLinks((int)si);
		if (!ll) continue;

		int prio = 1;
		if (sec == "favourites") prio = 0;
		else if (sec == "recents") prio = 0;

		string badge = makeSectionBadge(sec);

		for (size_t li = 0; li < ll->size(); li++) {
			LinkApp *la = dynamic_cast<LinkApp *>(ll->at(li));
			if (!la) continue;
			if (!la->getFile().empty()) {
				linkByFile[la->getFile()] = la;
			}
			if (sec == "recents") continue;

			SearchEntry e;
			e.isRom = false;
			e.title = clean_rom_title(la->getTitle(), false);
			if (e.title.empty()) e.title = la->getTitle();
			e.lowerTitle = lowercase(e.title);
			e.lowerRaw = lowercase(la->getTitle());
			e.badge = badge;
			e.lowerBadge = lowercase(badge);
			e.iconPath = la->getIconPath();
			e.link = la;
			e.priority = prio;
			allEntries.push_back(e);
		}
	}

	ensureRomCache(false);

	allEntries.reserve(allEntries.size() + s_romCache.size());
	unordered_map<LinkApp *, string> iconByLink;
	for (size_t i = 0; i < s_romCache.size(); i++) {
		const RomCacheEntry &rc = s_romCache[i];
		unordered_map<string, LinkApp *>::iterator it = linkByFile.find(rc.emuLinkFile);
		if (it == linkByFile.end() || it->second == NULL) continue;

		auto icIt = iconByLink.find(it->second);
		if (icIt == iconByLink.end()) {
			icIt = iconByLink.insert(std::make_pair(it->second, it->second->getIconPath())).first;
		}

		SearchEntry e;
		e.isRom = true;
		e.title = rc.cleanTitle;
		e.lowerTitle = rc.lowerTitle;
		e.lowerRaw = rc.lowerRaw;
		e.badge = rc.badge;
		e.lowerBadge = rc.lowerBadge;
		e.iconPath = icIt->second;
		e.link = it->second;
		e.romFile = rc.romFile;
		e.romDir = rc.romDir;
		e.priority = 2;
		allEntries.push_back(std::move(e));
	}
}

void SearchDialog::updateMatches() {
	matches.clear();
	selected = 0;
	firstRow = 0;
	selChangedTick = SDL_GetTicks();

	string q = lowercase(trim(query));
	vector<string> tokens;
	if (!q.empty()) {
		split(tokens, q, " ");
	}

	struct Scored {
		int idx;
		int score;
	};
	vector<Scored> scored;
	scored.reserve(256);

	for (size_t i = 0; i < allEntries.size(); i++) {
		const SearchEntry &e = allEntries[i];
		if (scope == SCOPE_LINKS && e.isRom) continue;
		if (scope == SCOPE_ROMS && !e.isRom) continue;

		if (q.empty()) {
			if (scope == SCOPE_ALL && e.isRom) continue;
			Scored sc;
			sc.idx = (int)i;
			sc.score = e.priority * 100;
			scored.push_back(sc);
			if (scored.size() >= 150) break;
			continue;
		}

		bool ok = true;
		int bestScore = 60;

		for (size_t t = 0; t < tokens.size(); t++) {
			const string &tok = tokens[t];
			if (tok.empty()) continue;

			size_t posTitle = e.lowerTitle.find(tok);
			if (posTitle != string::npos) {
				int s = 40;
				if (t == 0) {
					if (e.lowerTitle == q) s = 0;
					else if (posTitle == 0) s = 10;
					else if (posTitle > 0 && (e.lowerTitle[posTitle - 1] == ' ' || e.lowerTitle[posTitle - 1] == '-' || e.lowerTitle[posTitle - 1] == '_')) s = 20;
				}
				if (s < bestScore) bestScore = s;
			} else if (e.lowerRaw.find(tok) != string::npos || e.lowerBadge.find(tok) != string::npos) {
				if (bestScore > 55) bestScore = 55;
			} else {
				ok = false;
				break;
			}
		}

		if (ok) {
			if (e.priority == 0) bestScore -= 4;
			else if (e.priority == 1) bestScore -= 2;
			Scored sc;
			sc.idx = (int)i;
			sc.score = bestScore;
			scored.push_back(sc);
		}
	}

	const size_t maxResults = 120;
	auto cmp = [this](const Scored &a, const Scored &b) {
		if (a.score != b.score) return a.score < b.score;
		return allEntries[a.idx].lowerTitle < allEntries[b.idx].lowerTitle;
	};

	if (scored.size() > maxResults) {
		std::partial_sort(scored.begin(), scored.begin() + maxResults, scored.end(), cmp);
		scored.resize(maxResults);
	} else {
		std::sort(scored.begin(), scored.end(), cmp);
	}

	matches.reserve(scored.size());
	for (size_t i = 0; i < scored.size(); i++) {
		matches.push_back(scored[i].idx);
	}
}

void SearchDialog::launchEntry(const SearchEntry &e) {
	if (!e.link) return;
	if (e.isRom) {
		e.link->launch(e.romFile, e.romDir);
	} else {
		e.link->run();
	}
}

void SearchDialog::addEntryToFavourites(const SearchEntry &e) {
	if (!e.link) return;
	gmenu2x->menu->addSection("favourites");

	string favTitle = e.title;
	string linkBase = e.isRom ? (favTitle + "." + base_name(e.link->getExec(), true)) : favTitle;
	linkBase = strreplace(linkBase, "/", "_");
	string linkpath = home_path("sections/favourites/") + linkBase + ".lnk";

	if (file_exists(linkpath)) {
		MessageBox mb(gmenu2x, gmenu2x->tr["Already in Favourites"]);
		mb.setAutoHide(800);
		mb.exec();
		return;
	}

	LinkApp *fav = new LinkApp(gmenu2x, linkpath.c_str());
	fav->setExec(e.link->getExec());
	fav->setTitle(favTitle);
	fav->setDescription(e.link->getDescription());
	fav->setIcon(e.link->getIcon().empty() ? e.iconPath : e.link->getIcon());
	fav->setCPU(e.link->getCPU());
	fav->setScaleMode(e.link->getScaleMode());

	if (e.isRom) {
		string p = e.link->getParams();
		string fullPath = e.romDir + "/" + e.romFile;
		if (p.empty()) {
			p = cmdclean(fullPath);
		} else {
			string orig = p;
			p = strreplace(p, "[selFullPath]", cmdclean(fullPath));
			p = strreplace(p, "%f", cmdclean(fullPath));
			p = strreplace(p, "[selPath]", cmdclean(e.romDir));
			p = strreplace(p, "[selFile]", cmdclean(e.romFile));
			p = strreplace(p, "[selFileNoExt]", cmdclean(base_name(e.romFile, true)));
			p = strreplace(p, "[selExt]", cmdclean(file_ext(e.romFile, false)));
			if (p == orig) p += " " + cmdclean(fullPath);
		}
		fav->setParams(p);
	} else {
		fav->setParams(e.link->getParams());
		fav->setSelectorDir(e.link->getSelectorDir());
		fav->setSelectorFilter(e.link->getSelectorFilter());
		fav->setSelectorScreens(e.link->getSelectorScreens());
		fav->setAliasFile(e.link->getAliasFile());
	}

	fav->save();
	sync();

	int favSec = gmenu2x->menu->getSectionIndexByName("favourites");
	if (favSec >= 0) {
		gmenu2x->menu->sectionLinks(favSec)->push_back(fav);
		gmenu2x->menu->sortFavourites();
	} else {
		delete fav;
	}

	MessageBox mb(gmenu2x, gmenu2x->tr["Added to Favourites"] + ":\n" + favTitle);
	mb.setAutoHide(900);
	mb.exec();
}

void SearchDialog::cycleScope(int dir) {
	scope = (scope + 3 + dir) % 3;
	updateMatches();
}

void SearchDialog::doRebuildIndex() {
	ensureRomCache(true);
	buildEntries();
	updateMatches();
}

void SearchDialog::doClearQuery() {
	query.clear();
	focusResults = false;
	updateMatches();
}

void SearchDialog::doAddSelectedFav() {
	if (!matches.empty() && selected >= 0 && selected < (int)matches.size()) {
		addEntryToFavourites(allEntries[matches[selected]]);
	}
}

void SearchDialog::doLaunchSelected() {
	if (!matches.empty() && selected >= 0 && selected < (int)matches.size()) {
		launchEntry(allEntries[matches[selected]]);
	}
}

void SearchDialog::openContextMenu() {
	vector<MenuOption> options;
	if (!matches.empty() && selected >= 0 && selected < (int)matches.size()) {
		const SearchEntry &e = allEntries[matches[selected]];
		options.push_back((MenuOption){gmenu2x->tr["Launch"] + " " + e.title, MakeDelegate(this, &SearchDialog::doLaunchSelected)});
		if (e.priority != 0) {
			options.push_back((MenuOption){gmenu2x->tr["Add to Favourites"], MakeDelegate(this, &SearchDialog::doAddSelectedFav)});
		}
	}
	if (!query.empty()) {
		options.push_back((MenuOption){gmenu2x->tr["Clear search"], MakeDelegate(this, &SearchDialog::doClearQuery)});
	}
	options.push_back((MenuOption){gmenu2x->tr["Rebuild ROM index"], MakeDelegate(this, &SearchDialog::doRebuildIndex)});
	MessageBox mb(gmenu2x, options);
}

void SearchDialog::drawScreen() {
	Surface *s = gmenu2x->s;
	int W = gmenu2x->platform->w;
	int H = gmenu2x->platform->h;
	int bottomH = gmenu2x->bottomBarRect.h;
	int topH = 26;
	needsMarquee = false;

	this->bg->blit(s, 0, 0);

	// Top search bar
	s->box(0, 0, W, topH, gmenu2x->skinConfColor["topBarBg"]);

	const char *scopeLabel = (scope == SCOPE_ALL) ? "ALL" : (scope == SCOPE_LINKS ? "APPS" : "ROMS");
	string countStr = string(scopeLabel) + " (" + std::to_string(matches.size()) + ")";
	int rightW = gmenu2x->font->getTextWidth(countStr) + 12;

	// Query box
	SDL_Rect qRect = {4, 3, (Uint16)(W - rightW - 8), (Uint16)(topH - 6)};
	s->box(qRect, (RGBAColor){0, 0, 0, 120});
	s->rectangle(qRect, focusResults ? (RGBAColor){180, 180, 200, 120} : gmenu2x->skinConfColor["messageBoxBorder"]);

	string qDisplay = query.empty()
		? (focusResults ? gmenu2x->tr["All items"] : gmenu2x->tr["Type to search..."])
		: (query + (focusResults ? "" : "_"));
	RGBAColor qColor = query.empty() ? (RGBAColor){185, 195, 210, 200} : gmenu2x->skinConfColor["fontAlt"];
	s->setClipRect(qRect);
	s->write(gmenu2x->font, qDisplay, qRect.x + 5, qRect.y + qRect.h / 2, VAlignMiddle, qColor, gmenu2x->skinConfColor["fontAltOutline"]);
	s->clearClipRect();

	// Scope + count pill on right
	s->write(gmenu2x->font, countStr, W - 5, topH / 2, HAlignRight | VAlignMiddle, gmenu2x->skinConfColor["fontAlt"], gmenu2x->skinConfColor["fontAltOutline"]);

	// Compute panes
	int kbdH = focusResults ? 0 : 84;
	int listY = topH;
	int listH = H - topH - bottomH - kbdH;
	SDL_Rect resRect = {0, (Sint16)listY, (Uint16)W, (Uint16)listH};

	s->box(resRect, gmenu2x->skinConfColor["listBg"]);

	int rowH = gmenu2x->font->height() + 3;
	if (rowH < 15) rowH = 15;
	int visibleRows = listH / rowH;
	if (visibleRows < 1) visibleRows = 1;

	if (matches.empty()) {
		string msg = query.empty() ? gmenu2x->tr["No items found"] : (gmenu2x->tr["No matches for"] + " \"" + query + "\"");
		s->write(gmenu2x->font, msg, W / 2, listY + listH / 2, HAlignCenter | VAlignMiddle, gmenu2x->skinConfColor["font"], gmenu2x->skinConfColor["fontOutline"]);
	} else {
		if (selected < 0) selected = 0;
		if (selected >= (int)matches.size()) selected = (int)matches.size() - 1;
		if (selected < firstRow) firstRow = selected;
		if (selected >= firstRow + visibleRows) firstRow = selected - visibleRows + 1;

		int y = listY + 1;
		for (int i = firstRow; i < (int)matches.size() && i < firstRow + visibleRows; i++, y += rowH) {
			const SearchEntry &e = allEntries[matches[i]];
			if (i == selected) {
				RGBAColor selCol = gmenu2x->skinConfColor["selectionBg"];
				if (!focusResults) selCol.a = (uint8_t)(selCol.a * 2 / 3);
				s->box(0, y, W - 8, rowH, selCol);
			}

			// Right-aligned system/section badge
			int badgeW = gmenu2x->font->getTextWidth(e.badge) + 8;
			int badgeX = W - 10 - badgeW;
			s->box(badgeX, y + 2, badgeW, rowH - 4, (RGBAColor){0, 0, 0, 90});
			s->write(gmenu2x->font, e.badge, badgeX + badgeW / 2, y + rowH / 2, HAlignCenter | VAlignMiddle, gmenu2x->skinConfColor["fontAlt"], gmenu2x->skinConfColor["fontAltOutline"]);

			// Small category icon
			string iconKey = e.isRom ? "skin:imgs/file.png" : (e.priority == 0 ? "skin:imgs/fav.png" : "skin:imgs/go-up.png");
			Surface *ico = gmenu2x->sc[iconKey];
			if (ico) ico->blit(s, 10, y + rowH / 2, HAlignCenter | VAlignMiddle);

			// Clipped title (marquee scrolls when selected & overflowing)
			int clipW = badgeX - 24;
			if (clipW < 20) clipW = 20;
			SDL_Rect titleClip = {20, (Sint16)y, (Uint16)clipW, (Uint16)rowH};
			int shift = 0;
			if (i == selected) {
				int tw = (int)gmenu2x->font->getTextWidth(e.title);
				if (tw > clipW) {
					needsMarquee = true;
					shift = marquee_offset(tw, clipW, SDL_GetTicks() - selChangedTick);
				}
			}
			s->setClipRect(titleClip);
			s->write(gmenu2x->font, e.title, 20 - shift, y + rowH / 2, VAlignMiddle, gmenu2x->skinConfColor["font"], gmenu2x->skinConfColor["fontOutline"]);
			s->clearClipRect();
		}

		if ((int)matches.size() > visibleRows) {
			gmenu2x->drawScrollBar(visibleRows, matches.size(), firstRow, resRect);
		}
	}

	// Compact On-Screen Keyboard when typing
	if (!focusResults) {
		int kbdY = H - bottomH - kbdH;
		s->box(0, kbdY, W, kbdH, (RGBAColor){12, 18, 34, 225});
		s->box(0, kbdY, W, 1, gmenu2x->skinConfColor["messageBoxBorder"]);

		int padX = 4;
		int padY = 3;
		int gap = 2;
		int keyW = (W - padX * 2 - gap * (KBD_COLS - 1)) / KBD_COLS;
		int keyH = (kbdH - padY * 2 - gap * (KBD_ROWS - 1)) / KBD_ROWS;
		int startX = (W - (keyW * KBD_COLS + gap * (KBD_COLS - 1))) / 2;

		for (int r = 0; r < KBD_ROWS; r++) {
			for (int c = 0; c < KBD_COLS; c++) {
				int kx = startX + c * (keyW + gap);
				int ky = kbdY + padY + r * (keyH + gap);
				bool sel = (r == keyRow && c == keyCol);

				if (sel) {
					s->box(kx, ky, keyW, keyH, gmenu2x->skinConfColor["selectionBg"]);
					s->rectangle(kx, ky, keyW, keyH, gmenu2x->skinConfColor["messageBoxBorder"]);
				} else {
					bool special = (r == 2 && c == 9) || (r == 3 && c >= 7);
					s->box(kx, ky, keyW, keyH, special ? (RGBAColor){60, 80, 120, 180} : (RGBAColor){255, 255, 255, 32});
				}
				s->write(gmenu2x->font, KBD_LABELS[r][c], kx + keyW / 2, ky + keyH / 2, HAlignCenter | VAlignMiddle, gmenu2x->skinConfColor["font"], gmenu2x->skinConfColor["fontOutline"]);
			}
		}
	}

	// Bottom bar hints
	buttons.clear();
	if (!focusResults) {
		buttons.push_back({"a", gmenu2x->tr["Type"]});
		buttons.push_back({"b", query.empty() ? gmenu2x->tr["Close"] : gmenu2x->tr["Bksp"]});
		buttons.push_back({"y", gmenu2x->tr["List"]});
		buttons.push_back({"l", scopeLabel});
		buttons.push_back({"start", gmenu2x->tr["Go"]});
	} else {
		buttons.push_back({"a", gmenu2x->tr["Launch"]});
		buttons.push_back({"y", gmenu2x->tr["Keys"]});
		buttons.push_back({"x", gmenu2x->tr["Fav"]});
		buttons.push_back({"l", scopeLabel});
		buttons.push_back({"b", gmenu2x->tr["Back"]});
	}
	drawBottomBar(s, buttons);

	s->flip();
}

void SearchDialog::exec() {
	buildEntries();
	updateMatches();
	gmenu2x->input->dropEvents();

	while (true) {
		if (search_marquee_timer != NULL) {
			SDL_RemoveTimer(search_marquee_timer);
			search_marquee_timer = NULL;
		}
		drawScreen();
		if (needsMarquee) {
			search_marquee_timer = SDL_AddTimer(50, searchMarqueeTick, NULL);
		}

		bool inputAction = false;
		do {
			inputAction = gmenu2x->input->update();
		} while (!inputAction);

		if (search_marquee_timer != NULL) {
			SDL_RemoveTimer(search_marquee_timer);
			search_marquee_timer = NULL;
		}

		if (gmenu2x->inputCommonActions(inputAction)) continue;

		if (gmenu2x->input->isActive(SECTION_PREV) || gmenu2x->input->isActive(PAGEUP)) {
			cycleScope(-1);
			continue;
		}
		if (gmenu2x->input->isActive(SECTION_NEXT) || gmenu2x->input->isActive(PAGEDOWN)) {
			cycleScope(1);
			continue;
		}
		if (gmenu2x->input->isActive(MENU)) {
			openContextMenu();
			continue;
		}

		if (!focusResults) {
			if (gmenu2x->input->isActive(LEFT)) {
				keyCol = (keyCol + KBD_COLS - 1) % KBD_COLS;
			} else if (gmenu2x->input->isActive(RIGHT)) {
				keyCol = (keyCol + 1) % KBD_COLS;
			} else if (gmenu2x->input->isActive(UP)) {
				if (keyRow == 0 && !matches.empty()) {
					focusResults = true;
					selChangedTick = SDL_GetTicks();
				} else {
					keyRow = (keyRow + KBD_ROWS - 1) % KBD_ROWS;
				}
			} else if (gmenu2x->input->isActive(DOWN)) {
				keyRow = (keyRow + 1) % KBD_ROWS;
			} else if (gmenu2x->input->isActive(CONFIRM)) {
				string lbl = KBD_LABELS[keyRow][keyCol];
				if (lbl == "<-") {
					if (!query.empty()) {
						query.pop_back();
						updateMatches();
					}
				} else if (lbl == "_") {
					if (!query.empty() && query.back() != ' ') {
						query += ' ';
						updateMatches();
					}
				} else if (lbl == "CLR") {
					if (!query.empty()) {
						query.clear();
						updateMatches();
					}
				} else if (lbl == "GO") {
					if (!matches.empty()) {
						focusResults = true;
						selChangedTick = SDL_GetTicks();
					}
				} else if (query.size() < 28) {
					query += (char)tolower((unsigned char)lbl[0]);
					updateMatches();
				}
			} else if (gmenu2x->input->isActive(CANCEL)) {
				if (!query.empty()) {
					query.pop_back();
					updateMatches();
				} else {
					return;
				}
			} else if (gmenu2x->input->isActive(MODIFIER)) {
				if (!query.empty() && query.back() != ' ' && query.size() < 28) {
					query += ' ';
					updateMatches();
				}
			} else if (gmenu2x->input->isActive(MANUAL) || gmenu2x->input->isActive(SETTINGS)) {
				if (!matches.empty()) {
					focusResults = true;
					selChangedTick = SDL_GetTicks();
				}
			}
		} else {
			int visibleRows = (gmenu2x->platform->h - 26 - gmenu2x->bottomBarRect.h) / std::max(15, (int)gmenu2x->font->height() + 3);
			if (visibleRows < 1) visibleRows = 1;

			if (gmenu2x->input->isActive(UP)) {
				if (!matches.empty()) {
					selected = (selected > 0) ? (selected - 1) : ((int)matches.size() - 1);
					selChangedTick = SDL_GetTicks();
				}
			} else if (gmenu2x->input->isActive(DOWN)) {
				if (!matches.empty()) {
					selected = (selected + 1 < (int)matches.size()) ? (selected + 1) : 0;
					selChangedTick = SDL_GetTicks();
				}
			} else if (gmenu2x->input->isActive(LEFT)) {
				selected = std::max(0, selected - visibleRows);
				selChangedTick = SDL_GetTicks();
			} else if (gmenu2x->input->isActive(RIGHT)) {
				if (!matches.empty())
					selected = std::min((int)matches.size() - 1, selected + visibleRows);
				selChangedTick = SDL_GetTicks();
			} else if (gmenu2x->input->isActive(CONFIRM)) {
				if (!matches.empty()) {
					SearchEntry chosen = allEntries[matches[selected]];
					launchEntry(chosen);
					return;
				}
			} else if (gmenu2x->input->isActive(MODIFIER)) {
				doAddSelectedFav();
			} else if (gmenu2x->input->isActive(MANUAL) || gmenu2x->input->isActive(CANCEL)) {
				focusResults = false;
			} else if (gmenu2x->input->isActive(SETTINGS)) {
				return;
			}
		}
	}
}
