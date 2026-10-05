
#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("win32dialog.dll")

static void InitPlugin_WIN32Dialog()
{
	TVPExecuteScript(
		TJS_W(R"K2DIALOG(
class WIN32Dialog {
    function messageBox(owner, message = void, caption = void, flags = void) {
        if (flags === void && typeof owner == "String") {
            flags = caption; caption = message; message = owner;
        }
        if (caption === void) caption = System.title;
        if (flags === void) flags = 0;
        var types = [ ["OK"], ["OK", "Cancel"], ["Abort", "Retry", "Ignore"],
                      ["Yes", "No", "Cancel"], ["Yes", "No"], ["Retry", "Cancel"],
                      ["Cancel", "Try Again", "Continue"] ];
        var values = [ [1], [1, 2], [3, 4, 5], [6, 7, 2], [6, 7], [4, 2], [2, 10, 11] ];
        var type = int flags & 15;
        if (type > 6) throw "WIN32Dialog: unsupported message box type";
        var selected = System.inform(message, caption, types[type]);
        if (selected < 0 || selected >= values[type].count) return type == 4 ? IDNO : IDCANCEL;
        return values[type][selected];
    }
}
with (WIN32Dialog) {
    .MB_OK = 0; .MB_OKCANCEL = 1; .MB_ABORTRETRYIGNORE = 2;
    .MB_YESNOCANCEL = 3; .MB_YESNO = 4; .MB_RETRYCANCEL = 5; .MB_CANCELTRYCONTINUE = 6;
    .MB_ICONERROR = 16; .MB_ICONHAND = 16; .MB_ICONSTOP = 16;
    .MB_ICONQUESTION = 32; .MB_ICONWARNING = 48; .MB_ICONEXCLAMATION = 48;
    .MB_ICONINFORMATION = 64; .MB_ICONASTERISK = 64;
    .MB_DEFBUTTON1 = 0; .MB_DEFBUTTON2 = 256; .MB_DEFBUTTON3 = 512; .MB_DEFBUTTON4 = 768;
    .MB_APPLMODAL = 0; .MB_SYSTEMMODAL = 4096; .MB_TASKMODAL = 8192; .MB_TOPMOST = 262144;
    .IDOK = 1; .IDCANCEL = 2; .IDABORT = 3; .IDRETRY = 4; .IDIGNORE = 5;
    .IDYES = 6; .IDNO = 7; .IDTRYAGAIN = 10; .IDCONTINUE = 11;
}
)K2DIALOG")
		);
}

NCB_PRE_REGIST_CALLBACK(InitPlugin_WIN32Dialog);
