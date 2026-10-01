#include "ncbind/ncbind.hpp"
#include "WindowIntf.h"
#include "WindowImpl.h"
#define NCB_MODULE_NAME TJS_W("windowEx.dll")

struct WindowEx {
    template<int Action>
    static tjs_error TJS_INTF_METHOD change(tTJSVariant *, tjs_int, tTJSVariant **, iTJSDispatch2 *object) {
        tTJSNI_Window *window = nullptr;
        if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
                tTJSNC_Window::ClassID, reinterpret_cast<iTJSNativeInstance **>(&window))) || !window)
            return TJS_E_NATIVECLASSCRASH;
        if (Action == 0) window->SetVisible(false);
        else {
            window->SetFullScreen(Action == 1);
            window->SetVisible(true);
            window->BringToFront();
        }
        return TJS_S_OK;
    }
};
NCB_ATTACH_CLASS_WITH_HOOK(WindowEx, Window) {
    RawCallback(TJS_W("minimize"), &WindowEx::change<0>, 0);
    RawCallback(TJS_W("maximize"), &WindowEx::change<1>, 0);
    RawCallback(TJS_W("restore"), &WindowEx::change<2>, 0);
}
