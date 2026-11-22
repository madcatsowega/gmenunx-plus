#ifndef FONTHELPER_H
#define FONTHELPER_H

#include "surface.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <list>
#include <SDL_ttf.h>

#ifdef _WIN32
    typedef unsigned int uint32_t;
#endif
using std::vector;
using std::string;

struct FontCacheEntry {
	Surface *surf;
	std::list<string>::iterator lruIt;
};

class FontHelper {
private:
	int fontHeight, fontSize;
	RGBAColor textColor, outlineColor;
	string fontName;
	std::unordered_map<string, FontCacheEntry> textCache;
	std::list<string> textLru;
	void clearCache();
	void ensureOutlineFont();

public:
	FontHelper(const string &fontName, int size, RGBAColor textColor = (RGBAColor){255,255,255}, RGBAColor outlineColor = (RGBAColor){5,5,5});
	~FontHelper();

	const string &getFontName() const { return fontName; }
	int getFontSize() const { return fontSize; }

	bool utf8Code(uint8_t c);

	void write(Surface *surface, const string &text, int x, int y, RGBAColor fgColor, RGBAColor bgColor);
	void write(Surface *surface, const string &text, int x, int y, const uint8_t align = HAlignLeft | VAlignTop);
	void write(Surface *surface, const string &text, int x, int y, const uint8_t align, RGBAColor fgColor, RGBAColor bgColor);
	void write(Surface *surface, const string &text, SDL_Rect &wrapRect, const uint8_t align = HAlignLeft | VAlignTop);

	void write(Surface *surface, vector<string> *text, int x, int y, const uint8_t align, RGBAColor fgColor, RGBAColor bgColor);
	void write(Surface *surface, vector<string> *text, int x, int y, const uint8_t align = HAlignLeft | VAlignTop);

	uint32_t getLineWidth(const string &text);
	uint32_t getTextWidth(const string &text);
	int getTextHeight(const string &text);
	uint32_t getTextWidth(vector<string> *text);

	uint32_t height() { return fontHeight; };

	void loadFont(const string &fontName, int fontSize);

	FontHelper *setSize(const int size);
	FontHelper *setColor(RGBAColor color);
	FontHelper *setOutlineColor(RGBAColor color);
	TTF_Font *font, *fontOutline;
};

#endif /* FONTHELPER_H */
