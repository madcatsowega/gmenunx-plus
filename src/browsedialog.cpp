#include "browsedialog.h"
#include "inputdialog.h"
#include "messagebox.h"
#include "debug.h"
#include "utilities.h"
#include "powermanager.h"
#include "inputmanager.h"

SDL_TimerID alphanum_timer = NULL;
static SDL_TimerID browse_marquee_timer = NULL;

uint32_t hideAlphaNum(uint32_t interval, void *param) {
	SDL_RemoveTimer(alphanum_timer); alphanum_timer = NULL;
	InputManager::wakeUp(0, (void*)false);
	return 0;
};

static uint32_t browseMarqueeTick(uint32_t interval, void *param) {
	browse_marquee_timer = NULL;
	InputManager::wakeUp(0, (void*)false);
	return 0;
}

BrowseDialog::BrowseDialog(GMenu2X *gmenu2x, const string &title, const string &description, const string &icon):
Dialog(gmenu2x, title, description, icon) {
	srand(SDL_GetTicks());
}

void BrowseDialog::stepLetter(int dir) {
	if (size() <= 1) return;
	uint32_t startIdx = dirCount();
	if (startIdx >= size()) startIdx = 0;

	if ((uint32_t)selected < startIdx) {
		selected = (int32_t)startIdx;
		return;
	}

	char cur = letter_group(getFileName(selected));
	if (dir > 0) {
		uint32_t idx = (uint32_t)selected + 1;
		while (idx < size() && letter_group(getFileName(idx)) == cur) {
			idx++;
		}
		if (idx >= size()) idx = startIdx;
		selected = (int32_t)idx;
	} else {
		// If we are not at the first item of the current letter, jump to start of current letter first?
		// Better: jump straight to the first item of the previous letter group!
		int32_t idx = selected;
		while (idx > (int32_t)startIdx && letter_group(getFileName(idx - 1)) == cur) {
			idx--;
		}
		if (idx > (int32_t)startIdx) {
			idx--; // now at last item of previous letter group
			char prevLetter = letter_group(getFileName(idx));
			while (idx > (int32_t)startIdx && letter_group(getFileName(idx - 1)) == prevLetter) {
				idx--;
			}
			selected = idx;
		} else {
			// wrap to first item of last letter group
			int32_t last = (int32_t)size() - 1;
			char lastLetter = letter_group(getFileName(last));
			while (last > (int32_t)startIdx && letter_group(getFileName(last - 1)) == lastLetter) {
				last--;
			}
			selected = last;
		}
	}
}

void BrowseDialog::jumpToLetter() {
	if (size() == 0) return;

	static const char *letters = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const int totalLetters = 27;
	int firstForLetter[27];
	for (int i = 0; i < totalLetters; i++) firstForLetter[i] = -1;

	uint32_t startIdx = dirCount();
	if (startIdx >= size()) startIdx = 0;

	for (uint32_t i = startIdx; i < size(); i++) {
		char c = letter_group(getFileName(i));
		int li = (c >= 'A' && c <= 'Z') ? (c - 'A' + 1) : 0;
		if (firstForLetter[li] < 0) firstForLetter[li] = (int)i;
	}

	// Start cursor on current item's letter
	int curIdx = 0;
	if ((uint32_t)selected >= startIdx && (uint32_t)selected < size()) {
		char curC = letter_group(getFileName(selected));
		curIdx = (curC >= 'A' && curC <= 'Z') ? (curC - 'A' + 1) : 0;
	}

	const int cols = 9;
	const int rows = 3;
	const int cellW = 28;
	const int cellH = 26;
	const int pad = 8;
	const int boxW = cols * cellW + pad * 2;
	const int boxH = rows * cellH + pad * 2 + 22;
	const int boxX = (gmenu2x->platform->w - boxW) / 2;
	const int boxY = (gmenu2x->platform->h - boxH) / 2;

	Surface *snapshot = new Surface(gmenu2x->s);
	gmenu2x->input->dropEvents();

	while (true) {
		snapshot->blit(gmenu2x->s, 0, 0);
		gmenu2x->s->box(boxX, boxY, boxW, boxH, gmenu2x->skinConfColor["messageBoxBg"]);
		gmenu2x->s->rectangle(boxX, boxY, boxW, boxH, gmenu2x->skinConfColor["messageBoxBorder"]);

		gmenu2x->s->write(gmenu2x->font, gmenu2x->tr["Jump to letter"], boxX + boxW / 2, boxY + 12, HAlignCenter | VAlignMiddle, gmenu2x->skinConfColor["font"], gmenu2x->skinConfColor["fontOutline"]);

		for (int i = 0; i < totalLetters; i++) {
			int r = i / cols;
			int c = i % cols;
			int cx = boxX + pad + c * cellW;
			int cy = boxY + 22 + pad / 2 + r * cellH;

			bool hasItems = (firstForLetter[i] >= 0);
			if (i == curIdx) {
				gmenu2x->s->box(cx + 1, cy + 1, cellW - 2, cellH - 2, gmenu2x->skinConfColor["messageBoxSelection"]);
				gmenu2x->s->rectangle(cx + 1, cy + 1, cellW - 2, cellH - 2, gmenu2x->skinConfColor["messageBoxBorder"]);
			} else if (hasItems) {
				gmenu2x->s->box(cx + 1, cy + 1, cellW - 2, cellH - 2, (RGBAColor){255, 255, 255, 24});
			}

			RGBAColor fg = hasItems ? gmenu2x->skinConfColor["font"] : (RGBAColor){140, 140, 150, 160};
			string lbl(1, letters[i]);
			gmenu2x->s->write(gmenu2x->font, lbl, cx + cellW / 2, cy + cellH / 2, HAlignCenter | VAlignMiddle, fg, gmenu2x->skinConfColor["fontOutline"]);
		}
		gmenu2x->s->flip();

		bool inputAction = false;
		do {
			inputAction = gmenu2x->input->update();
		} while (!inputAction);

		if (gmenu2x->inputCommonActions(inputAction)) continue;

		if (gmenu2x->input->isActive(LEFT)) {
			curIdx = (curIdx + totalLetters - 1) % totalLetters;
		} else if (gmenu2x->input->isActive(RIGHT)) {
			curIdx = (curIdx + 1) % totalLetters;
		} else if (gmenu2x->input->isActive(UP)) {
			curIdx = (curIdx + totalLetters - cols) % totalLetters;
		} else if (gmenu2x->input->isActive(DOWN)) {
			curIdx = (curIdx + cols) % totalLetters;
		} else if (gmenu2x->input->isActive(CONFIRM)) {
			if (firstForLetter[curIdx] >= 0) {
				selected = firstForLetter[curIdx];
			} else {
				// Find nearest letter >= curIdx, else wrap
				for (int step = 1; step < totalLetters; step++) {
					int ni = (curIdx + step) % totalLetters;
					if (firstForLetter[ni] >= 0) {
						selected = firstForLetter[ni];
						break;
					}
				}
			}
			break;
		} else if (gmenu2x->input->isActive(CANCEL) || gmenu2x->input->isActive(MENU) || gmenu2x->input->isActive(SETTINGS)) {
			break;
		}
	}

	delete snapshot;
	gmenu2x->input->dropEvents();
}

void BrowseDialog::filterFolder() {
	InputDialog id(gmenu2x, gmenu2x->tr["Filter folder"], nameFilter, title, icon);
	if (id.exec()) {
		setNameFilter(id.getInput());
		selected = 0;
		directoryEnter(path);
	}
}

void BrowseDialog::clearFilter() {
	if (!nameFilter.empty()) {
		setNameFilter("");
		selected = 0;
		directoryEnter(path);
	}
}

void BrowseDialog::pickRandom() {
	if (fileCount() > 0) {
		alphanum_timer = SDL_AddTimer(1500, hideAlphaNum, (void*)false);
		selected = (rand() % fileCount()) + dirCount() + favCount();
		if (selected >= (int32_t)size()) selected = (int32_t)size() - 1;
	}
}

bool BrowseDialog::exec(string _path) {
	delete this->bg;
	this->bg = new Surface(gmenu2x->bg); // needed to redraw on child screen return

	uint32_t i, iY, firstElement = 0, padding = 6;
	int32_t animation = 0;
	uint32_t rowHeight = gmenu2x->font->height() + 1;
	uint32_t numRows = (gmenu2x->listRect.h - 2) / rowHeight - 1;

	if (_path.empty() || !dir_exists(_path))
		_path = gmenu2x->confStr["homePath"];

	directoryEnter(_path);

	if (!selectFileName.empty()) {
		for (uint32_t idx = 0; idx < size(); idx++) {
			if (getFile(idx) == selectFileName) {
				selected = idx;
				break;
			}
		}
		selectFileName.clear();
	}

	string preview = getPreview(selected);
	uint32_t selChangedTick = SDL_GetTicks();
	int32_t lastSelected = selected;

	// this->description = getFilter();

	while (true) {
		if (browse_marquee_timer != NULL) {
			SDL_RemoveTimer(browse_marquee_timer);
			browse_marquee_timer = NULL;
		}
		bool needsMarquee = false;

		if (size() == 0) {
			selected = 0;
		} else {
			if (selected < 0) selected = this->size() - 1;
			if (selected >= (int32_t)this->size()) selected = 0;
		}
		if (selected != lastSelected) {
			lastSelected = selected;
			selChangedTick = SDL_GetTicks();
		}

		bool inputAction = false;

		buttons.clear();
		if (alphanum_timer != NULL && size() > 0) {
			char c = letter_group(getFileName(selected));
			string sel(1, c);
			buttons.push_back({"skin:imgs/manual.png", strreplace("#ABCDEFGHIJKLMNOPQRSTUVWXYZ", sel, " [" + sel + "] ")});
		} else {
			buttons.push_back({"select", gmenu2x->tr["Menu"]});
			buttons.push_back({"b", gmenu2x->tr["Cancel"]});

			if (!showFiles && allowSelectDirectory)
				buttons.push_back({"start", gmenu2x->tr["Select"]});
			else if (size() > 0 && ((allowEnterDirectory && isDirectory(selected)) || !isDirectory(selected)))
				buttons.push_back({"a", gmenu2x->tr["Select"]});

			if (showFiles)
				buttons.push_back({"y", nameFilter.empty() ? gmenu2x->tr["Filter"] : gmenu2x->tr["Filter*"]});

			if (showDirectories && allowDirUp && path != "/")
				buttons.push_back({"x", gmenu2x->tr["Dir up"]});
		}

		if (gmenu2x->confStr["previewMode"] == "Backdrop") {
			if (!(preview.empty() || preview == "#"))
				gmenu2x->setBackground(this->bg, preview);
			else
				gmenu2x->bg->blit(this->bg,0,0);
		}

		this->description = nameFilter.empty() ? path : (path + "  [" + nameFilter + "]");

		drawDialog(gmenu2x->s);

		if (!size()) {
			MessageBox mb(gmenu2x, nameFilter.empty() ? gmenu2x->tr["This directory is empty"] : (gmenu2x->tr["No matches for"] + " \"" + nameFilter + "\""));
			mb.setAutoHide(1);
			mb.setBgAlpha(0);
			mb.exec();
		} else {
			// Selection
			if (selected >= firstElement + numRows) firstElement = selected - numRows;
			if (selected < firstElement) firstElement = selected;

			// Files & Directories
			iY = gmenu2x->listRect.y + 1;
			for (i = firstElement; i < size() && i <= firstElement + numRows; i++, iY += rowHeight) {
				if (i == selected) gmenu2x->s->roundedBox(gmenu2x->listRect.x + 2, iY, gmenu2x->listRect.w - 8, rowHeight, gmenu2x->skinConfColor["selectionBg"], 3);

				string icon = "skin:imgs/file.png";

				if (isDirectory(i)) {
					if (getFile(i) == "..")
						icon = "skin:imgs/go-up.png";
					else if (getPath(i) == "/media" || path == "/media")
						icon = "skin:imgs/sd.png";
					else
						icon = "skin:imgs/folder.png";
				} else if (isFavourite(getFile(i))) {
					icon = "skin:imgs/fav.png";
				}

				gmenu2x->sc[icon]->blit(gmenu2x->s, gmenu2x->listRect.x + 10, iY + rowHeight/2, HAlignCenter | VAlignMiddle);

				string rowName = getFileName(i);
				int textX = gmenu2x->listRect.x + 21;
				int textWMax = gmenu2x->listRect.w - 30;
				if (textWMax < 20) textWMax = 20;
				int shift = 0;
				if ((int32_t)i == selected) {
					int tw = (int)gmenu2x->font->getTextWidth(rowName);
					if (tw > textWMax) {
						needsMarquee = true;
						shift = marquee_offset(tw, textWMax, SDL_GetTicks() - selChangedTick);
					}
				}
				gmenu2x->s->setClipRect({(Sint16)textX, (Sint16)iY, (Uint16)textWMax, (Uint16)rowHeight});
				gmenu2x->s->write(gmenu2x->font, rowName, textX - shift, iY + rowHeight/2, VAlignMiddle);
				gmenu2x->s->clearClipRect();
			}

			// Floating letter-jump badge when stepping with L/R
			if (alphanum_timer != NULL) {
				char c = letter_group(getFileName(selected));
				string badge(1, c);
				int bw = 34, bh = 30;
				int bx = gmenu2x->listRect.x + gmenu2x->listRect.w - bw - 14;
				int by = gmenu2x->listRect.y + 6;
				gmenu2x->s->box(bx, by, bw, bh, gmenu2x->skinConfColor["messageBoxBg"]);
				gmenu2x->s->rectangle(bx, by, bw, bh, gmenu2x->skinConfColor["messageBoxBorder"]);
				gmenu2x->s->write(gmenu2x->titlefont, badge, bx + bw / 2, by + bh / 2, HAlignCenter | VAlignMiddle, gmenu2x->skinConfColor["font"], gmenu2x->skinConfColor["fontOutline"]);
			}

			if (gmenu2x->confStr["previewMode"] != "Backdrop") {
				Surface *anim = new Surface(gmenu2x->s);
				if (preview.empty() || preview == "#") { // hide preview
					 while (animation > 0) {
						animation -= gmenu2x->skinConfInt["previewWidth"] / 8;

						if (animation < 0)
							animation = 0;

						anim->blit(gmenu2x->s,0,0);
						gmenu2x->s->box(gmenu2x->platform->w - animation, gmenu2x->listRect.y, gmenu2x->skinConfInt["previewWidth"] + 2 * padding, gmenu2x->listRect.h, gmenu2x->skinConfColor["previewBg"]);
						gmenu2x->s->flip();
						SDL_Delay(10);
					};
				} else { // show preview
					if (!gmenu2x->sc.exists(preview + "scaled")) {
						Surface *previm = new Surface(preview);
						gmenu2x->sc.add(previm, preview + "scaled");

						gmenu2x->sc[preview + "scaled"]->softStretch(gmenu2x->skinConfInt["previewWidth"], gmenu2x->listRect.h - 2 * padding, SScaleFit);
					}

					do {
						animation += gmenu2x->skinConfInt["previewWidth"] / 8;

						if (animation > gmenu2x->skinConfInt["previewWidth"] + 2 * padding)
							animation = gmenu2x->skinConfInt["previewWidth"] + 2 * padding;

						anim->blit(gmenu2x->s,0,0);
						gmenu2x->s->box(gmenu2x->platform->w - animation, gmenu2x->listRect.y, gmenu2x->skinConfInt["previewWidth"] + 2 * padding, gmenu2x->listRect.h, gmenu2x->skinConfColor["previewBg"]);
						gmenu2x->sc[preview + "scaled"]->blit(gmenu2x->s, {gmenu2x->platform->w - animation + padding, gmenu2x->listRect.y + padding, gmenu2x->skinConfInt["previewWidth"], gmenu2x->listRect.h - 2 * padding}, HAlignCenter | VAlignMiddle, gmenu2x->platform->h);
						gmenu2x->s->flip();
						SDL_Delay(10);
					} while (animation < gmenu2x->skinConfInt["previewWidth"] + 2 * padding);
				}
				delete anim;
			}
			gmenu2x->drawScrollBar(numRows, size(), firstElement, gmenu2x->listRect);
			gmenu2x->s->flip();
		}

		if (needsMarquee) {
			browse_marquee_timer = SDL_AddTimer(50, browseMarqueeTick, NULL);
		}

		do {
			inputAction = gmenu2x->input->update();
		} while (!inputAction);

		if (browse_marquee_timer != NULL) {
			SDL_RemoveTimer(browse_marquee_timer);
			browse_marquee_timer = NULL;
		}

		if (gmenu2x->inputCommonActions(inputAction)) continue;

		SDL_RemoveTimer(alphanum_timer); alphanum_timer = NULL;

		if (gmenu2x->input->isActive(UP)) {
			selected--;
		} else if (gmenu2x->input->isActive(DOWN)) {
			selected++;
		} else if (gmenu2x->input->isActive(LEFT)) {
			selected -= numRows;
			if (selected < 0) selected = 0;
		} else if (gmenu2x->input->isActive(RIGHT)) {
			selected += numRows;
			if (selected >= (int32_t)this->size()) selected = this->size() - 1;
		} else if (gmenu2x->input->isActive(PAGEDOWN)) {
			if (size() > 0) {
				alphanum_timer = SDL_AddTimer(1500, hideAlphaNum, (void*)false);
				stepLetter(1);
			}
		} else if (gmenu2x->input->isActive(PAGEUP)) {
			if (size() > 0) {
				alphanum_timer = SDL_AddTimer(1500, hideAlphaNum, (void*)false);
				stepLetter(-1);
			}
		} else if (showDirectories && allowDirUp && (gmenu2x->input->isActive(MODIFIER) || (size() > 0 && gmenu2x->input->isActive(CONFIRM) && getFile(selected) == ".."))) { /*Directory Up */
			selected = 0;
			preview = "";
			setNameFilter("");
			if (browse_history.size() > 0) {
				selected = browse_history.back();
				browse_history.pop_back();
			}
			directoryEnter(path + "/..");
		} else if (gmenu2x->input->isActive(CONFIRM)) {
			if (size() == 0) continue;
			if (allowEnterDirectory && isDirectory(selected)) {
				browse_history.push_back(selected);
				setNameFilter("");
				directoryEnter(getPath(selected));
				selected = 0;
			} else {
				return true;
			}
		} else if (gmenu2x->input->isActive(SETTINGS) && allowSelectDirectory) {
			return true;
		} else if (gmenu2x->input->isActive(CANCEL) || gmenu2x->input->isActive(SETTINGS)) {
			if (!nameFilter.empty() && gmenu2x->input->isActive(CANCEL)) {
				clearFilter();
				preview = size() > 0 ? getPreview(selected) : "";
				continue;
			}
			if (!((gmenu2x->confStr["previewMode"] != "Backdrop") && !(preview.empty() || preview == "#")))
				return false; // close only if preview is empty.
			preview = "";
		} else if (gmenu2x->input->isActive(MANUAL) && showFiles) {
			filterFolder();
			preview = size() > 0 ? getPreview(selected) : "";
		} else if (gmenu2x->input->isActive(MENU)) {
			contextMenu();
			preview = size() > 0 ? getPreview(selected) : "";
		}

		if (size() > 0 && (gmenu2x->input->isActive(UP) || gmenu2x->input->isActive(DOWN) || gmenu2x->input->isActive(LEFT) || gmenu2x->input->isActive(RIGHT) || gmenu2x->input->isActive(PAGEUP) || gmenu2x->input->isActive(PAGEDOWN))) {
			preview = getPreview(selected);
		}
	}
}

void BrowseDialog::directoryEnter(string path) {
	gmenu2x->input->dropEvents(); // prevent passing input away
	gmenu2x->powerManager->clearTimer();

	this->description = path;
	buttons.clear();
	buttons.push_back({"skin:imgs/manual.png", gmenu2x->tr["Loading.. Please wait.."]});

	drawDialog(gmenu2x->s);

	SDL_TimerID flipScreenTimer = SDL_AddTimer(500, GMenu2X::timerFlip, (void*)false);

	browse(path);
	onChangeDir();

	SDL_RemoveTimer(flipScreenTimer); flipScreenTimer = NULL;
	gmenu2x->powerManager->resetSuspendTimer();
}

const std::string BrowseDialog::getFileName(uint32_t i) {
	return getFile(i);
}
const std::string BrowseDialog::getParams(uint32_t i) {
	return "";
}
const std::string BrowseDialog::getPreview(uint32_t i) {
	string ext = getExt(i);
	if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp") return getPath(i);
	return "";
}

void BrowseDialog::contextMenu() {
	vector<MenuOption> options;

	string ext = size() > 0 ? getExt(selected) : "";

	if (showFiles && size() > 0) {
		options.push_back((MenuOption){gmenu2x->tr["Jump to letter"], MakeDelegate(this, &BrowseDialog::jumpToLetter)});
		options.push_back((MenuOption){gmenu2x->tr["Filter folder"], MakeDelegate(this, &BrowseDialog::filterFolder)});
		if (!nameFilter.empty())
			options.push_back((MenuOption){gmenu2x->tr["Clear filter"], MakeDelegate(this, &BrowseDialog::clearFilter)});
		if (fileCount() > 1)
			options.push_back((MenuOption){gmenu2x->tr["Random game"], MakeDelegate(this, &BrowseDialog::pickRandom)});
	}

	customOptions(options);

	if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp")
		options.push_back((MenuOption){gmenu2x->tr["Set as wallpaper"], MakeDelegate(this, &BrowseDialog::setWallpaper)});

	if (path == "/media" && getFile(selected) != ".." && isDirectory(selected))
		options.push_back((MenuOption){gmenu2x->tr["Umount"], MakeDelegate(this, &BrowseDialog::umountDir)});

	if (path != home_path("../"))
		options.push_back((MenuOption){gmenu2x->tr["Go to"] + " " + home_path("../"), MakeDelegate(this, &BrowseDialog::exploreHome)});

	if (path != "/media")
		options.push_back((MenuOption){gmenu2x->tr["Go to"] + " /media", MakeDelegate(this, &BrowseDialog::exploreMedia)});

	if (isFile(selected))
		options.push_back((MenuOption){gmenu2x->tr["Delete"], MakeDelegate(this, &BrowseDialog::deleteFile)});

	MessageBox mb(gmenu2x, options);
}

void BrowseDialog::deleteFile() {
	MessageBox mb(gmenu2x, gmenu2x->tr["Delete"] + " '" +  getFile(selected) + "'\n" + gmenu2x->tr["THIS CAN'T BE UNDONE"] + "\n" + gmenu2x->tr["Are you sure?"], "explorer.png");
	mb.setButton(MANUAL, gmenu2x->tr["Yes"]);
	mb.setButton(CANCEL,  gmenu2x->tr["No"]);
	if (mb.exec() != MANUAL) return;
	if (!unlink(getPath(selected).c_str())) {
		directoryEnter(path); // refresh
		sync();
	}
}

void BrowseDialog::umountDir() {
	string umount = "sync; umount -fl " + getPath(selected) + " && rm -r " + getPath(selected);
	system(umount.c_str());
	directoryEnter(path); // refresh
}

void BrowseDialog::exploreHome() {
	selected = 0;
	directoryEnter(home_path("../"));
}

void BrowseDialog::exploreMedia() {
	selected = 0;
	directoryEnter("/media");
}

void BrowseDialog::setWallpaper() {
	string src = getPath(selected);
	string dst = home_path("Wallpaper") + file_ext(src, true);
	if (file_copy(src, dst)) {
		gmenu2x->confStr["wallpaper"] = dst;
		gmenu2x->setBackground(gmenu2x->bg, dst);
		this->exec();
	}
}
