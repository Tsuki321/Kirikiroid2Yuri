#pragma once

#include "DspFilters/RBJ.h"
#include "DspFilters/State.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace krkr {
namespace audio {

inline double CheckedRange(double value, double minimum, double maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum)
        throw std::invalid_argument("Wave filter parameter is outside its supported range");
    return value;
}

inline void CheckFormat(unsigned sampleRate, unsigned channels) {
    if (sampleRate < 1000 || sampleRate > 384000 || channels < 1 || channels > 2)
        throw std::invalid_argument("Wave filters require mono or stereo PCM at 1000..384000 Hz");
}

// GainLimit(gainDb = 0, threshold = 1, mode = 0). Modes 0..3 soften the
// portion above threshold; -1 selects hard clipping. A threshold of one
// leaves normal, unclipped samples unchanged.
class GainLimitDSP {
    double gain_ = 1.0;
    double threshold_ = 1.0;
    int mode_ = 0;

    double Shape(double value) const {
        switch (mode_) {
            case 0: return value / std::sqrt(1.0 + value * value);
            case 1: return std::tanh(value);
            case 2: return value / (1.0 + std::abs(value));
            default: return std::atan(value) * (2.0 / 3.14159265358979323846);
        }
    }
public:
    void Configure(double gainDb, double threshold, int mode) {
        CheckedRange(gainDb, -120.0, 120.0);
        CheckedRange(threshold, 0.0, 1.0);
        if (mode < -1 || mode > 3) throw std::invalid_argument("Unknown GainLimit mode");
        gain_ = std::pow(10.0, gainDb / 20.0);
        threshold_ = threshold;
        mode_ = mode;
    }
    void Reset() {}
    void Process(float *samples, std::size_t frames, unsigned channels) {
        for (std::size_t n = 0; n < frames * channels; ++n) {
            double value = samples[n] * gain_;
            if (mode_ < 0) {
                value = std::max(-threshold_, std::min(threshold_, value));
            } else if (threshold_ < 1.0 && std::abs(value) > threshold_) {
                const double headroom = 1.0 - threshold_;
                value = std::copysign(threshold_ +
                    headroom * Shape((std::abs(value) - threshold_) / headroom), value);
            }
            samples[n] = static_cast<float>(value);
        }
    }
};

// The original WaveDelay recurrence is deliberately preserved:
// stored[n] = (input[n] + stored[n-delay]) * feedback
// output[n] = input[n] + stored[n-delay] * wet.
// The second init argument is feedback, not a low-pass damping pole.
class DelayDSP {
    double delayMs_ = 0, feedback_ = 0, wet_ = 1;
    unsigned maximumMs_ = 1000, rate_ = 0, channels_ = 0;
    std::size_t position_ = 0, delayFrames_ = 1;
    std::vector<float> line_;
public:
    void Configure(double delayMs, double feedback, double wet, unsigned maximumMs) {
        CheckedRange(delayMs, 0, 60000);
        CheckedRange(feedback, -0.999999, 0.999999);
        CheckedRange(wet, -16, 16);
        if (maximumMs > 60000)
            throw std::invalid_argument("Delay buffer is limited to 60 seconds");
        delayMs_ = delayMs;
        feedback_ = feedback;
        wet_ = wet;
        maximumMs_ = maximumMs;
        if (rate_) Prepare(rate_, channels_);
    }
    void Prepare(unsigned sampleRate, unsigned channels) {
        CheckFormat(sampleRate, channels);
        rate_ = sampleRate;
        channels_ = channels;
        // A zero-time delay reads the previous ring cycle in the DLL. Use the
        // configured buffer duration in that case rather than a feedback loop
        // with no sample delay.
        const double milliseconds = delayMs_ > 0 ? delayMs_ : maximumMs_;
        delayFrames_ = std::max<std::size_t>(1,
            static_cast<std::size_t>(milliseconds * sampleRate / 1000.0));
        line_.assign(delayFrames_ * channels, 0.0f);
        position_ = 0;
    }
    void Reset() {
        std::fill(line_.begin(), line_.end(), 0.0f);
        position_ = 0;
    }
    void Process(float *samples, std::size_t frames, unsigned channels) {
        if (line_.empty() || channels != channels_)
            throw std::logic_error("Delay filter has no matching PCM format");
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (unsigned channel = 0; channel < channels; ++channel) {
                const std::size_t index = frame * channels + channel;
                float &delayed = line_[position_ * channels + channel];
                const float input = samples[index];
                samples[index] = static_cast<float>(input + delayed * wet_);
                delayed = static_cast<float>((input + delayed) * feedback_);
            }
            position_ = (position_ + 1) % delayFrames_;
        }
    }
};

// Ten linear-gain octave bands, 31.25 Hz through 16 kHz. Gains of one are
// neutral; gains are converted to dB only for the RBJ coefficient calculation.
class EqualizerDSP {
    typedef Dsp::BiquadBase::State<Dsp::DirectFormII> State;
    std::array<double, 10> gains_;
    std::array<Dsp::RBJ::BandShelf, 10> filters_;
    std::array<std::array<State, 2>, 10> states_;
    unsigned rate_ = 0;
    unsigned channels_ = 0;
    void Design(unsigned band) {
        const double center = 31.25 * (1u << band);
        if (rate_ && center < rate_ * 0.49) {
            const double gain = std::max(1.0e-6, gains_[band]);
            filters_[band].setup(rate_, center, 20.0 * std::log10(gain), 1.0);
        }
    }
public:
    EqualizerDSP() { gains_.fill(1.0); }
    double GetGain(unsigned band) const {
        if (band >= gains_.size()) throw std::out_of_range("Equalizer band must be 0..9");
        return gains_[band];
    }
    void SetGain(unsigned band, double gain) {
        if (band >= gains_.size()) throw std::out_of_range("Equalizer band must be 0..9");
        CheckedRange(gain, 0, 64);
        gains_[band] = gain;
        Design(band);
    }
    void Prepare(unsigned sampleRate, unsigned channels) {
        CheckFormat(sampleRate, channels);
        rate_ = sampleRate;
        channels_ = channels;
        for (unsigned band = 0; band < gains_.size(); ++band) Design(band);
        Reset();
    }
    void Reset() {
        for (auto &band : states_) for (auto &state : band) state.reset();
    }
    void Process(float *samples, std::size_t frames, unsigned channels) {
        if (!rate_ || channels != channels_)
            throw std::logic_error("Equalizer has no matching PCM format");
        for (unsigned band = 0; band < gains_.size(); ++band) {
            // Avoid loss of precision and denormal noise for neutral or
            // above-Nyquist bands, which must be transparent.
            if (gains_[band] == 1.0 || 31.25 * (1u << band) >= rate_ * 0.49) continue;
            for (std::size_t frame = 0; frame < frames; ++frame)
                for (unsigned channel = 0; channel < channels; ++channel) {
                    float &sample = samples[frame * channels + channel];
                    sample = states_[band][channel].process(sample, filters_[band]);
                }
        }
    }
};

// Jezar's FreeVerb topology with the STK defaults and parameter scaling.
// The STK adaptation's license is retained in thirdparty/stk-LICENSE.txt.
// Buffers belong to each instance; sample-rate changes never mutate shared
// tuning constants (which would detune subsequently created effects).
class FreeVerbDSP {
    struct DelayLine {
        std::vector<float> data;
        std::size_t position = 0;
        float lowpass = 0;
        void Resize(std::size_t size) {
            data.assign(std::max<std::size_t>(size, 1), 0);
            position = 0;
            lowpass = 0;
        }
        void Reset() {
            std::fill(data.begin(), data.end(), 0);
            position = 0;
            lowpass = 0;
        }
        float Comb(float input, float feedback, float damping) {
            const float output = data[position];
            lowpass = output * (1.0f - damping) + lowpass * damping;
            data[position] = input + lowpass * feedback;
            position = (position + 1) % data.size();
            return output;
        }
        float Allpass(float input) {
            const float delayed = data[position];
            data[position] = input + 0.5f * delayed;
            position = (position + 1) % data.size();
            return delayed - input;
        }
    };
    std::array<std::array<DelayLine, 8>, 2> combs_;
    std::array<std::array<DelayLine, 4>, 2> allpasses_;
    double mix_ = 0.75, room_ = 0.75, damping_ = 0.25, width_ = 1.0;
    bool frozen_ = false;
    unsigned rate_ = 0, channels_ = 0;
public:
    double Mix() const { return mix_; }
    double Room() const { return room_; }
    double Damping() const { return damping_; }
    double Width() const { return width_; }
    bool Frozen() const { return frozen_; }
    void SetMix(double value) { mix_ = CheckedRange(value, 0, 1); }
    void SetRoom(double value) { room_ = CheckedRange(value, 0, 1); }
    void SetDamping(double value) { damping_ = CheckedRange(value, 0, 1); }
    void SetWidth(double value) { width_ = CheckedRange(value, 0, 1); }
    void SetFrozen(bool value) { frozen_ = value; }
    void Prepare(unsigned sampleRate, unsigned channels) {
        CheckFormat(sampleRate, channels);
        rate_ = sampleRate;
        channels_ = channels;
        static const unsigned combLengths[8] = {1617, 1557, 1491, 1422, 1356, 1277, 1188, 1116};
        static const unsigned allpassLengths[4] = {225, 556, 441, 341};
        for (unsigned channel = 0; channel < 2; ++channel) {
            for (unsigned i = 0; i < 8; ++i)
                combs_[channel][i].Resize(static_cast<std::size_t>(
                    combLengths[i] * sampleRate / 44100.0) + channel * 23);
            for (unsigned i = 0; i < 4; ++i)
                allpasses_[channel][i].Resize(static_cast<std::size_t>(
                    allpassLengths[i] * sampleRate / 44100.0) + channel * 23);
        }
    }
    void Reset() {
        for (auto &channel : combs_) for (auto &line : channel) line.Reset();
        for (auto &channel : allpasses_) for (auto &line : channel) line.Reset();
    }
    void Process(float *samples, std::size_t frames, unsigned channels) {
        if (!rate_ || channels != channels_)
            throw std::logic_error("FreeVerb has no matching PCM format");
        const float feedback = frozen_ ? 1.0f : static_cast<float>(room_ * 0.28 + 0.7);
        const float damping = frozen_ ? 0.0f : static_cast<float>(damping_ * 0.4);
        const double wet = 3.0 * mix_, dry = 2.0 * (1.0 - mix_);
        const double normalizer = wet + dry;
        const float dryGain = static_cast<float>(dry / normalizer);
        const float wetSame = static_cast<float>((wet / normalizer) * (width_ * 0.5 + 0.5));
        const float wetOther = static_cast<float>((wet / normalizer) * ((1.0 - width_) * 0.5));
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const float left = samples[frame * channels];
            const float right = channels == 2 ? samples[frame * channels + 1] : left;
            const float input = frozen_ ? 0 : (left + right) * 0.015f;
            float output[2] = {0, 0};
            for (unsigned channel = 0; channel < 2; ++channel) {
                for (auto &line : combs_[channel]) output[channel] += line.Comb(input, feedback, damping);
                for (auto &line : allpasses_[channel]) output[channel] = line.Allpass(output[channel]);
            }
            samples[frame * channels] = dryGain * left + wetSame * output[0] + wetOther * output[1];
            if (channels == 2)
                samples[frame * channels + 1] = dryGain * right + wetSame * output[1] + wetOther * output[0];
        }
    }
};

} // namespace audio
} // namespace krkr
