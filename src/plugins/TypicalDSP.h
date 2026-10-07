#pragma once

#include "DspFilters/Filter.h"
#include <array>
#include <cstddef>
#include <memory>
#include <string>

namespace krkr {
namespace audio {

class TypicalDSP {
    std::unique_ptr<Dsp::Filter> filter_;
    Dsp::Params requested_;
    bool automaticRate_ = true;
    unsigned streamRate_ = 0;
    unsigned channels_ = 0;
    void ApplyParameters();
public:
    TypicalDSP(const std::string &response = "LowPass",
               const std::string &family = "RBJ",
               const std::string &state = "DirectFormII");
    ~TypicalDSP();
    TypicalDSP(const TypicalDSP &) = delete;
    TypicalDSP &operator=(const TypicalDSP &) = delete;
    int ParameterCount() const;
    Dsp::ParamInfo ParameterInfo(unsigned index) const;
    double CurrentValue(unsigned index) const;
    std::string Name() const;
    // Missing/void slots reset to the filter design's defaults, not the
    // previously assigned value. Slot zero tracks the source's sample rate
    // when omitted, as required by WaveSoundBuffer's decoder chain.
    void SetParameters(const std::array<double, 8> &values,
                       const std::array<bool, 8> &present);
    void Prepare(unsigned sampleRate, unsigned channels);
    void Reset();
    void Process(float *samples, std::size_t frames, unsigned channels);
};

} // namespace audio
} // namespace krkr
