#pragma once

#include "WaveIntf.h"
#include "WaveLoopManager.h"
#include "WaveEffectDSP.h"
#include "MsgIntf.h"
#include <cstring>
#include <limits>
#include <mutex>
#include <vector>

// Shared adapter for script-created filters. The interface property must be
// the actual iTVPBasicWaveFilter subobject address: WaveSoundBuffer obtains it
// and invokes Recreate/Decode through the native audio chain.
class WaveFilterBridge : public iTVPBasicWaveFilter, public tTVPSampleAndLabelSource {
    tTVPSampleAndLabelSource *source_ = nullptr;
    tTVPWaveFormat input_{}, output_{};
    std::vector<unsigned char> pcm_;
    tjs_uint64 tailRemaining_ = 0;
    tjs_int64 lastPosition_ = 0;
    bool ended_ = false;
protected:
    mutable std::recursive_mutex mutex_;
    unsigned extendMs_ = 0;
    virtual void PrepareDSP(unsigned sampleRate, unsigned channels) = 0;
    virtual void ResetDSP() = 0;
    virtual void ProcessDSP(float *data, std::size_t frames, unsigned channels) = 0;
public:
    virtual ~WaveFilterBridge() {}
    tjs_int64 GetInterface() const {
        return static_cast<tjs_int64>(reinterpret_cast<tjs_intptr_t>(
            static_cast<iTVPBasicWaveFilter *>(const_cast<WaveFilterBridge *>(this))));
    }
    tTVPSampleAndLabelSource *Recreate(tTVPSampleAndLabelSource *source) override {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        if (!source || source == this || source_)
            TVPThrowExceptionMessage(TJS_W("Cannot connect multiple wave sound buffers to one filter."));
        const tTVPWaveFormat format = source->GetFormat();
        krkr::audio::CheckFormat(format.SamplesPerSec, format.Channels);
        if ((!format.IsFloat && (format.BytesPerSample < 1 || format.BytesPerSample > 4 ||
                                format.BitsPerSample != format.BytesPerSample * 8)) ||
            (format.IsFloat && (format.BytesPerSample != 4 || format.BitsPerSample != 32)))
            TVPThrowExceptionMessage(TJS_W("Unsupported PCM format for wave filter."));
        PrepareDSP(format.SamplesPerSec, format.Channels);
        input_ = output_ = format;
        output_.BitsPerSample = 32;
        output_.BytesPerSample = 4;
        output_.IsFloat = true;
        if (output_.TotalSamples)
            output_.TotalSamples += static_cast<tjs_uint64>(extendMs_) * output_.SamplesPerSec / 1000;
        if (output_.TotalTime) output_.TotalTime += extendMs_;
        source_ = source;
        Reset();
        return this;
    }
    void Clear() override {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        source_ = nullptr;
        pcm_.clear();
        Reset();
    }
    void Reset() override {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        ResetDSP();
        ended_ = false;
        lastPosition_ = 0;
        tailRemaining_ = static_cast<tjs_uint64>(extendMs_) * input_.SamplesPerSec / 1000;
    }
    void Update() override {
        // Parameter setters and Decode share mutex_: updates cannot race with
        // an audio block and do not depend on integer/float writes being atomic.
    }
    void Finalize() { Clear(); }
    const tTVPWaveFormat &GetFormat() const override { return output_; }
    void Decode(void *destination, tjs_uint frames, tjs_uint &written,
                tTVPWaveSegmentQueue &segments) override {
        std::lock_guard<std::recursive_mutex> guard(mutex_);
        written = 0;
        if (!source_ || !frames) return;
        float *out = static_cast<float *>(destination);
        if (!ended_) {
            if (input_.IsFloat) {
                source_->Decode(out, frames, written, segments);
            } else {
                const std::size_t bytesPerFrame = input_.BytesPerSample * input_.Channels;
                if (frames > std::numeric_limits<std::size_t>::max() / bytesPerFrame)
                    TVPThrowExceptionMessage(TJS_W("Wave filter block is too large."));
                pcm_.resize(static_cast<std::size_t>(frames) * bytesPerFrame);
                source_->Decode(pcm_.data(), frames, written, segments);
                if (written > frames)
                    TVPThrowExceptionMessage(TJS_W("Wave decoder returned too many samples."));
                TVPConvertPCMToFloat(out, pcm_.data(), input_, written);
            }
            if (written > frames)
                TVPThrowExceptionMessage(TJS_W("Wave decoder returned too many samples."));
            if (!segments.GetSegments().empty()) {
                const tTVPWaveSegment &last = segments.GetSegments().back();
                lastPosition_ = last.Start + last.Length;
            }
            ended_ = written < frames;
        }
        if (ended_ && written < frames && tailRemaining_) {
            const tjs_uint extra = static_cast<tjs_uint>(
                std::min<tjs_uint64>(frames - written, tailRemaining_));
            std::fill(out + static_cast<std::size_t>(written) * input_.Channels,
                      out + static_cast<std::size_t>(written + extra) * input_.Channels, 0.0f);
            // Tail samples continue the time line without manufacturing labels.
            segments.Enqueue(tTVPWaveSegment(lastPosition_, extra));
            lastPosition_ += extra;
            tailRemaining_ -= extra;
            written += extra;
        }
        ProcessDSP(out, written, input_.Channels);
    }
};
