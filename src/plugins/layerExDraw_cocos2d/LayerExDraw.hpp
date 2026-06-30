#ifndef LAYEREXDRAW_COCOS2D_HPP
#define LAYEREXDRAW_COCOS2D_HPP

#include "ncbind/ncbind.hpp"

#include <string>
#include <vector>

struct FontInfo {
	ttstr familyName;
	tjs_real emSize;
	tjs_int style;
	bool forceSelfPathDraw;

	tjs_real ascent;
	tjs_real descent;
	tjs_real ascentLeading;
	tjs_real descentLeading;
	tjs_real lineSpacing;

	FontInfo(ttstr family, tjs_real size, tjs_int style_)
		: familyName(family), emSize(size), style(style_), forceSelfPathDraw(false)
		, ascent(0), descent(0), ascentLeading(0), descentLeading(0), lineSpacing(0)
	{
		refreshMetrics();
	}

	void refreshMetrics();

	ttstr getFamilyName() const { return familyName; }
	void setFamilyName(const ttstr &v) { familyName = v; }
	tjs_real getEmSize() const { return emSize; }
	void setEmSize(tjs_real v) { emSize = v; }
	tjs_int getStyle() const { return style; }
	void setStyle(tjs_int v) { style = v; }
	bool getForceSelfPathDraw() const { return forceSelfPathDraw; }
	void setForceSelfPathDraw(bool v) { forceSelfPathDraw = v; }
	tjs_real getAscent() const { return ascent; }
	tjs_real getDescent() const { return descent; }
	tjs_real getLineSpacing() const { return lineSpacing; }
};

struct DrawInfo {
	tjs_int type;
	tjs_uint32 color;
	tjs_real width;
	tjs_real ox;
	tjs_real oy;

	DrawInfo() : type(0), color(0xFF000000), width(1.0f), ox(0), oy(0) {}
};

struct Appearance {
	std::vector<DrawInfo> items;
	tjs_uint32 firstColor;

	Appearance() : firstColor(0xFF000000) {}

	void clear() { items.clear(); firstColor = 0xFF000000; }
	void addBrush(tjs_uint32 color, tjs_real ox, tjs_real oy);
	void addPen(tjs_uint32 color, tjs_real width_, tjs_real ox, tjs_real oy);
	tjs_uint32 getFirstColor() const { return firstColor; }
};

struct PathData {
	std::vector<float> points;
	bool open;

	PathData() : open(false) {}
	void startFigure() { open = true; points.clear(); }
	void closeFigure() { open = false; }
	void drawLine(tjs_real x1, tjs_real y1, tjs_real x2, tjs_real y2);
	void drawRectangle(tjs_real x, tjs_real y, tjs_real w, tjs_real h);
	void drawEllipse(tjs_real x, tjs_real y, tjs_real w, tjs_real h);
	void drawPolygon(const std::vector<tjs_real> &pts);
};

#endif
