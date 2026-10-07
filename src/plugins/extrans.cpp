#include "ncbind/ncbind.hpp"
#include "TransIntf.h"
#include "RenderManager.h"
#include "LayerBitmapIntf.h"
#include "ExtraTransitions.h"
#include <memory>

#define NCB_MODULE_NAME TJS_W("extrans.dll")

namespace {

struct ReleaseTexture {
    void operator()(iTVPTexture2D *texture) const { if (texture) texture->Release(); }
};
using Texture = std::unique_ptr<iTVPTexture2D, ReleaseTexture>;

class ExtraTransition : public iTVPDivisibleTransHandler {
    unsigned refs = 1;
    int width, height;
    uint64_t duration, start = 0, elapsed = 0;
    bool first = true, prepared = false;
    ExtraTransitions::Transition transition;
    Texture source1, source2, patch;
    std::vector<uint32_t> pixels;

    void snapshot(iTVPScanLineProvider *source, Texture &texture) {
        auto manager = TVPGetRenderManager();
        if (!texture) texture.reset(manager->CreateTexture2D(nullptr, 0, width, height,
            TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS));
        auto input = source->GetTexture();
        if (!input || input->GetWidth() < unsigned(width) || input->GetHeight() < unsigned(height))
            throw std::invalid_argument("extrans source is smaller than the transition image");
        const tTVPRect rect(0, 0, width, height);
        const tRenderTexRectArray::Element sources[] = { tRenderTexRectArray::Element(input, rect) };
        // An explicit RGBA snapshot also handles compressed, scaled and RGB
        // textures. Retain it for all of this frame's divided update regions.
        manager->OperateRect(manager->GetRenderMethod("Copy"), texture.get(), nullptr,
                             rect, tRenderTexRectArray(sources));
    }

public:
    ExtraTransition(const ExtraTransitions::Options &options, int w, int h)
        : width(w), height(h), duration(options.time), transition(options, w, h) {}
    virtual ~ExtraTransition() {}

    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override {
        if (!--refs) delete this;
        return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD SetOption(iTVPSimpleOptionProvider *) override { return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD StartProcess(tjs_uint64 tick) override {
        if (first) { start = tick; first = false; }
        elapsed = tick < start ? 0 : std::min<uint64_t>(tick - start, duration);
        transition.setTime(elapsed);
        prepared = false;
        return TJS_S_TRUE;
    }
    tjs_error TJS_INTF_METHOD EndProcess() override {
        return elapsed >= duration ? TJS_S_FALSE : TJS_S_TRUE;
    }
    tjs_error TJS_INTF_METHOD MakeFinalImage(iTVPScanLineProvider **dest,
                                            iTVPScanLineProvider *, iTVPScanLineProvider *source) override {
        if (!dest || !source) return TJS_E_FAIL;
        *dest = source;
        return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD Process(tTVPDivisibleData *data) override {
        if (!data || !data->Dest || !data->Src1 || !data->Src2) return TJS_E_FAIL;
        if (!data->Width || !data->Height) return TJS_S_OK;
        if (data->Width < 0 || data->Height < 0 || data->Left < 0 || data->Top < 0 ||
            data->Left > width || data->Top > height || data->Width > width - data->Left ||
            data->Height > height - data->Top || data->DestLeft < 0 || data->DestTop < 0)
            return TJS_E_FAIL;
        try {
            auto target = data->Dest->GetTextureForRender();
            if (!target || uint64_t(data->DestLeft) + data->Width > target->GetWidth() ||
                uint64_t(data->DestTop) + data->Height > target->GetHeight()) return TJS_E_FAIL;
            if (!prepared) {
                snapshot(data->Src1, source1);
                snapshot(data->Src2, source2);
                prepared = true;
            }
            pixels.resize(size_t(data->Width) * data->Height);
            transition.render(
                ExtraTransitions::Image(source1->GetScanLineForRead(0), source1->GetPitch()),
                ExtraTransitions::Image(source2->GetScanLineForRead(0), source2->GetPitch()),
                pixels.data(), data->Left, data->Top, data->Width, data->Height);
            auto manager = TVPGetRenderManager();
            if (!patch || patch->GetWidth() != unsigned(data->Width) || patch->GetHeight() != unsigned(data->Height))
                patch.reset(manager->CreateTexture2D(nullptr, 0, data->Width, data->Height,
                    TVPTextureFormat::RGBA, RENDER_CREATE_TEXTURE_FLAG_NO_COMPRESS));
            const tTVPRect patchRect(0, 0, data->Width, data->Height);
            patch->Update(pixels.data(), TVPTextureFormat::RGBA, data->Width * 4, patchRect);
            const tRenderTexRectArray::Element sources[] = { tRenderTexRectArray::Element(patch.get(), patchRect) };
            // Copy a complete patch through the renderer. Direct offset uploads
            // do not update every backend's cached pixels consistently.
            manager->OperateRect(manager->GetRenderMethod("Copy"), target, nullptr,
                tTVPRect(data->DestLeft, data->DestTop, data->DestLeft + data->Width, data->DestTop + data->Height),
                tRenderTexRectArray(sources));
            return TJS_S_OK;
        } catch (const std::exception &error) {
            TVPThrowExceptionMessage(ttstr(error.what()).c_str());
            return TJS_E_FAIL;
        }
    }
};

class ExtraProvider : public iTVPTransHandlerProvider {
    unsigned refs = 1;
    const tjs_char *name;
    ExtraTransitions::Effect effect;

    static bool value(iTVPSimpleOptionProvider *provider, const tjs_char *key, tTJSVariant &result) {
        return TJS_SUCCEEDED(provider->GetValue(key, &result)) && result.Type() != tvtVoid;
    }
    static int64_t integer(iTVPSimpleOptionProvider *provider, const tjs_char *key, int64_t fallback,
                           int64_t low = INT32_MIN, int64_t high = INT32_MAX) {
        tTJSVariant v;
        if (!value(provider, key, v)) return fallback;
        if (v.Type() == tvtReal && (!std::isfinite(double(v)) || double(v) < double(low) ||
                                   double(v) > double(high) || double(v) >= 9223372036854775808.0))
            throw std::invalid_argument("extrans integer option is out of range");
        const int64_t result = tjs_int64(v);
        if (result < low || result > high) throw std::invalid_argument("extrans integer option is out of range");
        return result;
    }
    static double real(iTVPSimpleOptionProvider *provider, const tjs_char *key, double fallback) {
        tTJSVariant v;
        return value(provider, key, v) ? double(v) : fallback;
    }
    static uint32_t color(iTVPSimpleOptionProvider *provider, const tjs_char *key, uint32_t fallback = 0) {
        const uint32_t result = uint32_t(integer(provider, key, fallback, INT32_MIN, UINT32_MAX));
        return TVP_REVRGB(result);
    }
public:
    ExtraProvider(const tjs_char *n, ExtraTransitions::Effect e) : name(n), effect(e) {}
    tjs_error TJS_INTF_METHOD AddRef() override { ++refs; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD Release() override { if (!--refs) delete this; return TJS_S_OK; }
    tjs_error TJS_INTF_METHOD GetName(const tjs_char **result) override {
        if (result) *result = name;
        return TJS_S_OK;
    }
    tjs_error TJS_INTF_METHOD StartTransition(iTVPSimpleOptionProvider *provider, iTVPSimpleImageProvider *,
        tTVPLayerType layerType, tjs_uint w, tjs_uint h, tjs_uint w2, tjs_uint h2,
        tTVPTransType *type, tTVPTransUpdateType *updateType, iTVPBaseTransHandler **handler) override {
        if (!handler) return TJS_E_FAIL;
        *handler = nullptr;
        if (!provider || w != w2 || h != h2 || !w || !h || w > 16384 || h > 16384) return TJS_E_FAIL;
        try {
            tTJSVariant time;
            if (!value(provider, TJS_W("time"), time))
                throw std::invalid_argument("extrans time option is required");
            ExtraTransitions::Options o;
            o.effect = effect;
            o.alpha = TVPIsTypeUsingAlpha(layerType) ? ExtraTransitions::Alpha::Straight :
                TVPIsTypeUsingAddAlpha(layerType) ? ExtraTransitions::Alpha::Additive : ExtraTransitions::Alpha::Opaque;
            o.time = uint64_t(std::max<int64_t>(2, integer(provider, TJS_W("time"), 2, 0, INT64_MAX)));
            switch (effect) {
            case ExtraTransitions::Effect::Mosaic:
                o.maxsize = int(integer(provider, TJS_W("maxsize"), 30));
                break;
            case ExtraTransitions::Effect::Wave:
                o.maxh = int(integer(provider, TJS_W("maxh"), 50));
                o.maxomega = real(provider, TJS_W("maxomega"), 0.2);
                o.bgcolor1 = color(provider, TJS_W("bgcolor1"));
                o.bgcolor2 = color(provider, TJS_W("bgcolor2"));
                o.wavetype = int(integer(provider, TJS_W("wavetype"), 0));
                break;
            case ExtraTransitions::Effect::Ripple:
                o.centerx = int(integer(provider, TJS_W("centerx"), w / 2));
                o.centery = int(integer(provider, TJS_W("centery"), h / 2));
                o.rwidth = int(integer(provider, TJS_W("rwidth"), 128));
                o.roundness = real(provider, TJS_W("roundness"), 1.0);
                o.speed = real(provider, TJS_W("speed"), 6.0);
                o.maxdrift = int(integer(provider, TJS_W("maxdrift"), 24));
                break;
            case ExtraTransitions::Effect::RotateSwap:
                o.bgcolor = color(provider, TJS_W("bgcolor"));
                o.twist = real(provider, TJS_W("twist"), 1.0);
                break;
            case ExtraTransitions::Effect::Turn:
                o.bgcolor = color(provider, TJS_W("bgcolor"), 0xffffff);
                break;
            case ExtraTransitions::Effect::RotateZoom:
            case ExtraTransitions::Effect::RotateVanish: {
                const bool zoom = effect == ExtraTransitions::Effect::RotateZoom;
                o.factor = zoom ? real(provider, TJS_W("factor"), 1.0) : 1.0;
                o.accel = real(provider, TJS_W("accel"), zoom ? 0.0 : 2.0);
                o.twist = real(provider, TJS_W("twist"), 2.0);
                o.twistaccel = real(provider, TJS_W("twistaccel"), zoom ? -2.0 : 2.0);
                o.centerx = int(integer(provider, TJS_W("centerx"), w / 2));
                o.centery = int(integer(provider, TJS_W("centery"), h / 2));
                break;
            }
            }
            *handler = new ExtraTransition(o, int(w), int(h));
            if (type) *type = ttExchange;
            if (updateType) *updateType = tutDivisible;
            return TJS_S_OK;
        } catch (const std::exception &error) {
            TVPThrowExceptionMessage(ttstr(error.what()).c_str());
            return TJS_E_FAIL;
        }
    }
};

ExtraProvider *providers[7] = {};
void unregisterProviders() {
    for (auto &provider : providers) if (provider) {
        TVPRemoveTransHandlerProvider(provider);
        provider->Release();
        provider = nullptr;
    }
}
void registerProviders() {
    const tjs_char *names[] = { TJS_W("mosaic"), TJS_W("wave"), TJS_W("ripple"), TJS_W("rotateswap"),
                              TJS_W("turn"), TJS_W("rotatezoom"), TJS_W("rotatevanish") };
    const ExtraTransitions::Effect effects[] = { ExtraTransitions::Effect::Mosaic, ExtraTransitions::Effect::Wave,
        ExtraTransitions::Effect::Ripple, ExtraTransitions::Effect::RotateSwap, ExtraTransitions::Effect::Turn,
        ExtraTransitions::Effect::RotateZoom, ExtraTransitions::Effect::RotateVanish };
    try {
        for (unsigned i = 0; i < sizeof(providers) / sizeof(providers[0]); ++i) {
            if (providers[i]) continue;
            auto provider = new ExtraProvider(names[i], effects[i]);
            try { TVPAddTransHandlerProvider(provider); }
            catch (...) { provider->Release(); throw; }
            providers[i] = provider;
        }
    } catch (...) { unregisterProviders(); throw; }
}
} // namespace

NCB_PRE_REGIST_CALLBACK(registerProviders);
NCB_POST_UNREGIST_CALLBACK(unregisterProviders);
