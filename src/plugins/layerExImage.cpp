#include "ncbind/ncbind.hpp"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "ImageEffects.h"

#define NCB_MODULE_NAME TJS_W("layerExImage.dll")

class LayerImageEffects {
    tTJSNI_Layer *layer;
    uint32_t randomState = 0x78a3bc51;
    template<class Operation> void apply(Operation operation) {
        int width = layer->GetImageWidth(), height = layer->GetImageHeight();
        int left = ImageEffects::clamp(layer->GetClipLeft(), 0, width);
        int top = ImageEffects::clamp(layer->GetClipTop(), 0, height);
        int right = int(std::max<int64_t>(left, std::min<int64_t>(width, int64_t(layer->GetClipLeft()) + layer->GetClipWidth())));
        int bottom = int(std::max<int64_t>(top, std::min<int64_t>(height, int64_t(layer->GetClipTop()) + layer->GetClipHeight())));
        width = right-left; height = bottom-top;
        if(!width || !height) return;
        if(size_t(width)*height > 16777216)
            TVPThrowExceptionMessage(TJS_W("Image effect exceeds the pixel limit"));
        std::vector<uint32_t> pixels(size_t(width)*height);
        auto bitmap = layer->GetMainImage();
        for(int y = 0; y < height; ++y) {
            auto row = static_cast<const tjs_uint32 *>(bitmap->GetScanLine(top+y));
            for(int x = 0; x < width; ++x) pixels[size_t(y)*width+x] = TVP_REVRGB(row[left+x]);
        }
        try { operation(pixels, width, height); }
        catch(const std::invalid_argument &error) { TVPThrowExceptionMessage(ttstr(error.what())); }
        for(auto &pixel : pixels) pixel = TVP_REVRGB(pixel);
        const_cast<tTVPBaseTexture *>(bitmap)->Update(pixels.data(), width*4, left, top, width, height);
        layer->SetImageModified(true);
        layer->Update(tTVPRect(left, top, right, bottom));
    }
public:
    explicit LayerImageEffects(iTJSDispatch2 *object) : layer(nullptr) {
        if(TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_Layer::ClassID,
                                                   reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer)
            TVPThrowExceptionMessage(TJS_W("Image effects require a Layer"));
    }
    void light(int brightness, int contrast) {
        apply([&](std::vector<uint32_t> &p, int, int) { ImageEffects::light(p, brightness, contrast); });
    }
    void colorize(int hue, int saturation, double blend) {
        apply([&](std::vector<uint32_t> &p, int, int) { ImageEffects::colorize(p, hue, saturation, blend); });
    }
    void modulate(int hue, int saturation, int luminance) {
        apply([&](std::vector<uint32_t> &p, int, int) { ImageEffects::modulate(p, hue, saturation, luminance); });
    }
    void noise(int level) {
        apply([&](std::vector<uint32_t> &p, int, int) { ImageEffects::noise(p, level, randomState); });
    }
    void generateWhiteNoise() {
        apply([&](std::vector<uint32_t> &p, int, int) { ImageEffects::noise(p, 255, randomState, true); });
    }
    void gaussianBlur(double radius) {
        apply([&](std::vector<uint32_t> &p, int w, int h) { ImageEffects::gaussianBlur(p, w, h, radius); });
    }
};

NCB_GET_INSTANCE_HOOK(LayerImageEffects) {
    NCB_INSTANCE_GETTER(object) {
        ClassT *value = GetNativeInstance(object);
        if(!value) { value = new ClassT(object); SetNativeInstance(object, value); }
        return value;
    }
};
NCB_ATTACH_CLASS_WITH_HOOK(LayerImageEffects, Layer) {
    NCB_METHOD(light);
    NCB_METHOD(colorize);
    NCB_METHOD(modulate);
    NCB_METHOD(noise);
    NCB_METHOD(generateWhiteNoise);
    NCB_METHOD(gaussianBlur);
}
