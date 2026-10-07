#include "ncbind/ncbind.hpp"
#include "TransIntf.h"
#include "RenderManager.h"
#include "LayerBitmapIntf.h"
#include "LayerImpl.h"
#include "Random.h"
#include "NaganoTransitions.h"
#include <memory>

#define NCB_MODULE_NAME TJS_W("extNagano.dll")

namespace {

struct ReleaseTexture {
    void operator()(iTVPTexture2D *texture) const { if (texture) texture->Release(); }
};
using Texture = std::unique_ptr<iTVPTexture2D, ReleaseTexture>;
struct ReleaseScanLine {
    void operator()(iTVPScanLineProvider *provider) const { if (provider) provider->Release(); }
};

void snapshot(iTVPTexture2D *input, Texture &texture, int width, int height) {
    if (!input || input->GetWidth() < unsigned(width) || input->GetHeight() < unsigned(height))
        throw std::invalid_argument("extNagano source is smaller than the transition image");
    auto manager = TVPGetRenderManager();
    if (!texture) texture.reset(manager->CreateTexture2D(nullptr, 0, width, height,
        TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS));
    if (!texture) throw std::runtime_error("cannot allocate extNagano image snapshot");
    const tTVPRect rect(0, 0, width, height);
    const tRenderTexRectArray::Element sources[] = { tRenderTexRectArray::Element(input, rect) };
    // Normalise scaled, RGB and compressed textures to an owned RGBA snapshot.
    // Reading the source texture directly is unsafe on the GL renderer.
    manager->OperateRect(manager->GetRenderMethod("Copy"), texture.get(), nullptr,
                         rect, tRenderTexRectArray(sources));
}

struct RuleImage {
    int width = 0, height = 0;
    std::vector<uint32_t> pixels;
};

RuleImage loadRule(iTVPSimpleOptionProvider *options, iTVPSimpleImageProvider *images,
                   NaganoTransitions::Effect effect, unsigned width, unsigned height) {
    tTJSVariant value;
    if (TJS_FAILED(options->GetValue(TJS_W("rule"), &value)) || value.Type() == tvtVoid)
        throw std::invalid_argument("extNagano requires a rule image path or Layer");
    std::unique_ptr<iTVPScanLineProvider, ReleaseScanLine> provider;
    iTVPTexture2D *input = nullptr;
    if (value.Type() == tvtObject) {
        auto object = value.AsObjectNoAddRef();
        tTJSNI_Layer *layer = nullptr;
        if (!object || TJS_FAILED(object->NativeInstanceSupport(TJS_NIS_GETINSTANCE,
            tTJSNC_Layer::ClassID, reinterpret_cast<iTJSNativeInstance **>(&layer))) || !layer || !layer->GetMainImage())
            throw std::invalid_argument("extNagano rule object must be a Layer with an image");
        input = layer->GetMainImage()->GetTexture();
    } else {
        if (!images) throw std::invalid_argument("extNagano has no image storage provider");
        const tjs_char *path = nullptr;
        if (TJS_FAILED(options->GetAsString(TJS_W("rule"), &path)) || !path || !*path)
            throw std::invalid_argument("extNagano rule path is empty");
        const ttstr storage(path);
        iTVPScanLineProvider *loaded = nullptr;
        // Keep imagewipe's natural strip width. The engine's loader repeats
        // undersized file rules to the requested size, including SAF storage.
        const tjs_error status = images->LoadImage(storage.c_str(), 32, 0x02ffffff,
            effect == NaganoTransitions::Effect::Universal3D ? width : 0, height, &loaded);
        provider.reset(loaded);
        if (TJS_FAILED(status) || !provider) throw std::invalid_argument("cannot load extNagano rule image");
        input = provider->GetTexture();
    }
    if (!input || !input->GetWidth() || !input->GetHeight() || input->GetWidth() > 16384 ||
        input->GetHeight() > 16384 || size_t(input->GetWidth()) * input->GetHeight() > ExtraTransitions::MaxPixels)
        throw std::invalid_argument("extNagano rule image exceeds the pixel limit");
    RuleImage result;
    result.width = int(input->GetWidth()); result.height = int(input->GetHeight());
    Texture texture;
    snapshot(input, texture, result.width, result.height);
    result.pixels.resize(size_t(result.width) * result.height);
    for (int y = 0; y < result.height; ++y) {
        const void *line = texture->GetScanLineForRead(y);
        if (!line) throw std::runtime_error("cannot read extNagano rule pixels");
        std::memcpy(result.pixels.data() + size_t(y) * result.width, line, size_t(result.width) * 4);
    }
    return result;
}

class NaganoTransition : public iTVPDivisibleTransHandler {
    unsigned refs = 1;
    int width, height;
    uint64_t duration, start = 0, elapsed = 0;
    bool first = true, prepared = false;
    NaganoTransitions::Transition transition;
    Texture source1, source2, patch;
    std::vector<uint32_t> pixels;
public:
    NaganoTransition(NaganoTransitions::Options options, int w, int h, RuleImage rule)
        : width(w), height(h), duration(options.time),
          transition(std::move(options), w, h, std::move(rule.pixels), rule.width, rule.height) {}
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if (!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *) override { return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick) override {
        if (first) { first = false; start = tick; }
        elapsed = tick >= start ? std::min<uint64_t>(duration, tick - start) : 0;
        transition.setTime(elapsed); prepared = false;
        return TJS_S_TRUE;
    }
    tjs_error TJS_INTF_METHOD EndProcess() override { return elapsed == duration ? TJS_S_FALSE : TJS_S_TRUE; }
    tjs_error TJS_INTF_METHOD MakeFinalImage(iTVPScanLineProvider **dest,
        iTVPScanLineProvider *, iTVPScanLineProvider *src2) override {
        if (!dest || !src2) return TJS_E_FAIL;
        *dest = src2; return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data) override {
        if (!data || !data->Dest || !data->Src1 || !data->Src2 || data->Left < 0 || data->Top < 0 ||
            data->Width < 0 || data->Height < 0 || data->DestLeft < 0 || data->DestTop < 0 ||
            data->Left > width || data->Top > height || data->Width > width - data->Left ||
            data->Height > height - data->Top) return TJS_E_FAIL;
        if (!data->Width || !data->Height) return TJS_S_OK;
        try {
            auto manager = TVPGetRenderManager();
            if (!prepared) {
                snapshot(data->Src1->GetTexture(), source1, width, height);
                snapshot(data->Src2->GetTexture(), source2, width, height);
                prepared = true;
            }
            pixels.resize(size_t(data->Width) * data->Height);
            transition.render(NaganoTransitions::Image(source1->GetScanLineForRead(0), source1->GetPitch()),
                NaganoTransitions::Image(source2->GetScanLineForRead(0), source2->GetPitch()),
                pixels.data(), data->Left, data->Top, data->Width, data->Height);
            const tTVPRect sourceRect(0, 0, data->Width, data->Height);
            if (!patch || patch->GetWidth() != unsigned(data->Width) || patch->GetHeight() != unsigned(data->Height))
                patch.reset(manager->CreateTexture2D(pixels.data(), data->Width * 4, data->Width, data->Height,
                    TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS));
            else patch->Update(pixels.data(), TVPTextureFormat::RGBA, data->Width * 4, sourceRect);
            if (!patch) throw std::runtime_error("cannot allocate extNagano update image");
            auto dest = data->Dest->GetTextureForRender();
            if (!dest || unsigned(data->DestLeft) > dest->GetWidth() || unsigned(data->DestTop) > dest->GetHeight() ||
                unsigned(data->Width) > dest->GetWidth() - unsigned(data->DestLeft) ||
                unsigned(data->Height) > dest->GetHeight() - unsigned(data->DestTop)) return TJS_E_FAIL;
            const tTVPRect destRect(data->DestLeft, data->DestTop,
                                   data->DestLeft + data->Width, data->DestTop + data->Height);
            const tRenderTexRectArray::Element sources[] = { tRenderTexRectArray::Element(patch.get(), sourceRect) };
            manager->OperateRect(manager->GetRenderMethod("Copy"), dest, nullptr, destRect, tRenderTexRectArray(sources));
            return TJS_S_OK;
        } catch (const std::exception &error) {
            TVPThrowExceptionMessage(ttstr(error.what()).c_str());
            return TJS_E_FAIL;
        }
    }
};

class NaganoProvider : public iTVPTransHandlerProvider {
    unsigned refs = 1;
    const tjs_char *name;
    NaganoTransitions::Effect effect;

    static bool value(iTVPSimpleOptionProvider *provider, const tjs_char *key, tTJSVariant &v) {
        return TJS_SUCCEEDED(provider->GetValue(key, &v)) && v.Type() != tvtVoid;
    }
    static int64_t integer(iTVPSimpleOptionProvider *provider, const tjs_char *key, int64_t fallback,
                           int64_t low = INT32_MIN, int64_t high = INT32_MAX) {
        tTJSVariant v;
        if (!value(provider, key, v)) return fallback;
        if (v.Type() != tvtInteger && (!std::isfinite(double(v)) ||
            static_cast<long double>(double(v)) < static_cast<long double>(low) ||
            static_cast<long double>(double(v)) > static_cast<long double>(high) ||
            double(v) >= 9223372036854775808.0))
            throw std::invalid_argument("extNagano integer option is out of range");
        const int64_t result = tjs_int64(v);
        if (result < low || result > high) throw std::invalid_argument("extNagano integer option is out of range");
        return result;
    }
    static double real(iTVPSimpleOptionProvider *provider, const tjs_char *key, double fallback,
                       const tjs_char *alias = nullptr) {
        tTJSVariant v;
        if (value(provider, key, v) || (alias && value(provider, alias, v))) return double(v);
        return fallback;
    }
    static std::vector<double> coordinates(iTVPSimpleOptionProvider *provider, const tjs_char *key) {
        tTJSVariant v, count;
        if (!value(provider, key, v) || v.Type() != tvtObject)
            throw std::invalid_argument("extNagano morphing requires before/after numeric arrays");
        const auto object = v.AsObjectClosureNoAddRef();
        if (!object.Object || TJS_FAILED(object.PropGet(0, TJS_W("count"), nullptr, &count, nullptr)))
            throw std::invalid_argument("extNagano morphing array has no count");
        const double size = double(count);
        if (!std::isfinite(size) || size < 6 || size > 6 * 4096 || std::floor(size) != size || int(size) % 6)
            throw std::invalid_argument("extNagano morphing array must contain six coordinates per triangle");
        std::vector<double> result(size_t(int(size)), 0.0);
        for (int i = 0; i < int(size); ++i) {
            tTJSVariant coordinate;
            if (TJS_FAILED(object.PropGetByNum(0, i, &coordinate, nullptr)) || coordinate.Type() == tvtVoid)
                throw std::invalid_argument("extNagano morphing array has a missing coordinate");
            result[size_t(i)] = double(coordinate);
        }
        return result;
    }
public:
    NaganoProvider(const tjs_char *n, NaganoTransitions::Effect e) : name(n), effect(e) {}
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if (!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetName(const tjs_char **result) override {
        if (result) *result = name;
        return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD StartTransition(iTVPSimpleOptionProvider *provider, iTVPSimpleImageProvider *images,
        tTVPLayerType layerType, tjs_uint w, tjs_uint h, tjs_uint w2, tjs_uint h2,
        tTVPTransType *type, tTVPTransUpdateType *updateType, iTVPBaseTransHandler **handler) override {
        if (!handler) return TJS_E_FAIL;
        *handler = nullptr;
        if (!provider || w != w2 || h != h2 || !w || !h || w > 16384 || h > 16384) return TJS_E_FAIL;
        try {
            tTJSVariant time;
            if (!value(provider, TJS_W("time"), time)) throw std::invalid_argument("extNagano requires time");
            NaganoTransitions::Options o;
            o.effect = effect;
            o.alpha = TVPIsTypeUsingAlpha(layerType) ? NaganoTransitions::Alpha::Straight :
                TVPIsTypeUsingAddAlpha(layerType) ? NaganoTransitions::Alpha::Additive : NaganoTransitions::Alpha::Opaque;
            o.time = uint64_t(std::max<int64_t>(2, integer(provider, TJS_W("time"), 2, 0, INT64_MAX)));
            uint32_t entropy[4];
            TVPGetRandomBits128(entropy); o.seed = entropy[0];
            using NaganoTransitions::Effect;
            switch (effect) {
            case Effect::Universal3D: {
                o.speed1 = real(provider, TJS_W("speed1"), 0, TJS_W("s1"));
                o.speed2 = real(provider, TJS_W("speed2"), 0, TJS_W("s2"));
                o.accel1 = real(provider, TJS_W("accel1"), 0, TJS_W("a1"));
                o.accel2 = real(provider, TJS_W("accel2"), 0, TJS_W("a2"));
                o.bound1 = int(integer(provider, TJS_W("bound1"), 0, 0, 127));
                o.bound2 = int(integer(provider, TJS_W("bound2"), 0, 0, 127));
                tTJSVariant typeName;
                if (value(provider, TJS_W("type"), typeName)) {
                    const ttstr kind(typeName);
                    if (kind == TJS_W("HSB") || kind == TJS_W("hsb")) o.hsb = true;
                    else if (kind != TJS_W("RGB") && kind != TJS_W("rgb"))
                        throw std::invalid_argument("extNagano 3duniversal type must be RGB or HSB");
                }
                break;
            }
            case Effect::BlurFade:
                o.exponent = real(provider, TJS_W("exponent"), 1);
                o.blur1x = o.blur1y = int(integer(provider, TJS_W("blur1"), 0));
                o.blur2x = o.blur2y = int(integer(provider, TJS_W("blur2"), 0));
                o.blur1x = int(integer(provider, TJS_W("blur1x"), o.blur1x));
                o.blur1y = int(integer(provider, TJS_W("blur1y"), o.blur1y));
                o.blur2x = int(integer(provider, TJS_W("blur2x"), o.blur2x));
                o.blur2y = int(integer(provider, TJS_W("blur2y"), o.blur2y));
                o.blurtype = int(integer(provider, TJS_W("type"), 0));
                o.prerender = int(integer(provider, TJS_W("prerender"), 0));
                break;
            case Effect::ZoomFade:
                o.zoom1 = real(provider, TJS_W("zoom1"), 100);
                o.zoom2 = real(provider, TJS_W("zoom2"), 200);
                break;
            case Effect::RGBFade:
                o.delay = {{ int(integer(provider, TJS_W("delayR"), 0)), int(integer(provider, TJS_W("delayG"), 0)),
                             int(integer(provider, TJS_W("delayB"), 0)), int(integer(provider, TJS_W("delayA"), 0)) }};
                break;
            case Effect::Spin:
                o.type1 = int(integer(provider, TJS_W("type1"), 0));
                o.type2 = int(integer(provider, TJS_W("type2"), 1));
                break;
            case Effect::Flutter: {
                const uint32_t color = uint32_t(integer(provider, TJS_W("back"), 0, INT32_MIN, UINT32_MAX));
                o.back = TVP_REVRGB(color);
                o.backalpha = int(integer(provider, TJS_W("alpha"), 255));
                o.slip = int(integer(provider, TJS_W("slip"), 8));
                break;
            }
            case Effect::Book: o.dir = int(integer(provider, TJS_W("dir"), -1)); break;
            case Effect::ImageWipe: o.dir = int(integer(provider, TJS_W("dir"), 0)); break;
            case Effect::HoneyTurn:
                o.size = int(integer(provider, TJS_W("size"), 40));
                o.dir = int(integer(provider, TJS_W("dir"), 6));
                o.order = int(integer(provider, TJS_W("order"), 2));
                o.twist = real(provider, TJS_W("twist"), 0);
                break;
            case Effect::Morphing:
                o.before = coordinates(provider, TJS_W("before")); o.after = coordinates(provider, TJS_W("after"));
                break;
            case Effect::MultiRipple:
                o.count = int(integer(provider, TJS_W("count"), 1));
                o.wavecount = int(integer(provider, TJS_W("wavecount"), 2));
                o.rwidth = int(integer(provider, TJS_W("rwidth"), 32));
                o.maxdrift = int(integer(provider, TJS_W("maxdrift"), 24));
                o.roundness = real(provider, TJS_W("roundness"), 1);
                o.delaylast = real(provider, TJS_W("delaylast"), 1);
                break;
            case Effect::ScanLine: break;
            }
            NaganoTransitions::validate(o, int(w), int(h));
            RuleImage rule;
            if (effect == Effect::Universal3D || effect == Effect::ImageWipe) rule = loadRule(provider, images, effect, w, h);
            *handler = new NaganoTransition(std::move(o), int(w), int(h), std::move(rule));
            if (type) *type = ttExchange;
            if (updateType) *updateType = tutDivisible;
            return TJS_S_OK;
        } catch (const std::exception &error) {
            TVPThrowExceptionMessage(ttstr(error.what()).c_str());
            return TJS_E_FAIL;
        }
    }
};

NaganoProvider *providers[12] = {};
void unregisterProviders() {
    for (auto &provider : providers) if (provider) {
        TVPRemoveTransHandlerProvider(provider); provider->Release(); provider = nullptr;
    }
}
void registerProviders() {
    using NaganoTransitions::Effect;
    const tjs_char *names[] = { TJS_W("3duniversal"), TJS_W("blurfade"), TJS_W("scanline"), TJS_W("zoomfade"),
        TJS_W("rgbfade"), TJS_W("spin"), TJS_W("flutter"), TJS_W("book"), TJS_W("imagewipe"),
        TJS_W("honeyturn"), TJS_W("morphing"), TJS_W("multiripple") };
    const Effect effects[] = { Effect::Universal3D, Effect::BlurFade, Effect::ScanLine, Effect::ZoomFade,
        Effect::RGBFade, Effect::Spin, Effect::Flutter, Effect::Book, Effect::ImageWipe,
        Effect::HoneyTurn, Effect::Morphing, Effect::MultiRipple };
    try {
        for (size_t i = 0; i < sizeof(providers) / sizeof(providers[0]); ++i) {
            if (providers[i]) continue;
            auto provider = new NaganoProvider(names[i], effects[i]);
            try { TVPAddTransHandlerProvider(provider); }
            catch (...) { provider->Release(); throw; }
            providers[i] = provider;
        }
    } catch (...) { unregisterProviders(); throw; }
}
} // namespace

NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
