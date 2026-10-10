// Built only by script/benchmark_wave_effects.py on GitHub Actions. The baseline
// header is read from git, so equality checks exercise the actual old code.
#include "WaveEffectDSP_baseline.h"
#include "WaveEffectDSP.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>

namespace {
std::vector<float> Signal(std::size_t size) {
    std::vector<float> result(size);
    uint32_t random = 0x414c5048;
    for (auto &sample : result) {
        random = random * 1664525u + 1013904223u;
        sample = (int(random >> 16) - 32768) / 65536.0f;
    }
    return result;
}

template <class DSP> DSP Verb(unsigned rate, unsigned channels) {
    DSP dsp;
    dsp.SetRoom(0.83); dsp.SetDamping(0.29);
    dsp.SetMix(0.71); dsp.SetWidth(0.37);
    dsp.Prepare(rate, channels);
    return dsp;
}

template <class DSP> DSP Delay(unsigned rate, unsigned channels, double milliseconds) {
    DSP dsp;
    dsp.Configure(milliseconds, -0.625, 0.7, 1000);
    dsp.Prepare(rate, channels);
    return dsp;
}

template <class DSP> void IrregularBlocks(DSP &dsp, std::vector<float> &samples, unsigned channels) {
    const std::size_t frames = samples.size() / channels;
    for (std::size_t at = 0; at < frames;) {
        const std::size_t count = std::min(frames - at, 1 + (at * 37 + 13) % 521);
        dsp.Process(samples.data() + at * channels, 0, channels);
        dsp.Process(samples.data() + at * channels, count, channels);
        at += count;
    }
}

template <class Before, class After>
void Verify(Before &before, After &after, const std::vector<float> &signal, unsigned channels) {
    auto expected = signal, actual = signal;
    IrregularBlocks(before, expected, channels);
    IrregularBlocks(after, actual, channels);
    if (std::memcmp(expected.data(), actual.data(), actual.size() * sizeof(float)))
        throw std::runtime_error("Wave DSP differs from the baseline");
    for (float value : actual)
        if (!std::isfinite(value)) throw std::runtime_error("Wave DSP produced a nonfinite sample");
}

void CheckCompatibility() {
    for (unsigned rate : {1000u, 11025u, 44100u, 48000u, 384000u}) for (unsigned channels : {1u, 2u}) {
        const auto signal = Signal((rate / 3 + 127) * channels);
        auto before = Verb<krkr_before::audio::FreeVerbDSP>(rate, channels);
        auto after = Verb<krkr::audio::FreeVerbDSP>(rate, channels);
        Verify(before, after, signal, channels);
        before.SetFrozen(true); after.SetFrozen(true);
        Verify(before, after, signal, channels);
        before.Reset(); after.Reset();
        Verify(before, after, signal, channels);
        for (double milliseconds : {0.0, 0.125, 1.0, 3.0, 31.75}) {
            auto oldDelay = Delay<krkr_before::audio::DelayDSP>(rate, channels, milliseconds);
            auto newDelay = Delay<krkr::audio::DelayDSP>(rate, channels, milliseconds);
            Verify(oldDelay, newDelay, signal, channels);
            oldDelay.Reset(); newDelay.Reset();
            Verify(oldDelay, newDelay, signal, channels);
        }
    }
}

template <class DSP> double Measure(DSP &dsp, const std::vector<float> &signal, double &checksum) {
    dsp.Reset();
    auto samples = signal;
    const std::size_t frames = samples.size() / 2;
    const auto start = std::chrono::steady_clock::now();
    for (std::size_t at = 0; at < frames; at += 512)
        dsp.Process(samples.data() + at * 2, std::min<std::size_t>(512, frames - at), 2);
    const auto end = std::chrono::steady_clock::now();
    for (float value : samples) checksum += value;
    return std::chrono::duration<double, std::nano>(end - start).count() / frames;
}

template <class Before, class After>
void Benchmark(const char *name, Before &before, After &after, unsigned rate, bool comma) {
    const auto signal = Signal(rate * 8 * 2);
    std::vector<double> oldTimes, newTimes;
    double checksum = 0;
    for (unsigned round = 0; round < 8; ++round) {
        // Warm up both implementations, then alternate which one runs first.
        double oldTime, newTime;
        if (round % 2) {
            newTime = Measure(after, signal, checksum);
            oldTime = Measure(before, signal, checksum);
        } else {
            oldTime = Measure(before, signal, checksum);
            newTime = Measure(after, signal, checksum);
        }
        if (round) { oldTimes.push_back(oldTime); newTimes.push_back(newTime); }
    }
    std::sort(oldTimes.begin(), oldTimes.end());
    std::sort(newTimes.begin(), newTimes.end());
    const double previous = oldTimes[oldTimes.size() / 2], current = newTimes[newTimes.size() / 2];
    if (comma) std::cout << ",\n";
    std::cout << "    {\"effect\":\"" << name << "\",\"sample_rate\":" << rate
              << ",\"channels\":2,\"baseline_ns_per_frame\":" << previous
              << ",\"current_ns_per_frame\":" << current << ",\"speedup\":" << previous / current
              << ",\"checksum\":" << checksum << "}";
}
}

int main() {
    try {
        CheckCompatibility();
        std::cout << std::setprecision(9) << "{\n  \"bit_exact\":true,\n  \"benchmarks\":[\n";
        bool comma = false;
        for (unsigned rate : {44100u, 48000u}) {
            auto oldVerb = Verb<krkr_before::audio::FreeVerbDSP>(rate, 2);
            auto newVerb = Verb<krkr::audio::FreeVerbDSP>(rate, 2);
            Benchmark("FreeVerb", oldVerb, newVerb, rate, comma);
            auto oldDelay = Delay<krkr_before::audio::DelayDSP>(rate, 2, 31.75);
            auto newDelay = Delay<krkr::audio::DelayDSP>(rate, 2, 31.75);
            Benchmark("Delay", oldDelay, newDelay, rate, true);
            comma = true;
        }
        std::cout << "\n  ]\n}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
