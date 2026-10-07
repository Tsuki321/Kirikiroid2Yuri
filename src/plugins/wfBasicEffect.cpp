#define NCB_MODULE_NAME TJS_W("wfBasicEffect.dll")
#include "ncbind/ncbind.hpp"
#include "WaveFilterBridge.h"
#include <memory>

namespace {
double Parameter(tjs_int count, tTJSVariant **params, tjs_int index, double fallback) {
    return index < count && params[index]->Type() != tvtVoid
        ? static_cast<tjs_real>(*params[index]) : fallback;
}
}

class GainLimit : public WaveFilterBridge {
    krkr::audio::GainLimitDSP dsp_;
protected:
    void PrepareDSP(unsigned, unsigned) override {}
    void ResetDSP() override { dsp_.Reset(); }
    void ProcessDSP(float *data, std::size_t frames, unsigned channels) override {
        dsp_.Process(data, frames, channels);
    }
public:
    tjs_int64 Interface() const { return GetInterface(); }
    void FinalizeFilter() { Finalize(); }
    static tjs_error InitCallback(tTJSVariant *result, tjs_int count,
                                  tTJSVariant **params, GainLimit *self) {
        return self->Init(result, count, params, nullptr);
    }
    static tjs_error Factory(GainLimit **result, tjs_int count, tTJSVariant **params,
                             iTJSDispatch2 *) {
        std::unique_ptr<GainLimit> filter(new GainLimit());
        filter->Init(nullptr, count, params, nullptr);
        *result = filter.release();
        return TJS_S_OK;
    }
    tjs_error Init(tTJSVariant *, tjs_int count, tTJSVariant **params, iTJSDispatch2 *) {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        const double mode = Parameter(count, params, 2, 0);
        krkr::audio::CheckedRange(mode, -1, 3);
        dsp_.Configure(Parameter(count, params, 0, 0),
                       Parameter(count, params, 1, 1), static_cast<int>(mode));
        return TJS_S_OK;
    }
};

class DelayEffect : public WaveFilterBridge {
    krkr::audio::DelayDSP dsp_;
protected:
    void PrepareDSP(unsigned rate, unsigned channels) override { dsp_.Prepare(rate, channels); }
    void ResetDSP() override { dsp_.Reset(); }
    void ProcessDSP(float *data, std::size_t frames, unsigned channels) override {
        dsp_.Process(data, frames, channels);
    }
public:
    DelayEffect() { extendMs_ = 1000; }
    tjs_int64 Interface() const { return GetInterface(); }
    void FinalizeFilter() { Finalize(); }
    static tjs_error InitCallback(tTJSVariant *result, tjs_int count,
                                  tTJSVariant **params, DelayEffect *self) {
        return self->Init(result, count, params, nullptr);
    }
    static tjs_error Factory(DelayEffect **result, tjs_int count, tTJSVariant **params,
                             iTJSDispatch2 *) {
        std::unique_ptr<DelayEffect> filter(new DelayEffect());
        if (count) {
            const tjs_error error = filter->Init(nullptr, count, params, nullptr);
            if (TJS_FAILED(error)) return error;
        }
        *result = filter.release();
        return TJS_S_OK;
    }
    tjs_error Init(tTJSVariant *, tjs_int count, tTJSVariant **params, iTJSDispatch2 *) {
        if (count < 2) return TJS_E_BADPARAMCOUNT;
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        const double maximum = Parameter(count, params, 3, 1000);
        krkr::audio::CheckedRange(maximum, 0, 60000);
        dsp_.Configure(Parameter(count, params, 0, 0), Parameter(count, params, 1, 0),
                       Parameter(count, params, 2, 1), static_cast<unsigned>(maximum));
        return TJS_S_OK;
    }
};

class GraphicEqualizer : public WaveFilterBridge {
    krkr::audio::EqualizerDSP dsp_;
protected:
    void PrepareDSP(unsigned rate, unsigned channels) override { dsp_.Prepare(rate, channels); }
    void ResetDSP() override { dsp_.Reset(); }
    void ProcessDSP(float *data, std::size_t frames, unsigned channels) override {
        dsp_.Process(data, frames, channels);
    }
public:
    tjs_int64 Interface() const { return GetInterface(); }
    void FinalizeFilter() { Finalize(); }
    static tjs_error Factory(GraphicEqualizer **result, tjs_int count, tTJSVariant **params,
                             iTJSDispatch2 *) {
        std::unique_ptr<GraphicEqualizer> filter(new GraphicEqualizer());
        for (tjs_int i = 0; i < std::min<tjs_int>(count, 10); ++i)
            filter->SetGain(i, Parameter(count, params, i, 1));
        *result = filter.release();
        return TJS_S_OK;
    }
    tjs_real GetGain(tjs_int band) const {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        return dsp_.GetGain(static_cast<unsigned>(band));
    }
    void SetGain(tjs_int band, tjs_real gain) {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        dsp_.SetGain(static_cast<unsigned>(band), gain);
    }
};

class StkFreeVerb : public WaveFilterBridge {
    krkr::audio::FreeVerbDSP dsp_;
protected:
    void PrepareDSP(unsigned rate, unsigned channels) override { dsp_.Prepare(rate, channels); }
    void ResetDSP() override { dsp_.Reset(); }
    void ProcessDSP(float *data, std::size_t frames, unsigned channels) override {
        dsp_.Process(data, frames, channels);
    }
public:
    StkFreeVerb() { extendMs_ = 1000; }
    tjs_int64 Interface() const { return GetInterface(); }
    void FinalizeFilter() { Finalize(); }
    static tjs_error Factory(StkFreeVerb **result, tjs_int count, tTJSVariant **params,
                             iTJSDispatch2 *) {
        std::unique_ptr<StkFreeVerb> filter(new StkFreeVerb());
        if (count) filter->SetMix(Parameter(count, params, 0, 0.75));
        *result = filter.release();
        return TJS_S_OK;
    }
    tjs_real GetMix() const { std::lock_guard<std::recursive_mutex> g(mutex_); return dsp_.Mix(); }
    void SetMix(tjs_real value) { std::lock_guard<std::recursive_mutex> g(mutex_); dsp_.SetMix(value); }
    tjs_real GetRoom() const { std::lock_guard<std::recursive_mutex> g(mutex_); return dsp_.Room(); }
    void SetRoom(tjs_real value) { std::lock_guard<std::recursive_mutex> g(mutex_); dsp_.SetRoom(value); }
    tjs_real GetDamping() const { std::lock_guard<std::recursive_mutex> g(mutex_); return dsp_.Damping(); }
    void SetDamping(tjs_real value) { std::lock_guard<std::recursive_mutex> g(mutex_); dsp_.SetDamping(value); }
    tjs_real GetWidth() const { std::lock_guard<std::recursive_mutex> g(mutex_); return dsp_.Width(); }
    void SetWidth(tjs_real value) { std::lock_guard<std::recursive_mutex> g(mutex_); dsp_.SetWidth(value); }
    bool GetMode() const { std::lock_guard<std::recursive_mutex> g(mutex_); return dsp_.Frozen(); }
    void SetMode(bool value) { std::lock_guard<std::recursive_mutex> g(mutex_); dsp_.SetFrozen(value); }
    tjs_int GetExtend() const { std::lock_guard<std::recursive_mutex> g(mutex_); return extendMs_; }
    void SetExtend(tjs_int value) {
        std::lock_guard<std::recursive_mutex> g(mutex_);
        krkr::audio::CheckedRange(value, 0, 60000);
        extendMs_ = value;
    }
};

NCB_REGISTER_SUBCLASS(GainLimit) {
    Factory(&Class::Factory);
    Property(TJS_W("interface"), &Class::Interface, (int)0);
    RawCallback(TJS_W("init"), &Class::InitCallback, 0);
    Method(TJS_W("finalize"), &Class::FinalizeFilter);
}
NCB_REGISTER_SUBCLASS(DelayEffect) {
    Factory(&Class::Factory);
    Property(TJS_W("interface"), &Class::Interface, (int)0);
    RawCallback(TJS_W("init"), &Class::InitCallback, 0);
    Method(TJS_W("finalize"), &Class::FinalizeFilter);
}
NCB_REGISTER_SUBCLASS(GraphicEqualizer) {
    Factory(&Class::Factory);
    Property(TJS_W("interface"), &Class::Interface, (int)0);
    Method(TJS_W("getGain"), &Class::GetGain);
    Method(TJS_W("setGain"), &Class::SetGain);
    Method(TJS_W("finalize"), &Class::FinalizeFilter);
}
NCB_REGISTER_SUBCLASS(StkFreeVerb) {
    Factory(&Class::Factory);
    Property(TJS_W("interface"), &Class::Interface, (int)0);
    Property(TJS_W("effectMix"), &Class::GetMix, &Class::SetMix);
    Property(TJS_W("roomSize"), &Class::GetRoom, &Class::SetRoom);
    Property(TJS_W("damping"), &Class::GetDamping, &Class::SetDamping);
    Property(TJS_W("width"), &Class::GetWidth, &Class::SetWidth);
    Property(TJS_W("mode"), &Class::GetMode, &Class::SetMode);
    Property(TJS_W("extend"), &Class::GetExtend, &Class::SetExtend);
    Method(TJS_W("finalize"), &Class::FinalizeFilter);
}

class BasicWaveEffects {};
NCB_ATTACH_CLASS(BasicWaveEffects, WaveSoundBuffer) {
    NCB_SUBCLASS(GainLimit, GainLimit);
    NCB_SUBCLASS(DelayEffect, DelayEffect);
    NCB_SUBCLASS(GraphicEqualizer, GraphicEqualizer);
    NCB_SUBCLASS(StkFreeVerb, StkFreeVerb);
}
