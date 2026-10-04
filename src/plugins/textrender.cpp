#include "ncbind/ncbind.hpp"
#define NCB_MODULE_NAME TJS_W("textrender.dll")

namespace {
const tjs_char TextRenderSource[] =
#include "TextRenderScript.inc"
;
void RegisterTextRender() { TVPExecuteScript(TextRenderSource); }
}
NCB_PRE_REGIST_CALLBACK(RegisterTextRender);
