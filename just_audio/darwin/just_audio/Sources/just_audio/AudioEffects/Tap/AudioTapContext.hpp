// AudioTapContext.hpp
//
// Per-item state owned by one MTAudioProcessingTap (proposal §5/§13). Each
// AVPlayerItem gets its own context and its own DSP/filter history; contexts
// may share the parameter store (via shared_ptr) but never share filter state.
//
// Objective-C++ / CoreAudio: this header pulls in AudioToolbox for
// AudioStreamBasicDescription and is only imported by .mm files. It is NOT
// imported by the pure-Objective-C player.

#ifndef JUST_AUDIO_AUDIOFX_AUDIO_TAP_CONTEXT_HPP
#define JUST_AUDIO_AUDIOFX_AUDIO_TAP_CONTEXT_HPP

#import <AudioToolbox/AudioToolbox.h>

#include <atomic>
#include <memory>

#include "../DSP/EQParameterStore.hpp"
#include "../DSP/EqualizerConstants.hpp"
#include "../DSP/EqualizerDSP.hpp"

namespace just_audio {
namespace audiofx {

struct AudioTapContext {
    // Shared with the controller; keeps the store alive for the tap's lifetime
    // regardless of controller/item teardown order.
    std::shared_ptr<EQParameterStore> store;
    EqualizerDSP dsp;

    // Format state, populated in the prepare callback.
    bool formatSupported = false;   // false => realtime path bypasses
    bool interleaved = false;
    int channels = 0;
    int maxFrames = 0;
    double sampleRate = 0.0;

    // Tap lifecycle counters for diagnostics: written by the tap callbacks
    // (init/prepare/unprepare on control threads, process entry on the
    // realtime thread — a relaxed atomic increment is wait-free), read by
    // AudioTapAttachment when building a diagnostics snapshot. They answer
    // the one question the DSP counters cannot: did AVFoundation ever invoke
    // this tap at all?
    std::atomic<uint64_t> tapInitCount{0};
    std::atomic<uint64_t> tapPrepareCount{0};
    std::atomic<uint64_t> tapUnprepareCount{0};
    std::atomic<uint64_t> tapProcessEntryCount{0};
    // Set once by the first tap_Process entry so the (one-shot) log there
    // never repeats on the realtime thread.
    std::atomic<bool> firstProcessLogged{false};

    explicit AudioTapContext(std::shared_ptr<EQParameterStore> s)
        : store(std::move(s)), dsp(store.get()) {}
};

// Inspect the format for EQ support. Returns true and fills the context format
// fields when the format is Float32 Linear PCM with 1–2 channels. Called from
// the (non-realtime) prepare callback.
inline bool ConfigureContextForFormat(AudioTapContext* ctx,
                                      const AudioStreamBasicDescription* asbd,
                                      int maxFrames) {
    ctx->formatSupported = false;
    if (asbd == nullptr) return false;
    if (asbd->mFormatID != kAudioFormatLinearPCM) return false;
    if ((asbd->mFormatFlags & kAudioFormatFlagIsFloat) == 0) return false;
    if (asbd->mBitsPerChannel != 32) return false;

    const int channels = static_cast<int>(asbd->mChannelsPerFrame);
    if (channels < 1 || channels > kMaxSupportedChannels) return false;

    const bool nonInterleaved =
        (asbd->mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;

    ctx->interleaved = !nonInterleaved;
    ctx->channels = channels;
    ctx->maxFrames = maxFrames;
    ctx->sampleRate = asbd->mSampleRate;

    if (!ctx->dsp.prepare(ctx->sampleRate, channels, maxFrames)) return false;
    ctx->formatSupported = true;
    return true;
}

// Realtime: run the EQ over the source audio already fetched into bufferList.
// Never allocates or logs (proposal §24). Bypasses (leaves audio untouched) if
// the format was rejected in prepare.
inline void ProcessAudioBufferList(AudioTapContext* ctx,
                                   AudioBufferList* bufferList,
                                   int frameCount) {
    if (ctx == nullptr || !ctx->formatSupported || bufferList == nullptr ||
        frameCount <= 0) {
        return;
    }
    if (ctx->interleaved) {
        // Single buffer, channels interleaved.
        if (bufferList->mNumberBuffers < 1) return;
        AudioBuffer& b = bufferList->mBuffers[0];
        if (b.mData == nullptr) return;
        ctx->dsp.processInterleaved(static_cast<float*>(b.mData), frameCount,
                                    ctx->channels);
    } else {
        // One buffer per channel.
        if (static_cast<int>(bufferList->mNumberBuffers) < ctx->channels) return;
        float* channels[kMaxSupportedChannels] = {nullptr, nullptr};
        for (int c = 0; c < ctx->channels; ++c) {
            channels[c] = static_cast<float*>(bufferList->mBuffers[c].mData);
            if (channels[c] == nullptr) return;
        }
        ctx->dsp.processDeinterleaved(channels, frameCount, ctx->channels);
    }
}

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_AUDIO_TAP_CONTEXT_HPP
