#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("windowEx.dll")

struct WindowExStub {
	static tjs_error TJS_INTF_METHOD notAvailable(tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		if (result) *result = (tjs_int)0;
		return TJS_S_OK;
	}
};

NCB_ATTACH_CLASS_WITH_HOOK(WindowExStub, Window) {
	RawCallback(TJS_W("minimize"), &WindowExStub::notAvailable, 0);
	RawCallback(TJS_W("maximize"), &WindowExStub::notAvailable, 0);
}