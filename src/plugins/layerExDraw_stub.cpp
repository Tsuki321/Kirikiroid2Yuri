#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")

NCB_REGISTER_CLASS(GdiPlus) {
	Variant(TJS_W("Ok"), (int)0);
}

// Stub: silently no-op instead of throwing. Games calling these for
// non-critical decoration now run instead of crashing; text/effects are
// simply missing. The real implementation lives in layerExDraw_cocos2d.cpp
// (TODO: see docs/ROADMAP.md phase 1).
struct LayerExDrawStub {
	static tjs_error TJS_INTF_METHOD noop(tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		if (result) result->Clear();
		return TJS_S_OK;
	}
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerExDrawStub, Layer) {
	RawCallback(TJS_W("drawString"), &LayerExDrawStub::noop, 0);
}