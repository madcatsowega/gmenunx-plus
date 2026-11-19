#ifndef SEARCHDIALOG_H_
#define SEARCHDIALOG_H_

#include <string>
#include <vector>
#include "dialog.h"
#include "gmenu2x.h"

class LinkApp;

struct RomCacheEntry {
	std::string romDir;
	std::string romFile;
	std::string cleanTitle;
	std::string lowerTitle;
	std::string lowerRaw;
	std::string badge;
	std::string lowerBadge;
	std::string emuLinkFile;
};

struct SearchEntry {
	bool isRom = false;
	std::string title;
	std::string lowerTitle;
	std::string lowerRaw;
	std::string badge;
	std::string lowerBadge;
	std::string iconPath;
	LinkApp *link = NULL;
	std::string romFile;
	std::string romDir;
	int priority = 2; // 0 = favourite, 1 = app/game/emu, 2 = rom
};

class SearchDialog : protected Dialog {
private:
	enum Scope {
		SCOPE_ALL = 0,
		SCOPE_LINKS = 1,
		SCOPE_ROMS = 2
	};

	std::string query;
	int scope = SCOPE_ALL;
	bool focusResults = false;
	int keyRow = 1;
	int keyCol = 0;
	int selected = 0;
	int firstRow = 0;

	std::vector<SearchEntry> allEntries;
	std::vector<int> matches;

	static std::vector<RomCacheEntry> s_romCache;
	static bool s_romCacheLoaded;

	void buildEntries();
	void ensureRomCache(bool forceRebuild = false);
	bool loadRomCacheFile();
	void saveRomCacheFile();
	void scanRomDir(const std::string &dir, const std::vector<std::string> &exts,
	                const std::string &badge, const std::string &emuLinkFile,
	                int depth, unordered_map<std::string, bool> &seen);
	void updateMatches();
	void launchEntry(const SearchEntry &e);
	void addEntryToFavourites(const SearchEntry &e);
	void openContextMenu();
	void cycleScope(int dir = 1);
	void doRebuildIndex();
	void doClearQuery();
	void doAddSelectedFav();
	void doLaunchSelected();
	void drawScreen();

public:
	SearchDialog(GMenu2X *gmenu2x);
	virtual ~SearchDialog() {}
	void exec();
	static void invalidateRomCache();
};

#endif /* SEARCHDIALOG_H_ */
