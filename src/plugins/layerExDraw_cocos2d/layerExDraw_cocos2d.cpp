#include "ncbind/ncbind.hpp"
#include "LayerExDraw.hpp"
#include "LayerImpl.h"
#include "tjsNative.h"
#include "RenderManager.h"
#include "CharacterSet.h"

#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")

//---------------------------------------------------------------------------

NCB_REGISTER_CLASS(GdiPlus) {
	Variant(TJS_W("Ok"), (int)0);
	Variant(TJS_W("GenericError"), (int)1);
	Variant(TJS_W("InvalidParameter"), (int)2);
	Variant(TJS_W("OutOfMemory"), (int)3);
	Variant(TJS_W("ObjectBusy"), (int)4);
	Variant(TJS_W("InsufficientBuffer"), (int)5);
	Variant(TJS_W("NotImplemented"), (int)6);
	Variant(TJS_W("Win32Error"), (int)7);
	Variant(TJS_W("WrongState"), (int)8);
	Variant(TJS_W("Aborted"), (int)9);
	Variant(TJS_W("FileNotFound"), (int)10);
	Variant(TJS_W("ValueOverflow"), (int)11);
	Variant(TJS_W("AccessDenied"), (int)12);
	Variant(TJS_W("UnknownImageFormat"), (int)13);
	Variant(TJS_W("FontFamilyNotFound"), (int)14);
	Variant(TJS_W("FontStyleNotFound"), (int)15);
	Variant(TJS_W("NotTrueTypeFont"), (int)16);

	Variant(TJS_W("FontStyleRegular"), (int)0);
	Variant(TJS_W("FontStyleBold"), (int)1);
	Variant(TJS_W("FontStyleItalic"), (int)2);
	Variant(TJS_W("FontStyleBoldItalic"), (int)3);
	Variant(TJS_W("FontStyleUnderline"), (int)4);
	Variant(TJS_W("FontStyleStrikeout"), (int)8);

	Variant(TJS_W("BrushTypeSolidColor"), (int)0);
	Variant(TJS_W("BrushTypeHatchFill"), (int)1);
	Variant(TJS_W("BrushTypeTextureFill"), (int)2);
	Variant(TJS_W("BrushTypePathGradient"), (int)3);
	Variant(TJS_W("BrushTypeLinearGradient"), (int)4);

	Variant(TJS_W("DashStyleSolid"), (int)0);
	Variant(TJS_W("DashStyleDash"), (int)1);
	Variant(TJS_W("DashStyleDot"), (int)2);
	Variant(TJS_W("DashStyleDashDot"), (int)3);
	Variant(TJS_W("DashStyleDashDotDot"), (int)4);

	Variant(TJS_W("LineCapFlat"), (int)0);
	Variant(TJS_W("LineCapRound"), (int)2);
	Variant(TJS_W("LineCapTriangle"), (int)3);

	Variant(TJS_W("LineJoinMiter"), (int)0);
	Variant(TJS_W("LineJoinBevel"), (int)1);
	Variant(TJS_W("LineJoinRound"), (int)2);

	Variant(TJS_W("WrapModeTile"), (int)0);
	Variant(TJS_W("WrapModeClamp"), (int)4);

	Variant(TJS_W("SmoothingModeDefault"), (int)0);
	Variant(TJS_W("SmoothingModeHighSpeed"), (int)1);
	Variant(TJS_W("SmoothingModeHighQuality"), (int)2);
	Variant(TJS_W("SmoothingModeAntiAlias"), (int)1);

	Variant(TJS_W("TextRenderingHintSystemDefault"), (int)0);
	Variant(TJS_W("TextRenderingHintAntiAlias"), (int)4);
	Variant(TJS_W("TextRenderingHintClearTypeGridFit"), (int)5);
}

//---------------------------------------------------------------------------

void FontInfo::refreshMetrics() {
}

NCB_REGISTER_CLASS(Font) {
	NCB_CONSTRUCTOR(ttstr, tjs_real, tjs_int);
	NCB_PROPERTY(familyName, familyName, familyName);
	NCB_PROPERTY(emSize, emSize, emSize);
	NCB_PROPERTY(style, style, style);
	NCB_PROPERTY(forceSelfPathDraw, forceSelfPathDraw, forceSelfPathDraw);
	NCB_PROPERTY_RO(ascent, ascent);
	NCB_PROPERTY_RO(descent, descent);
	NCB_PROPERTY_RO(lineSpacing, lineSpacing);
}

//---------------------------------------------------------------------------

void Appearance::addBrush(tjs_uint32 color_, tjs_real ox_, tjs_real oy_) {
	DrawInfo d;
	d.type = 1; d.color = color_; d.ox = ox_; d.oy = oy_;
	items.push_back(d);
	firstColor = color_;
}

void Appearance::addPen(tjs_uint32 color_, tjs_real width_, tjs_real ox_, tjs_real oy_) {
	DrawInfo d;
	d.type = 0; d.color = color_; d.width = width_; d.ox = ox_; d.oy = oy_;
	items.push_back(d);
	firstColor = color_;
}

NCB_REGISTER_CLASS(Appearance) {
	NCB_CONSTRUCTOR();
	NCB_METHOD(clear);
	NCB_METHOD(addBrush);
	NCB_METHOD(addPen);
	NCB_PROPERTY_RO(firstColor, firstColor);
}

//---------------------------------------------------------------------------

void PathData::drawLine(tjs_real x1, tjs_real y1, tjs_real x2, tjs_real y2) {
	points.push_back(x1); points.push_back(y1);
	points.push_back(x2); points.push_back(y2);
}

void PathData::drawRectangle(tjs_real x, tjs_real y, tjs_real w, tjs_real h) {
	points.push_back(x);     points.push_back(y);
	points.push_back(x + w); points.push_back(y);
	points.push_back(x + w); points.push_back(y + h);
	points.push_back(x);     points.push_back(y + h);
}

void PathData::drawEllipse(tjs_real x, tjs_real y, tjs_real w, tjs_real h) {
	points.push_back(x); points.push_back(y);
	points.push_back(w); points.push_back(h);
}

void PathData::drawPolygon(const std::vector<tjs_real> &pts) {
	points = pts;
}

NCB_REGISTER_CLASS(Path) {
	NCB_CONSTRUCTOR();
	NCB_METHOD(startFigure);
	NCB_METHOD(closeFigure);
	NCB_METHOD(drawLine);
	NCB_METHOD(drawRectangle);
	NCB_METHOD(drawEllipse);
	NCB_METHOD(drawPolygon);
}

//---------------------------------------------------------------------------

static void assignNestedClasses() {
	tTJSVariant r;
	TVPExecuteExpression(
		TJS_W("GdiPlus.Font = Font; "
		      "GdiPlus.Appearance = Appearance; "
		      "GdiPlus.Path = Path;"),
		&r);
}

NCB_POST_REGIST_CALLBACK(assignNestedClasses);

//---------------------------------------------------------------------------

struct LayerExDrawImpl {

	static tTJSNI_Layer * getLayer(iTJSDispatch2 *objthis) {
		if (!objthis) return nullptr;
		tTJSNI_Layer *layer = nullptr;
		objthis->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
			tTJSNC_Layer::ClassID, (iTJSNativeInstance**)&layer);
		return layer;
	}

	static ttstr getStrProp(iTJSDispatch2 *obj, const tjs_char *name) {
		tTJSVariant v;
		obj->PropGet(TJS_IGNOREPROP, name, nullptr, &v, obj);
		return ttstr(v);
	}

	static tjs_real getRealProp(iTJSDispatch2 *obj, const tjs_char *name) {
		tTJSVariant v;
		obj->PropGet(TJS_IGNOREPROP, name, nullptr, &v, obj);
		return v.AsReal();
	}

	static tjs_uint32 getColorProp(iTJSDispatch2 *obj, const tjs_char *name) {
		tTJSVariant v;
		obj->PropGet(TJS_IGNOREPROP, name, nullptr, &v, obj);
		return (tjs_uint32)(tTVInteger)v;
	}

	static tjs_error TJS_INTF_METHOD drawString(tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 5) return TJS_E_BADPARAMCOUNT;

		tTJSNI_Layer *layer = getLayer(objthis);
		if (!layer) return TJS_E_NATIVECLASSCRASH;

		iTJSDispatch2 *fontObj = param[0]->AsObjectNoAddRef();
		iTJSDispatch2 *appObj  = param[1]->AsObjectNoAddRef();
		tjs_real x = param[2]->AsReal();
		tjs_real y = param[3]->AsReal();
		ttstr text = *param[4];

		ttstr familyName = getStrProp(fontObj, TJS_W("familyName"));
		tjs_real emSize  = getRealProp(fontObj, TJS_W("emSize"));
		tjs_uint32 color = appObj ? getColorProp(appObj, TJS_W("firstColor")) : 0xFF000000;

		unsigned char *buf = (unsigned char*)layer->GetMainImagePixelBufferForWrite();
		tjs_int pitch = layer->GetMainImagePixelBufferPitch();
		tjs_int w = layer->GetWidth();
		tjs_int h = layer->GetHeight();

		if (!buf || !pitch || !text.length()) {
			if (result) result->Clear();
			return TJS_S_OK;
		}

		layer->Update(tTVPRect((tjs_int)x, (tjs_int)y,
		                       (tjs_int)(x + text.length() * emSize),
		                       (tjs_int)(y + emSize + 2)));
		if (result) result->Clear();
		return TJS_S_OK;
	}

	static tjs_error TJS_INTF_METHOD measureString(tTJSVariant *result,
		tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *objthis) {
		if (numparams < 2) return TJS_E_BADPARAMCOUNT;

		iTJSDispatch2 *fontObj = param[0]->AsObjectNoAddRef();
		ttstr text = *param[1];
		ttstr familyName = getStrProp(fontObj, TJS_W("familyName"));
		tjs_real emSize  = getRealProp(fontObj, TJS_W("emSize"));

		tjs_int totalW = (tjs_int)(text.length() * emSize * 0.6);
		tjs_int totalH = (tjs_int)(emSize * 1.2);

		if (result) {
			iTJSDispatch2 *dict = TJSCreateDictionaryObject();
			tTJSVariant wv(totalW), hv(totalH), xv(0), yv(0);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("x"), nullptr, &xv, dict);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("y"), nullptr, &yv, dict);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &wv, dict);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &hv, dict);
			*result = tTJSVariant(dict, dict);
			dict->Release();
		}
		return TJS_S_OK;
	}

	static tjs_error TJS_INTF_METHOD noop(tTJSVariant *result,
		tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		if (result) result->Clear();
		return TJS_S_OK;
	}

	static tjs_error TJS_INTF_METHOD noopRect(tTJSVariant *result,
		tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		if (result) {
			iTJSDispatch2 *dict = TJSCreateDictionaryObject();
			tTJSVariant zv(0);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("x"), nullptr, &zv, dict);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("y"), nullptr, &zv, dict);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("width"), nullptr, &zv, dict);
			dict->PropSet(TJS_MEMBERENSURE, TJS_W("height"), nullptr, &zv, dict);
			*result = tTJSVariant(dict, dict);
			dict->Release();
		}
		return TJS_S_OK;
	}
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerExDrawImpl, Layer) {
	RawCallback(TJS_W("drawString"), &LayerExDrawImpl::drawString, 0);
	RawCallback(TJS_W("measureString"), &LayerExDrawImpl::measureString, 0);
	RawCallback(TJS_W("measureStringInternal"), &LayerExDrawImpl::measureString, 0);
	RawCallback(TJS_W("drawPathString"), &LayerExDrawImpl::drawString, 0);

	RawCallback(TJS_W("drawLine"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawLines"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawRectangle"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawRectangles"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawEllipse"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawPolygon"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawCurve"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawClosedCurve"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawBezier"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawBeziers"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawArc"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawPie"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawPath"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawImage"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawImageRect"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawImageStretch"), &LayerExDrawImpl::noopRect, 0);
	RawCallback(TJS_W("drawImageAffine"), &LayerExDrawImpl::noopRect, 0);

	RawCallback(TJS_W("clear"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("setTransform"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("resetTransform"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("rotateTransform"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("scaleTransform"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("translateTransform"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("saveImage"), &LayerExDrawImpl::noop, 0);
	RawCallback(TJS_W("getColorRegionRects"), &LayerExDrawImpl::noop, 0);
}
