#include "fonthelper.h"
#include "utilities.h"
#include "debug.h"
#include <sstream>
#include <cstdio>

const uint8_t outline = 1;
static const size_t MAX_TEXT_CACHE = 160;

FontHelper::FontHelper(const string &fontName, int fontSize, RGBAColor textColor, RGBAColor outlineColor):
fontName(fontName), fontSize(fontSize), textColor(textColor), outlineColor(outlineColor), font(NULL), fontOutline(NULL) {
	loadFont(fontName, fontSize);
}

FontHelper::~FontHelper() {
	clearCache();
	if (font) TTF_CloseFont(font);
	if (fontOutline) TTF_CloseFont(fontOutline);
}

void FontHelper::clearCache() {
	for (auto it = textCache.begin(); it != textCache.end(); ++it) {
		delete it->second.surf;
	}
	textCache.clear();
	textLru.clear();
}

void FontHelper::ensureOutlineFont() {
	if (fontOutline || fontName.empty()) return;
	fontOutline = TTF_OpenFont(fontName.c_str(), fontSize);
	if (fontOutline) {
		TTF_SetFontHinting(fontOutline, TTF_HINTING_LIGHT);
		TTF_SetFontOutline(fontOutline, outline);
	}
}

void FontHelper::loadFont(const string &fontName, int fontSize) {
	if (!TTF_WasInit()) {
		DEBUG("Initializing font");
		if (TTF_Init() == -1) {
			ERROR("TTF_Init: %s", TTF_GetError());
			exit(2);
		}
	}
	this->font = TTF_OpenFont(fontName.c_str(), fontSize);
	this->fontOutline = NULL;
	if (!this->font) {
		ERROR("TTF_OpenFont %s: %s", fontName.c_str(), TTF_GetError());
		return;
	}
	TTF_SetFontHinting(this->font, TTF_HINTING_LIGHT);
	if (outlineColor.a > 0) {
		ensureOutlineFont();
	}
	fontHeight = 0;
	// Get maximum line height with a sample text
	TTF_SizeUTF8(this->font, "AZ0987654321", NULL, &fontHeight);
}

bool FontHelper::utf8Code(uint8_t c) {
	return (c >= 194 && c <= 198) || c == 208 || c == 209;
}

FontHelper *FontHelper::setSize(const int size) {
	if (this->fontSize == size) return this;
	clearCache();
	if (font) { TTF_CloseFont(font); font = NULL; }
	if (fontOutline) { TTF_CloseFont(fontOutline); fontOutline = NULL; }
	fontSize = size;
	loadFont(this->fontName, this->fontSize);
	return this;
}

FontHelper *FontHelper::setColor(RGBAColor color) {
	textColor = color;
	return this;
}

FontHelper *FontHelper::setOutlineColor(RGBAColor color) {
	outlineColor = color;
	if (outlineColor.a > 0) ensureOutlineFont();
	return this;
}

uint32_t FontHelper::getLineWidth(const string &text) {
	int width = 0;
	TTF_Font *f = fontOutline ? fontOutline : font;
	if (f) TTF_SizeUTF8(f, text.c_str(), &width, NULL);
	return width;
}

uint32_t FontHelper::getTextWidth(const string &text) {
	if (text.find("\n",0) != string::npos) {
		vector<string> textArr;
		split(textArr,text,"\n");
		return getTextWidth(&textArr);
	}
	return getLineWidth(text);
}

uint32_t FontHelper::getTextWidth(vector<string> *text) {
	int w = 0;
	for (uint32_t i = 0; i < text->size(); i++) {
		w = max(getLineWidth(text->at(i)), w);
	};
	return w;
}

int FontHelper::getTextHeight(const string &text) {
	vector<string> textArr;
	split(textArr, text, "\n");
	return textArr.size();
}

void FontHelper::write(Surface *surface, vector<string> *text, int x, int y, const uint8_t align) {
	write(surface, text, x, y, align, textColor, outlineColor);
}

void FontHelper::write(Surface *surface, vector<string> *text, int x, int y, const uint8_t align, RGBAColor fgColor, RGBAColor bgColor) {
	if (align & VAlignMiddle) {
		y -= text->size() * height() / 2;
	} else if (align & VAlignBottom) {
		y -= text->size() * height();
	}

	for (uint32_t i = 0; i < text->size(); i++) {
		int ix = x;
		if (align & HAlignCenter) {
			ix -= getTextWidth(text->at(i)) / 2;
		} else if (align & HAlignRight) {
			ix -= getTextWidth(text->at(i));
		}
		write(surface, text->at(i), ix, y + i * height(), fgColor, bgColor);
	}
}

void FontHelper::write(Surface* surface, const string &text, int x, int y, const uint8_t align, RGBAColor fgColor, RGBAColor bgColor) {
	if (text.find("\n", 0) != string::npos) {
		vector<string> textArr;
		split(textArr,text, "\n");
		write(surface, &textArr, x, y, align, fgColor, bgColor);
		return;
	}

	if (align & HAlignCenter) {
		x -= getTextWidth(text) / 2;
	} else if (align & HAlignRight) {
		x -= getTextWidth(text);
	}

	if (align & VAlignMiddle) {
		y -= height() / 2;
	} else if (align & VAlignBottom) {
		y -= height();
	}

	write(surface, text, x, y, fgColor, bgColor);
}

void FontHelper::write(Surface *surface, const string &text, int x, int y, const uint8_t align) {
	write(surface, text, x, y, align, textColor, outlineColor);
}

void FontHelper::write(Surface *surface, const string &text, SDL_Rect &wrapRect, const uint8_t align) {
	string textwrap = "";
	if (getTextWidth(text) > wrapRect.w) {
		string line = "";
		std::istringstream iss(text);
		do {
			string subs;
			iss >> subs;
			line += subs + " ";

			if (getTextWidth(line) > wrapRect.w) {
				textwrap += "\n";
				line = "";
			} else {
				textwrap += " ";
			}
			textwrap += subs;
		} while (iss);

		textwrap = trim(textwrap);
	} else {
		textwrap = trim(text);
	}
	write(surface, textwrap, wrapRect.x, wrapRect.y, align, textColor, outlineColor);
}

void FontHelper::write(Surface *surface, const string &text, int x, int y, RGBAColor fgColor, RGBAColor bgColor) {
	if (text.empty() || !font) return;

	auto renderCached = [&](TTF_Font *fnt, char tag, RGBAColor col, int dx, int dy) {
		if (!fnt || col.a == 0) return;
		char hdr[16];
		snprintf(hdr, sizeof(hdr), "%c%02x%02x%02x%02x|", tag, col.r, col.g, col.b, col.a);
		string key = string(hdr) + text;

		auto it = textCache.find(key);
		Surface *surf = NULL;
		if (it != textCache.end()) {
			textLru.splice(textLru.begin(), textLru, it->second.lruIt);
			surf = it->second.surf;
		} else {
			SDL_Surface *raw = TTF_RenderUTF8_Blended(fnt, text.c_str(), rgbatosdl(col));
			if (!raw) return;
			surf = new Surface();
			surf->raw = raw;
			if (col.a < 255) {
				surf->setAlpha(col.a);
			}
			if (textCache.size() >= MAX_TEXT_CACHE && !textLru.empty()) {
				string oldKey = textLru.back();
				textLru.pop_back();
				auto oit = textCache.find(oldKey);
				if (oit != textCache.end()) {
					delete oit->second.surf;
					textCache.erase(oit);
				}
			}
			textLru.push_front(key);
			textCache[key] = { surf, textLru.begin() };
		}
		if (surf && surf->raw) {
			surf->blit(surface, dx, dy);
		}
	};

	if (bgColor.a > 0) {
		ensureOutlineFont();
		renderCached(fontOutline, 'o', bgColor, x - outline, y - outline);
	}

	if (fgColor.a > 0) {
		renderCached(font, 'f', fgColor, x, y);
	}
}
