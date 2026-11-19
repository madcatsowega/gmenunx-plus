#ifndef PLAYSTATS_H_
#define PLAYSTATS_H_

#include <stdint.h>
#include <string>
#include <vector>
#include <tr1/unordered_map>

struct PlayStatEntry {
	std::string key;
	std::string title;
	uint32_t totalSeconds = 0;
	uint32_t launches = 0;
	uint64_t lastPlayed = 0; // epoch seconds (for recency ordering)
};

class PlayStats {
private:
	std::tr1::unordered_map<std::string, PlayStatEntry> entries;
	bool loaded = false;

	void load();
	void save();
	static double readUptimeSeconds();

public:
	static std::string makeKey(const std::string &exec, const std::string &params);
	static std::string formatDuration(uint32_t secs);

	void ensureLoaded();
	void beginSession(const std::string &exec, const std::string &params, const std::string &title);
	void finishPendingSession();

	const PlayStatEntry *get(const std::string &exec, const std::string &params);
	uint32_t getSeconds(const std::string &exec, const std::string &params);
	uint64_t getLastPlayed(const std::string &exec, const std::string &params);
	std::vector<PlayStatEntry> getTopPlayed(size_t maxCount = 50);
	uint32_t getTotalSeconds();
};

#endif /* PLAYSTATS_H_ */
