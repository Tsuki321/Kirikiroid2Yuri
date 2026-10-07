#include "TypicalDSP.h"
#include "WaveEffectDSP.h"
#include "DspFilters/Dsp.h"
#include <cctype>

namespace krkr {
namespace audio {
namespace {
std::string Key(std::string value) {
    std::string result;
    for (unsigned char c : value)
        if (c != ' ' && c != '_' && c != '-')
            result += static_cast<char>(std::tolower(c));
    return result;
}

template<class Design, class State>
std::unique_ptr<Dsp::Filter> MakeFilter() {
    return std::unique_ptr<Dsp::Filter>(new Dsp::FilterDesign<Design, 2, State>());
}

template<class State>
std::unique_ptr<Dsp::Filter> SelectFilter(const std::string &response, const std::string &family) {
    const int maxOrder = 50;
    if (family == "rbj") {
        if (response == "lowpass") return MakeFilter<Dsp::RBJ::Design::LowPass, State>();
        if (response == "highpass") return MakeFilter<Dsp::RBJ::Design::HighPass, State>();
        if (response == "bandpass" || response == "bandpass1")
            return MakeFilter<Dsp::RBJ::Design::BandPass1, State>();
        if (response == "bandpass2") return MakeFilter<Dsp::RBJ::Design::BandPass2, State>();
        if (response == "bandstop") return MakeFilter<Dsp::RBJ::Design::BandStop, State>();
        if (response == "lowshelf") return MakeFilter<Dsp::RBJ::Design::LowShelf, State>();
        if (response == "highshelf") return MakeFilter<Dsp::RBJ::Design::HighShelf, State>();
        if (response == "bandshelf") return MakeFilter<Dsp::RBJ::Design::BandShelf, State>();
        if (response == "allpass") return MakeFilter<Dsp::RBJ::Design::AllPass, State>();
    }
#define KRKR_PASS_FILTERS(Family) \
    if (response == "lowpass") return MakeFilter<Dsp::Family::Design::LowPass<maxOrder>, State>(); \
    if (response == "highpass") return MakeFilter<Dsp::Family::Design::HighPass<maxOrder>, State>(); \
    if (response == "bandpass") return MakeFilter<Dsp::Family::Design::BandPass<maxOrder>, State>(); \
    if (response == "bandstop") return MakeFilter<Dsp::Family::Design::BandStop<maxOrder>, State>();
#define KRKR_SHELF_FILTERS(Family) \
    if (response == "lowshelf") return MakeFilter<Dsp::Family::Design::LowShelf<maxOrder>, State>(); \
    if (response == "highshelf") return MakeFilter<Dsp::Family::Design::HighShelf<maxOrder>, State>(); \
    if (response == "bandshelf") return MakeFilter<Dsp::Family::Design::BandShelf<maxOrder>, State>();
    if (family == "butterworth") {
        KRKR_PASS_FILTERS(Butterworth)
        KRKR_SHELF_FILTERS(Butterworth)
    }
    if (family == "chebyshevi" || family == "chebyshev1") {
        KRKR_PASS_FILTERS(ChebyshevI)
        KRKR_SHELF_FILTERS(ChebyshevI)
    }
    if (family == "chebyshevii" || family == "chebyshev2") {
        KRKR_PASS_FILTERS(ChebyshevII)
        KRKR_SHELF_FILTERS(ChebyshevII)
    }
    if (family == "bessel") {
        KRKR_PASS_FILTERS(Bessel)
        if (response == "lowshelf") return MakeFilter<Dsp::Bessel::Design::LowShelf<maxOrder>, State>();
    }
    if (family == "elliptic") { KRKR_PASS_FILTERS(Elliptic) }
    if (family == "legendre") { KRKR_PASS_FILTERS(Legendre) }
#undef KRKR_PASS_FILTERS
#undef KRKR_SHELF_FILTERS
    if (family == "custom") {
        if (response == "onepole") return MakeFilter<Dsp::Custom::Design::OnePole, State>();
        if (response == "twopole") return MakeFilter<Dsp::Custom::Design::TwoPole, State>();
    }
    throw std::invalid_argument("Unsupported WaveDSPFilter response/design pair");
}

std::unique_ptr<Dsp::Filter> CreateFilter(const std::string &response,
                                        const std::string &family,
                                        const std::string &state) {
    if (state == "directformi" || state == "directform1")
        return SelectFilter<Dsp::DirectFormI>(response, family);
    if (state == "directformii" || state == "directform2")
        return SelectFilter<Dsp::DirectFormII>(response, family);
    if (state == "transposeddirectformi" || state == "transposeddirectform1")
        return SelectFilter<Dsp::TransposedDirectFormI>(response, family);
    if (state == "transposeddirectformii" || state == "transposeddirectform2")
        return SelectFilter<Dsp::TransposedDirectFormII>(response, family);
    throw std::invalid_argument("Unsupported WaveDSPFilter state form");
}
}

TypicalDSP::TypicalDSP(const std::string &response, const std::string &family,
                       const std::string &state)
    : filter_(CreateFilter(Key(response), Key(family), Key(state))) {
    requested_ = filter_->getDefaultParams();
    ApplyParameters();
}
TypicalDSP::~TypicalDSP() {}
int TypicalDSP::ParameterCount() const { return filter_->getNumParams(); }
Dsp::ParamInfo TypicalDSP::ParameterInfo(unsigned index) const {
    if (index >= static_cast<unsigned>(ParameterCount()))
        throw std::out_of_range("WaveDSPFilter parameter index is out of range");
    return filter_->getParamInfo(index);
}
double TypicalDSP::CurrentValue(unsigned index) const {
    ParameterInfo(index);
    return filter_->getParam(index);
}
std::string TypicalDSP::Name() const { return filter_->getName(); }

void TypicalDSP::ApplyParameters() {
    Dsp::Params applied = requested_;
    if (automaticRate_ && streamRate_) applied[0] = streamRate_;
    CheckedRange(applied[0], 1000, 384000);
    for (int i = 1; i < ParameterCount(); ++i) {
        const Dsp::ParamInfo info = filter_->getParamInfo(i);
        if (!std::isfinite(applied[i]))
            throw std::invalid_argument("Non-finite WaveDSPFilter parameter");
        // Respect the library's real native ranges, including its logarithmic
        // Q/bandwidth transforms. Stay below Nyquist when the source rate is
        // lower than the design's 44.1 kHz default.
        double minimum = info.toNativeValue(0), maximum = info.toNativeValue(1);
        if (info.getId() == Dsp::idFrequency || info.getId() == Dsp::idBandwidthHz)
            maximum = std::min(maximum, applied[0] * 0.499);
        applied[i] = std::max(minimum, std::min(maximum, applied[i]));
        if (info.getId() == Dsp::idOrder)
            applied[i] = static_cast<int>(applied[i]);
    }
    // Individual metadata ranges do not cover every combination: steep RBJ
    // shelf slopes at high gain otherwise take sqrt of a negative number.
    const int slope = filter_->findParamId(Dsp::idSlope);
    const int gain = filter_->findParamId(Dsp::idGain);
    if (slope >= 0 && gain >= 0) {
        const double a = std::pow(10.0, applied[gain] / 40.0);
        const double sum = a + 1.0 / a;
        applied[slope] = std::min(applied[slope], sum / (sum - 2.0 + 1.0e-6));
    }
    // RBJ BandShelf uses sinh(log(2)/2 * bandwidth * omega/sin(omega)).
    // Near Nyquist a legal octave width can overflow. Bound the exponent,
    // retaining headroom for finite, nondegenerate biquad coefficients.
    const int bandwidth = filter_->findParamId(Dsp::idBandwidth);
    const int frequency = filter_->findParamId(Dsp::idFrequency);
    if (filter_->getKind() == Dsp::kindBandShelf && bandwidth >= 0 && frequency >= 0) {
        const double omega = 2.0 * Dsp::doublePi * applied[frequency] / applied[0];
        const double maximum = 20.0 * std::sin(omega) / (0.5 * std::log(2.0) * omega);
        applied[bandwidth] = std::min(applied[bandwidth], maximum);
    }
    filter_->setParams(applied);
}
void TypicalDSP::SetParameters(const std::array<double, 8> &values,
                               const std::array<bool, 8> &present) {
    Dsp::Params next = filter_->getDefaultParams();
    for (int i = 0; i < ParameterCount(); ++i) {
        if (present[i]) {
            if (!std::isfinite(values[i]))
                throw std::invalid_argument("Non-finite WaveDSPFilter parameter");
            next[i] = values[i];
        }
    }
    CheckedRange(next[0], 1000, 384000);
    requested_ = next;
    automaticRate_ = !present[0];
    ApplyParameters();
}
void TypicalDSP::Prepare(unsigned sampleRate, unsigned channels) {
    CheckFormat(sampleRate, channels);
    streamRate_ = sampleRate;
    channels_ = channels;
    ApplyParameters();
    Reset();
}
void TypicalDSP::Reset() { filter_->reset(); }
void TypicalDSP::Process(float *samples, std::size_t frames, unsigned channels) {
    if (!streamRate_ || channels != channels_)
        throw std::logic_error("WaveDSPFilter has no matching PCM format");
    float planar[2][256];
    float *channelPointers[2] = {planar[0], planar[1]};
    while (frames) {
        const std::size_t count = std::min<std::size_t>(frames, 256);
        for (std::size_t frame = 0; frame < count; ++frame) {
            planar[0][frame] = samples[frame * channels];
            planar[1][frame] = channels == 2 ? samples[frame * channels + 1] : 0;
        }
        filter_->process(static_cast<int>(count), channelPointers);
        for (std::size_t frame = 0; frame < count; ++frame)
            for (unsigned channel = 0; channel < channels; ++channel)
                samples[frame * channels + channel] = planar[channel][frame];
        samples += count * channels;
        frames -= count;
    }
}

} // namespace audio
} // namespace krkr
