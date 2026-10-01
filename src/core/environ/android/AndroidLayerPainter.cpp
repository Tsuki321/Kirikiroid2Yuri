#include "AndroidLayerPainter.h"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "RenderManager.h"
#include "MsgIntf.h"
#include "platform/android/jni/JniHelper.h"
#include <algorithm>

namespace {
class Call {
public:
    cocos2d::JniMethodInfo method;
    Call(const char *name, const char *signature) {
        if (!cocos2d::JniHelper::getStaticMethodInfo(method, "org/tvp/kirikiri2/LayerPainter", name, signature))
            TVPThrowExceptionMessage(TJS_W("layerExDraw: Android drawing service is unavailable"));
        if (method.env->PushLocalFrame(24) < 0) check();
    }
    ~Call() { method.env->PopLocalFrame(nullptr); method.env->DeleteLocalRef(method.classID); }
    void check() {
        if (!method.env->ExceptionCheck()) return;
        method.env->ExceptionDescribe(); method.env->ExceptionClear();
        TVPThrowExceptionMessage(TJS_W("layerExDraw: drawing failed; see the Android log for the invalid argument"));
    }
    jstring string(const ttstr &value) {
        jstring result = method.env->NewString(reinterpret_cast<const jchar *>(value.c_str()), value.length());
        check(); return result;
    }
    jfloatArray floats(const float *data, size_t size) {
        jfloatArray result = method.env->NewFloatArray(size); check();
        if (size) method.env->SetFloatArrayRegion(result, 0, size, data);
        check(); return result;
    }
    jintArray integers(const std::vector<tjs_uint32> &values) {
        jintArray result = method.env->NewIntArray(values.size()); check();
        if (!values.empty()) method.env->SetIntArrayRegion(result, 0, values.size(), reinterpret_cast<const jint *>(values.data()));
        check(); return result;
    }
};
}

tTVPRect TVPPaintLayer(tTJSNI_Layer *layer, const TVPLayerPaintRequest &request) {
    if (!layer || !layer->GetMainImage()) TVPThrowExceptionMessage(TJS_W("layerExDraw: layer has no image"));
    Call call("render", "([F[F[I[F[FLjava/lang/String;FILjava/lang/String;Z[III)[I");
    float clip[] = {float(std::max(0, layer->GetClipLeft())), float(std::max(0, layer->GetClipTop())),
        float(std::min<int>(layer->GetImageWidth(), layer->GetClipLeft()+layer->GetClipWidth())),
        float(std::min<int>(layer->GetImageHeight(), layer->GetClipTop()+layer->GetClipHeight()))};
    jintArray output = static_cast<jintArray>(call.method.env->CallStaticObjectMethod(call.method.classID, call.method.methodID,
        call.floats(request.commands.data(), request.commands.size()), call.floats(request.matrix.data(), 6),
        call.integers(request.colors), call.floats(request.styles.data(), request.styles.size()), call.floats(clip, 4),
        call.string(request.family), request.fontSize, request.fontStyle,
        request.drawText ? call.string(request.text) : nullptr, request.antialias ? JNI_TRUE : JNI_FALSE,
        request.image ? call.integers(*request.image) : nullptr, request.imageWidth, request.imageHeight));
    call.check();
    if (!output || call.method.env->GetArrayLength(output) < 4) TVPThrowExceptionMessage(TJS_W("layerExDraw: invalid bitmap result"));
    jint rectangle[4]; call.method.env->GetIntArrayRegion(output, 0, 4, rectangle); call.check();
    int x=rectangle[0], y=rectangle[1], width=rectangle[2], height=rectangle[3];
    if (width == 0 || height == 0) return tTVPRect(0, 0, 0, 0);
    size_t size = size_t(width) * size_t(height);
    if (width < 0 || height < 0 || size > 16777216 || size + 4 != size_t(call.method.env->GetArrayLength(output)))
        TVPThrowExceptionMessage(TJS_W("layerExDraw: invalid bitmap dimensions"));
    std::vector<tjs_uint32> pixels(size);
    call.method.env->GetIntArrayRegion(output, 4, size, reinterpret_cast<jint *>(pixels.data())); call.check();
    for (tjs_uint32 &pixel : pixels) pixel = TVP_REVRGB(pixel);
    tTVPBaseTexture patch(width, height);
    patch.Update(pixels.data(), width * 4, 0, 0, width, height);
    // Upload only the affected patch, including when the destination is an OpenGL texture.
    tTVPBaseTexture *destination = const_cast<tTVPBaseTexture *>(layer->GetMainImage());
    destination->Blt(x, y, &patch, tTVPRect(0, 0, width, height),
        layer->GetType() == ltAddAlpha ? bmAlphaOnAddAlpha : bmAlphaOnAlpha, 255, false);
    layer->SetImageModified(true);
    tTVPRect result(x, y, x+width, y+height);
    layer->Update(result);
    return result;
}

std::array<float, 7> TVPMeasureAndroidText(const ttstr &family, float size, int style, const ttstr &text) {
    Call call("metrics", "(Ljava/lang/String;FILjava/lang/String;)[F");
    jfloatArray output = static_cast<jfloatArray>(call.method.env->CallStaticObjectMethod(call.method.classID, call.method.methodID,
        call.string(family), size, style, call.string(text)));
    call.check();
    if (!output || call.method.env->GetArrayLength(output) != 7) TVPThrowExceptionMessage(TJS_W("layerExDraw: invalid font metrics"));
    std::array<float, 7> result;
    call.method.env->GetFloatArrayRegion(output, 0, 7, result.data()); call.check(); return result;
}

std::vector<tjs_uint8> TVPEncodeAndroidImage(const std::vector<tjs_uint32> &pixels, int width, int height, const ttstr &mime, int quality) {
    Call call("encode", "([IIILjava/lang/String;I)[B");
    jbyteArray output = static_cast<jbyteArray>(call.method.env->CallStaticObjectMethod(call.method.classID, call.method.methodID,
        call.integers(pixels), width, height, call.string(mime), quality));
    call.check();
    if (!output) TVPThrowExceptionMessage(TJS_W("layerExDraw: image encoding failed"));
    std::vector<tjs_uint8> result(call.method.env->GetArrayLength(output));
    if (!result.empty()) call.method.env->GetByteArrayRegion(output, 0, result.size(), reinterpret_cast<jbyte *>(result.data()));
    call.check(); return result;
}

void TVPAddAndroidFont(const ttstr &file, const ttstr &names) {
    Call call("addFont", "(Ljava/lang/String;Ljava/lang/String;)V");
    call.method.env->CallStaticVoidMethod(call.method.classID, call.method.methodID, call.string(file), call.string(names));
    call.check();
}
