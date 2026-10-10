#include <gtest/gtest.h>
#include "SystemLocale.h"
#include "WaveEffectDSP.h"
#include "TypicalDSP.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

using namespace krkr::audio;

namespace {
std::vector<float> Sine(unsigned rate, double frequency, std::size_t count) {
    std::vector<float> result(count);
    for (std::size_t i = 0; i < count; ++i)
        result[i] = static_cast<float>(0.5 * std::sin(2.0 * 3.14159265358979323846 * frequency * i / rate));
    return result;
}
double Energy(const std::vector<float> &values, std::size_t start = 0) {
    double total = 0;
    for (std::size_t i = start; i < values.size(); ++i) total += values[i] * values[i];
    return total;
}
}

TEST(SystemLocale, ReturnsEnglishNamesInsteadOfLanguageTags) {
    EXPECT_EQ("Japanese", krkr::EnglishLanguageName("ja_JP"));
    EXPECT_EQ("English", krkr::EnglishLanguageName("EN-us"));
    EXPECT_EQ("Korean", krkr::EnglishLanguageName("ko-KR"));
    EXPECT_EQ("French", krkr::EnglishLanguageName("fr_CA"));
    EXPECT_EQ("Catalan", krkr::EnglishLanguageName("ca-ES", "Catalan"));
}

TEST(SystemLocale, PreservesChineseScriptAndRegion) {
    EXPECT_EQ("Chinese (Traditional)", krkr::EnglishLanguageName("zh-Hant"));
    EXPECT_EQ("Chinese (Traditional)", krkr::EnglishLanguageName("zh_HK"));
    EXPECT_EQ("Chinese (Traditional)", krkr::EnglishLanguageName("zh-TW"));
    EXPECT_EQ("Chinese (Traditional)", krkr::EnglishLanguageName("zh-MO"));
    EXPECT_EQ("Chinese (Simplified)", krkr::EnglishLanguageName("zh-Hans-HK"));
    EXPECT_EQ("Chinese (Traditional)", krkr::EnglishLanguageName("zh-Hant-CN"));
    EXPECT_EQ("Chinese (Simplified)", krkr::EnglishLanguageName("zh_CN"));
    EXPECT_EQ("Chinese (Simplified)", krkr::EnglishLanguageName("zh", "Chinese"));
    EXPECT_EQ("Chinese (Simplified)", krkr::EnglishLanguageName("zh-SG"));
}

TEST(WaveEffects, GainLimitUsesDecibels) {
    GainLimitDSP gain;
    gain.Configure(-20.0 * std::log10(2.0), 1, 0);
    float values[] = {0.5f, -0.5f, 1.0f, -0.25f};
    gain.Process(values, 2, 2);
    EXPECT_FLOAT_EQ(0.25f, values[0]);
    EXPECT_FLOAT_EQ(-0.25f, values[1]);
    EXPECT_FLOAT_EQ(0.5f, values[2]);
    EXPECT_FLOAT_EQ(-0.125f, values[3]);
}

TEST(WaveEffects, GainLimitDefaultIsNeutralAndClippingPreservesSign) {
    GainLimitDSP gain;
    float neutral[] = {0.125f, -0.25f, 0.75f, -0.875f};
    const std::vector<float> original(neutral, neutral + 4);
    gain.Process(neutral, 4, 1);
    EXPECT_EQ(original, std::vector<float>(neutral, neutral + 4));
    gain.Configure(0, 0.5, -1);
    float clipped[] = {2.0f, -2.0f, 0.25f, -0.25f};
    gain.Process(clipped, 4, 1);
    EXPECT_FLOAT_EQ(0.5f, clipped[0]);
    EXPECT_FLOAT_EQ(-0.5f, clipped[1]);
    EXPECT_FLOAT_EQ(0.25f, clipped[2]);
    EXPECT_FLOAT_EQ(-0.25f, clipped[3]);
}

TEST(WaveEffects, SoftClippingIsContinuousAndBounded) {
    for (int mode = 0; mode <= 3; ++mode) {
        GainLimitDSP gain;
        gain.Configure(0, 0.75, mode);
        float values[] = {0.5f, 0.75f, 0.750001f, 2, -2};
        gain.Process(values, 5, 1);
        EXPECT_FLOAT_EQ(0.5f, values[0]);
        EXPECT_FLOAT_EQ(0.75f, values[1]);
        EXPECT_NEAR(0.75f, values[2], 0.000002f);
        EXPECT_GT(values[3], 0.75f);
        EXPECT_LT(values[3], 1.0f);
        EXPECT_FLOAT_EQ(values[3], -values[4]);
    }
}

TEST(WaveEffects, InvalidGainAndDelaySettingsAreRejected) {
    GainLimitDSP gain;
    EXPECT_THROW(gain.Configure(std::numeric_limits<double>::infinity(), 1, 0), std::invalid_argument);
    EXPECT_THROW(gain.Configure(0, -1, 0), std::invalid_argument);
    EXPECT_THROW(gain.Configure(0, 1, 99), std::invalid_argument);
    DelayDSP delay;
    EXPECT_THROW(delay.Configure(-1, 0.5, 1, 1000), std::invalid_argument);
    EXPECT_THROW(delay.Configure(1, 1, 1, 1000), std::invalid_argument);
    EXPECT_THROW(delay.Configure(1, 0.5, 1, 60001), std::invalid_argument);
    EXPECT_THROW(delay.Prepare(44100, 0), std::invalid_argument);
    EXPECT_THROW(delay.Prepare(44100, 3), std::invalid_argument);
}

TEST(WaveEffects, DelayProducesTimedFeedbackWithoutChannelLeakage) {
    DelayDSP delay;
    delay.Configure(3, 0.5, 1, 1000);
    delay.Prepare(1000, 2);
    std::vector<float> values(20, 0);
    values[0] = 1;
    delay.Process(values.data(), 10, 2);
    for (unsigned frame = 0; frame < 10; ++frame) {
        const float expected = frame % 3 == 0 ? std::pow(0.5f, frame / 3) : 0;
        EXPECT_FLOAT_EQ(expected, values[frame * 2]);
        EXPECT_FLOAT_EQ(0, values[frame * 2 + 1]);
    }
}

TEST(WaveEffects, DelayBlockBoundariesAndResetPreserveTiming) {
    DelayDSP whole, divided;
    for (auto *delay : {&whole, &divided}) {
        delay->Configure(5, 0.6, 0.4, 1000);
        delay->Prepare(1000, 1);
    }
    std::vector<float> first(70, 0), second;
    first[0] = 1;
    second = first;
    whole.Process(first.data(), first.size(), 1);
    divided.Process(second.data(), 7, 1);
    divided.Process(second.data() + 7, 13, 1);
    divided.Process(second.data() + 20, 50, 1);
    EXPECT_EQ(first, second);
    divided.Reset();
    std::fill(second.begin(), second.end(), 0);
    divided.Process(second.data(), second.size(), 1);
    EXPECT_DOUBLE_EQ(0, Energy(second));
}

TEST(WaveEffects, DelayTracksSampleRate) {
    DelayDSP delay;
    delay.Configure(10, 0.5, 1, 1000);
    delay.Prepare(2000, 1);
    std::vector<float> values(41, 0);
    values[0] = 1;
    delay.Process(values.data(), values.size(), 1);
    EXPECT_FLOAT_EQ(0, values[10]);
    EXPECT_FLOAT_EQ(0.5, values[20]);
    EXPECT_FLOAT_EQ(0.25, values[40]);
}

TEST(WaveEffects, DelayWrapsMatchAnUnboundedHistoryAcrossIrregularBlocks) {
    // The reference retains the entire history: it has no circular index and
    // checks wrapping independently of the production delay line.
    for (unsigned channels : {1u, 2u}) for (unsigned rate : {1000u, 44100u, 384000u}) {
        for (std::size_t length : {1u, 2u, 3u, 31u, 1009u}) {
            SCOPED_TRACE(std::to_string(rate) + "/" + std::to_string(channels) + "/" + std::to_string(length));
            const std::size_t frames = length * 5 + 17;
            std::vector<float> input(frames * channels), expected(input.size()), history(input.size());
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = float(int((i * 73 + 19) % 257) - 128) / 256.0f;
            for (std::size_t frame = 0; frame < frames; ++frame) for (unsigned channel = 0; channel < channels; ++channel) {
                const std::size_t at = frame * channels + channel;
                const float delayed = frame < length ? 0 : history[at - length * channels];
                expected[at] = static_cast<float>(input[at] + delayed * 0.7);
                history[at] = static_cast<float>((input[at] + delayed) * -0.625);
            }
            DelayDSP delay;
            // Keep rounding away from the integer boundary when converting
            // this intended frame length to milliseconds and back.
            delay.Configure((length + 0.125) * 1000.0 / rate, -0.625, 0.7, 1000);
            delay.Prepare(rate, channels);
            const auto original = input;
            for (unsigned repeat = 0; repeat < 2; ++repeat) {
                input = original;
                for (std::size_t at = 0; at < frames;) {
                    const std::size_t count = std::min(frames - at, 1 + (at * 17 + 7) % 53);
                    delay.Process(input.data() + at * channels, 0, channels);
                    delay.Process(input.data() + at * channels, count, channels);
                    at += count;
                }
                EXPECT_EQ(expected, input);
                delay.Reset();
            }
        }
    }
}

TEST(WaveEffects, FreeVerbDelayLinesPreserveExactHistoryAcrossChunksAndReset) {
    for (unsigned channels : {1u, 2u}) for (unsigned rate : {1000u, 11025u, 44100u, 48000u, 384000u}) {
        SCOPED_TRACE(std::to_string(rate) + "/" + std::to_string(channels));
        const std::size_t frames = rate / 3 + 127;
        std::vector<float> original(frames * channels);
        for (std::size_t i = 0; i < original.size(); ++i)
            original[i] = float(int((i * 131 + 71) % 509) - 254) / 512.0f;
        FreeVerbDSP whole, divided;
        for (auto *verb : {&whole, &divided}) {
            verb->SetRoom(0.83); verb->SetDamping(0.29);
            verb->SetMix(0.71); verb->SetWidth(0.37);
            verb->Prepare(rate, channels);
        }
        auto expected = original;
        whole.Process(expected.data(), frames, channels);
        EXPECT_NE(expected, original);
        for (unsigned repeat = 0; repeat < 2; ++repeat) {
            auto actual = original;
            for (std::size_t at = 0; at < frames;) {
                const std::size_t count = std::min(frames - at, 1 + (at * 37 + 13) % 521);
                divided.Process(actual.data() + at * channels, 0, channels);
                divided.Process(actual.data() + at * channels, count, channels);
                at += count;
            }
            EXPECT_EQ(expected, actual);
            divided.Reset();
        }
    }
}

TEST(WaveEffects, EqualizerNeutralBandsAreBitExact) {
    EqualizerDSP eq;
    eq.Prepare(22050, 1);
    const std::vector<float> original = Sine(22050, 1000, 1024);
    std::vector<float> values = original;
    eq.Process(values.data(), values.size(), 1);
    EXPECT_EQ(original, values);
    for (unsigned band = 0; band < 10; ++band) EXPECT_EQ(1, eq.GetGain(band));
}

TEST(WaveEffects, EqualizerGainIsLinearAndAffectsTheSelectedBand) {
    EqualizerDSP eq;
    eq.SetGain(5, 0.25); // 31.25 * 2^5 = 1000 Hz.
    eq.Prepare(44100, 1);
    const std::vector<float> original = Sine(44100, 1000, 8192);
    std::vector<float> values = original;
    eq.Process(values.data(), values.size(), 1);
    const double amplitudeRatio = std::sqrt(Energy(values, 4096) / Energy(original, 4096));
    EXPECT_NEAR(0.25, amplitudeRatio, 0.005);
    EXPECT_THROW(eq.SetGain(10, 1), std::out_of_range);
    EXPECT_THROW(eq.SetGain(0, -1), std::invalid_argument);
    EXPECT_THROW(eq.SetGain(0, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

TEST(WaveEffects, EqualizerResetAndChannelsAreIndependent) {
    EqualizerDSP eq, quiet;
    for (auto *filter : {&eq, &quiet}) {
        filter->SetGain(5, 2);
        filter->Prepare(44100, 2);
    }
    // DSPFilters injects a tiny alternating anti-denormal signal even on a
    // silent channel. Compare independent instances at the same sample count.
    // An odd count also exercises its retained phase across reset.
    std::vector<float> values(513 * 2, 0), silence(values.size(), 0);
    for (std::size_t i = 0; i < values.size(); i += 2) values[i] = 0.5f;
    eq.Process(values.data(), values.size() / 2, 2);
    quiet.Process(silence.data(), silence.size() / 2, 2);
    for (std::size_t i = 1; i < values.size(); i += 2)
        ASSERT_FLOAT_EQ(silence[i], values[i]) << "right channel sample " << i / 2;
    // The driven channel still has history when reset occurs. It must then
    // match the silent instance, including the library's background signal.
    eq.Reset();
    quiet.Reset();
    std::fill(values.begin(), values.end(), 0);
    std::fill(silence.begin(), silence.end(), 0);
    eq.Process(values.data(), values.size() / 2, 2);
    quiet.Process(silence.data(), silence.size() / 2, 2);
    EXPECT_EQ(silence, values);
}

TEST(WaveEffects, FreeVerbHasDryAndWetImpulseResponses) {
    FreeVerbDSP verb;
    verb.SetMix(0);
    verb.Prepare(44100, 1);
    std::vector<float> dry = Sine(44100, 450, 128), original = dry;
    verb.Process(dry.data(), dry.size(), 1);
    EXPECT_EQ(original, dry);
    verb.Reset();
    verb.SetMix(1);
    std::vector<float> wet(5000, 0);
    wet[0] = 0.5f;
    verb.Process(wet.data(), wet.size(), 1);
    for (std::size_t i = 0; i < 1116; ++i) EXPECT_FLOAT_EQ(0, wet[i]);
    EXPECT_NEAR(0.015, wet[1116], 1.0e-7);
    EXPECT_GT(Energy(wet, 1116), 0.001);
    verb.Reset();
    std::fill(wet.begin(), wet.end(), 0);
    verb.Process(wet.data(), wet.size(), 1);
    EXPECT_DOUBLE_EQ(0, Energy(wet));
}

TEST(WaveEffects, FreeVerbRateTuningIsPerInstance) {
    FreeVerbDSP highRate, lowRate;
    highRate.SetMix(1);
    highRate.Prepare(48000, 1);
    lowRate.SetMix(1);
    lowRate.Prepare(22050, 1);
    std::vector<float> values(800, 0);
    values[0] = 0.5f;
    lowRate.Process(values.data(), values.size(), 1);
    for (std::size_t i = 0; i < 558; ++i) EXPECT_FLOAT_EQ(0, values[i]);
    EXPECT_NEAR(0.015, values[558], 1.0e-7);
}

TEST(WaveEffects, FreeVerbFreezeDoesNotAdmitNewInput) {
    FreeVerbDSP verb;
    verb.SetMix(1);
    verb.SetFrozen(true);
    verb.Prepare(44100, 2);
    std::vector<float> values(6000, 0.5f);
    verb.Process(values.data(), values.size() / 2, 2);
    EXPECT_DOUBLE_EQ(0, Energy(values));
    EXPECT_THROW(verb.SetRoom(1.01), std::invalid_argument);
    EXPECT_THROW(verb.SetDamping(std::numeric_limits<double>::infinity()), std::invalid_argument);
}

TEST(TypicalWaveDSP, OriginalDesignFamiliesProcessPCM) {
    for (const char *family : {"RBJ", "Butterworth", "ChebyshevI", "ChebyshevII",
                              "Bessel", "Elliptic", "Legendre"}) {
        SCOPED_TRACE(family);
        TypicalDSP dsp("LowPass", family), quiet("LowPass", family);
        dsp.Prepare(44100, 2);
        quiet.Prepare(44100, 2);
        std::vector<float> values(1024, 0), silence(values.size(), 0);
        values[0] = 1;
        dsp.Process(values.data(), values.size() / 2, 2);
        quiet.Process(silence.data(), silence.size() / 2, 2);
        EXPECT_GT(Energy(values), 1.0e-8);
        EXPECT_LT(std::abs(values[0]), 0.9f);
        for (std::size_t i = 0; i < values.size(); ++i) {
            ASSERT_TRUE(std::isfinite(values[i])) << i;
            // Isolate leakage from DSPFilters' intentional anti-denormal input.
            if (i % 2) ASSERT_FLOAT_EQ(silence[i], values[i]) << i;
        }
    }
}

TEST(TypicalWaveDSP, HighPassRemovesDCWhileLowPassPassesDC) {
    TypicalDSP low("LowPass"), high("HighPass");
    low.Prepare(44100, 1);
    high.Prepare(44100, 1);
    std::vector<float> passed(4096, 0.5f), rejected = passed;
    low.Process(passed.data(), passed.size(), 1);
    high.Process(rejected.data(), rejected.size(), 1);
    EXPECT_NEAR(0.5, passed.back(), 1.0e-6);
    EXPECT_LT(Energy(rejected, 2048), 1.0e-9);
}

TEST(TypicalWaveDSP, BlockSplitsAndStateFormsAgree) {
    const std::vector<float> input = Sine(44100, 1700, 2048);
    std::vector<float> reference = input;
    TypicalDSP baseline("LowPass", "Butterworth");
    baseline.Prepare(44100, 1);
    baseline.Process(reference.data(), reference.size(), 1);
    for (const char *state : {"DirectFormI", "DirectForm2", "TransposedDirectFormI",
                             "TransposedDirectForm2"}) {
        SCOPED_TRACE(state);
        TypicalDSP dsp("LowPass", "Butterworth", state);
        dsp.Prepare(44100, 1);
        std::vector<float> values = input;
        dsp.Process(values.data(), 17, 1);
        dsp.Process(values.data() + 17, values.size() - 17, 1);
        for (std::size_t i = 0; i < values.size(); ++i)
            EXPECT_NEAR(reference[i], values[i], 1.0e-6);
    }
}

TEST(TypicalWaveDSP, AllSupportedResponsesProduceFiniteSamples) {
    const std::vector<std::pair<std::string, std::vector<std::string>>> families = {
        {"RBJ", {"LowPass", "HighPass", "BandPass", "BandPass1", "BandPass2", "BandStop",
                 "LowShelf", "HighShelf", "BandShelf", "AllPass"}},
        {"Butterworth", {"LowPass", "HighPass", "BandPass", "BandStop", "LowShelf", "HighShelf", "BandShelf"}},
        {"Chebyshev1", {"LowPass", "HighPass", "BandPass", "BandStop", "LowShelf", "HighShelf", "BandShelf"}},
        {"Chebyshev2", {"LowPass", "HighPass", "BandPass", "BandStop", "LowShelf", "HighShelf", "BandShelf"}},
        {"Bessel", {"LowPass", "HighPass", "BandPass", "BandStop", "LowShelf"}},
        {"Elliptic", {"LowPass", "HighPass", "BandPass", "BandStop"}},
        {"Legendre", {"LowPass", "HighPass", "BandPass", "BandStop"}},
        {"Custom", {"OnePole", "TwoPole"}}
    };
    for (const auto &family : families) for (const auto &response : family.second) {
        SCOPED_TRACE(family.first + "/" + response);
        TypicalDSP dsp(response, family.first);
        dsp.Prepare(44100, 1);
        std::vector<float> values(1024, 0);
        values[0] = 0.5f;
        dsp.Process(values.data(), values.size(), 1);
        for (float value : values) ASSERT_TRUE(std::isfinite(value));
        dsp.Reset();
        std::fill(values.begin(), values.end(), 0);
        dsp.Process(values.data(), values.size(), 1);
        EXPECT_LT(Energy(values), 1.0e-12);
    }
}

TEST(TypicalWaveDSP, MissingParametersRestoreDesignDefaultsAndTrackSourceRate) {
    TypicalDSP dsp("LowPass", "Butterworth");
    EXPECT_EQ(3, dsp.ParameterCount());
    EXPECT_EQ(Dsp::idOrder, dsp.ParameterInfo(1).getId());
    std::array<double, 8> values{};
    std::array<bool, 8> present{};
    values[1] = 4;
    values[2] = 1200;
    present[1] = present[2] = true;
    dsp.SetParameters(values, present);
    EXPECT_EQ(4, dsp.CurrentValue(1));
    EXPECT_EQ(1200, dsp.CurrentValue(2));
    dsp.Prepare(22050, 1);
    EXPECT_EQ(22050, dsp.CurrentValue(0));
    present.fill(false);
    dsp.SetParameters(values, present);
    EXPECT_EQ(dsp.ParameterInfo(1).getDefaultValue(), dsp.CurrentValue(1));
    EXPECT_EQ(dsp.ParameterInfo(2).getDefaultValue(), dsp.CurrentValue(2));
    EXPECT_EQ(22050, dsp.CurrentValue(0));
}

TEST(TypicalWaveDSP, InvalidDesignsAndNonFiniteParametersFailExplicitly) {
    EXPECT_THROW(TypicalDSP("LowPass", "unknown"), std::invalid_argument);
    EXPECT_THROW(TypicalDSP("HighShelf", "Elliptic"), std::invalid_argument);
    EXPECT_THROW(TypicalDSP("LowPass", "RBJ", "unknown"), std::invalid_argument);
    TypicalDSP dsp;
    std::array<double, 8> values{};
    std::array<bool, 8> present{};
    values[1] = std::numeric_limits<double>::quiet_NaN();
    present[1] = true;
    EXPECT_THROW(dsp.SetParameters(values, present), std::invalid_argument);
    EXPECT_THROW(dsp.ParameterInfo(8), std::out_of_range);
    EXPECT_THROW(dsp.Prepare(0, 1), std::invalid_argument);
}

TEST(TypicalWaveDSP, RBJShelvesStayFiniteAtHighGainAndNearNyquist) {
    for (const char *response : {"LowShelf", "HighShelf", "BandShelf"}) {
        for (double gain : {-24.0, 24.0}) {
            for (const char *state : {"DirectFormI", "DirectFormII",
                                       "TransposedDirectFormI", "TransposedDirectFormII"}) {
                SCOPED_TRACE(std::string(response) + " " + state + " " + std::to_string(gain));
                TypicalDSP dsp(response, "RBJ", state);
                std::array<double, 8> values{{44100, 22040, gain, 16}};
                std::array<bool, 8> present{{true, true, true, true}};
                dsp.SetParameters(values, present);
                dsp.Prepare(44100, 1);
                std::vector<float> samples(4096, 0);
                samples[0] = 1;
                dsp.Process(samples.data(), samples.size(), 1);
                for (float value : samples) ASSERT_TRUE(std::isfinite(value));
                EXPECT_GT(Energy(samples), 0);
                EXPECT_LT(Energy(samples), 10000);
            }
        }
    }
}
