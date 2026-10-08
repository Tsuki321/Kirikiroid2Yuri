#include "ncbind/ncbind.hpp"

#define NCB_MODULE_NAME TJS_W("wuvorbis.dll")

// VorbisWaveDecoder is already part of the engine. Legacy games still link
// wuvorbis.dll before playing Ogg audio; no Windows module is needed.
static void RegisterBuiltinVorbis() {}
NCB_PRE_REGIST_CALLBACK(RegisterBuiltinVorbis);
