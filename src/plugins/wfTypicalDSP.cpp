#define NCB_MODULE_NAME TJS_W("wfTypicalDSP.dll")
#include "ncbind/ncbind.hpp"
#include "WaveFilterBridge.h"
#include "TypicalDSP.h"
#include "tjsDictionary.h"

class WaveDSPFilter : public WaveFilterBridge {
    krkr::audio::TypicalDSP dsp_;
protected:
    void PrepareDSP(unsigned rate, unsigned channels) override { dsp_.Prepare(rate, channels); }
    void ResetDSP() override { dsp_.Reset(); }
    void ProcessDSP(float *data, std::size_t frames, unsigned channels) override {
        dsp_.Process(data, frames, channels);
    }
public:
    WaveDSPFilter(const std::string &response, const std::string &family, const std::string &state)
        : dsp_(response, family, state) {}
    static tjs_error Factory(WaveDSPFilter **result, tjs_int count, tTJSVariant **params,
                             iTJSDispatch2 *) {
        const std::string response = count > 0 && params[0]->Type() != tvtVoid
            ? ttstr(*params[0]).AsStdString() : "LowPass";
        const std::string family = count > 1 && params[1]->Type() != tvtVoid
            ? ttstr(*params[1]).AsStdString() : "RBJ";
        const std::string state = count > 2 && params[2]->Type() != tvtVoid
            ? ttstr(*params[2]).AsStdString() : "DirectFormII";
        *result = new WaveDSPFilter(response, family, state);
        return TJS_S_OK;
    }
    tjs_int64 Interface() const { return GetInterface(); }
    void FinalizeFilter() { Finalize(); }
    static tjs_error SetParamsCallback(tTJSVariant *result, tjs_int count,
                                      tTJSVariant **params, WaveDSPFilter *self) {
        return self->SetParams(result, count, params, nullptr);
    }
    static tjs_error GetParamInfoCallback(tTJSVariant *result, tjs_int count,
                                         tTJSVariant **params, WaveDSPFilter *self) {
        return self->GetParamInfo(result, count, params, nullptr);
    }
    tjs_error SetParams(tTJSVariant *result, tjs_int count, tTJSVariant **params,
                        iTJSDispatch2 *) {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        std::array<double, 8> values{};
        std::array<bool, 8> present{};
        const int used = std::min<tjs_int>(count, 8);
        for (int i = 0; i < used; ++i) {
            present[i] = params[i]->Type() != tvtVoid;
            if (present[i]) values[i] = static_cast<tjs_real>(*params[i]);
        }
        dsp_.SetParameters(values, present);
        if (result) *result = used;
        return TJS_S_OK;
    }
    tjs_error GetParamInfo(tTJSVariant *result, tjs_int count, tTJSVariant **params,
                           iTJSDispatch2 *) {
        if (count < 1) return TJS_E_BADPARAMCOUNT;
        if (!result) return TJS_S_OK;
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        const tjs_int index = *params[0];
        if (index < 0 || index >= dsp_.ParameterCount()) {
            result->Clear();
            return TJS_S_OK;
        }
        const Dsp::ParamInfo info = dsp_.ParameterInfo(index);
        iTJSDispatch2 *dictionary = TJSCreateDictionaryObject();
        tTJSVariant value(dictionary, dictionary);
        dictionary->Release();
        const auto set = [&](const tjs_char *key, const tTJSVariant &v) {
            dictionary->PropSet(TJS_MEMBERENSURE, key, nullptr, &v, dictionary);
        };
        set(TJS_W("name"), tTJSVariant(info.getName()));
        set(TJS_W("label"), tTJSVariant(info.getLabel()));
        set(TJS_W("defaultValue"), tTJSVariant(info.getDefaultValue()));
        set(TJS_W("currentValue"), tTJSVariant(dsp_.CurrentValue(index)));
        set(TJS_W("min"), tTJSVariant(info.toNativeValue(0)));
        set(TJS_W("max"), tTJSVariant(info.toNativeValue(1)));
        *result = value;
        return TJS_S_OK;
    }
};

NCB_REGISTER_SUBCLASS(WaveDSPFilter) {
    Factory(&Class::Factory);
    Property(TJS_W("interface"), &Class::Interface, (int)0);
    RawCallback(TJS_W("setParams"), &Class::SetParamsCallback, 0);
    RawCallback(TJS_W("getParamInfo"), &Class::GetParamInfoCallback, 0);
    Method(TJS_W("finalize"), &Class::FinalizeFilter);
}
class TypicalWaveEffects {};
NCB_ATTACH_CLASS(TypicalWaveEffects, WaveSoundBuffer) {
    NCB_SUBCLASS(WaveDSPFilter, WaveDSPFilter);
}
