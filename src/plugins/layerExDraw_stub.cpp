#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("layerExDraw.dll")

NCB_REGISTER_CLASS(GdiPlus) {
	Variant(TJS_W("Ok"), (int)0);
}

struct LayerExDrawStub {
	static tjs_error TJS_INTF_METHOD drawString(tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		TVPThrowExceptionMessage(TJS_W("layerExDraw (GdiPlus) is not available on this platform."));
		if (result) result->Clear();
		return TJS_E_FAIL;
	}
};

NCB_ATTACH_CLASS_WITH_HOOK(LayerExDrawStub, Layer) {
	RawCallback(TJS_W("drawString"), &LayerExDrawStub::drawString, 0);
}