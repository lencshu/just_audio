// AudioTapFactory.mm — MTAudioProcessingTap callbacks + factory (proposal §12/§14).

#import "AudioTapFactory.hpp"

#import <Foundation/Foundation.h>
#import <MediaToolbox/MediaToolbox.h>

#include "AudioTapContext.hpp"

using namespace just_audio::audiofx;

// Diagnostic logging for the tap lifecycle. These callbacks fire (or fail to
// fire) entirely inside AVFoundation, so a log line here is the only direct
// evidence of whether the tap ever entered the render graph.
#define JA_EQ_LOG(fmt, ...) NSLog(@"[JustAudioEQ][tap] " fmt, ##__VA_ARGS__)

namespace {

// §12.1 init: publish the pre-created context as the tap's storage. No
// allocation, no DSP prepare, no player access.
void tap_Init(MTAudioProcessingTapRef tap, void* clientInfo, void** tapStorageOut) {
    (void)tap;
    *tapStorageOut = clientInfo;  // ownership transfers to the tap
    auto* ctx = static_cast<AudioTapContext*>(clientInfo);
    if (ctx != nullptr) {
        ctx->tapInitCount.fetch_add(1, std::memory_order_relaxed);
    }
    JA_EQ_LOG("init tap=%p ctx=%p", tap, clientInfo);
}

// §12.2 prepare: read the processing format and (if supported) prepare the DSP.
// A rejected format simply leaves formatSupported == false → realtime bypass.
void tap_Prepare(MTAudioProcessingTapRef tap, CMItemCount maxFrames,
                 const AudioStreamBasicDescription* processingFormat) {
    auto* ctx = static_cast<AudioTapContext*>(MTAudioProcessingTapGetStorage(tap));
    if (ctx == nullptr) {
        JA_EQ_LOG("prepare tap=%p WITHOUT storage context", tap);
        return;
    }
    ctx->tapPrepareCount.fetch_add(1, std::memory_order_relaxed);
    const bool ok =
        ConfigureContextForFormat(ctx, processingFormat, static_cast<int>(maxFrames));
    if (processingFormat != nullptr) {
        JA_EQ_LOG("prepare tap=%p accepted=%d sampleRate=%.0f channels=%u "
                  "bits=%u formatID=0x%08x flags=0x%08x interleaved=%d maxFrames=%ld",
                  tap, ok ? 1 : 0, processingFormat->mSampleRate,
                  (unsigned)processingFormat->mChannelsPerFrame,
                  (unsigned)processingFormat->mBitsPerChannel,
                  (unsigned)processingFormat->mFormatID,
                  (unsigned)processingFormat->mFormatFlags,
                  ctx->interleaved ? 1 : 0, (long)maxFrames);
    } else {
        JA_EQ_LOG("prepare tap=%p with NULL format", tap);
    }
}

// §12.3 process: fetch source PCM, then process in place. Strictly realtime-safe
// (the one-shot first-call log below is the sole, deliberate exception: it fires
// exactly once per tap and only exists to prove the render path is live).
void tap_Process(MTAudioProcessingTapRef tap, CMItemCount numberFrames,
                 MTAudioProcessingTapFlags flags, AudioBufferList* bufferListInOut,
                 CMItemCount* numberFramesOut, MTAudioProcessingTapFlags* flagsOut) {
    (void)flags;
    auto* ctx = static_cast<AudioTapContext*>(MTAudioProcessingTapGetStorage(tap));
    if (ctx != nullptr) {
        ctx->tapProcessEntryCount.fetch_add(1, std::memory_order_relaxed);
        bool expected = false;
        if (ctx->firstProcessLogged.compare_exchange_strong(
                expected, true, std::memory_order_relaxed)) {
            JA_EQ_LOG("first process call tap=%p frames=%ld", tap, (long)numberFrames);
        }
    }
    OSStatus status = MTAudioProcessingTapGetSourceAudio(
        tap, numberFrames, bufferListInOut, flagsOut, nullptr, numberFramesOut);
    if (status != noErr) {
        // Could not obtain source audio: emit zero frames rather than garbage.
        if (numberFramesOut) *numberFramesOut = 0;
        return;
    }
    ProcessAudioBufferList(ctx, bufferListInOut,
                           static_cast<int>(numberFramesOut ? *numberFramesOut : 0));
}

// §12.4 unprepare: release format-dependent DSP state; the tap may be prepared
// again afterwards with a different format.
void tap_Unprepare(MTAudioProcessingTapRef tap) {
    auto* ctx = static_cast<AudioTapContext*>(MTAudioProcessingTapGetStorage(tap));
    if (ctx == nullptr) return;
    ctx->tapUnprepareCount.fetch_add(1, std::memory_order_relaxed);
    JA_EQ_LOG("unprepare tap=%p", tap);
    ctx->formatSupported = false;
    ctx->dsp.unprepare();
}

// §12.5 finalize: free the retained context exactly once. No player/item access.
void tap_Finalize(MTAudioProcessingTapRef tap) {
    auto* ctx = static_cast<AudioTapContext*>(MTAudioProcessingTapGetStorage(tap));
    JA_EQ_LOG("finalize tap=%p prepared=%llu processed=%llu", tap,
              ctx ? (unsigned long long)ctx->tapPrepareCount.load(std::memory_order_relaxed)
                  : 0ULL,
              ctx ? (unsigned long long)ctx->tapProcessEntryCount.load(std::memory_order_relaxed)
                  : 0ULL);
    delete ctx;
}

}  // namespace

namespace just_audio {
namespace audiofx {

MTAudioProcessingTapRef MakeEqualizerTap(std::shared_ptr<EQParameterStore> store,
                                         OSStatus* statusOut) {
    // Context is heap-allocated and handed to the tap via clientInfo. On success
    // the tap owns it (freed in tap_Finalize); on failure we free it here.
    auto* ctx = new AudioTapContext(std::move(store));

    MTAudioProcessingTapCallbacks callbacks;
    callbacks.version = kMTAudioProcessingTapCallbacksVersion_0;
    callbacks.clientInfo = ctx;
    callbacks.init = tap_Init;
    callbacks.prepare = tap_Prepare;
    callbacks.process = tap_Process;
    callbacks.unprepare = tap_Unprepare;
    callbacks.finalize = tap_Finalize;

    MTAudioProcessingTapRef tap = nullptr;
    // PostEffects: the tap runs after AVFoundation's own audio processing
    // (e.g. the time-pitch unit used for playback speed), so our EQ operates on
    // the final playback stream. Verify against playback-rate changes on device
    // (proposal §14).
    OSStatus status = MTAudioProcessingTapCreate(
        kCFAllocatorDefault, &callbacks,
        kMTAudioProcessingTapCreationFlag_PostEffects, &tap);

    if (statusOut) *statusOut = status;

    if (status != noErr || tap == nullptr) {
        JA_EQ_LOG("MTAudioProcessingTapCreate FAILED status=%d", (int)status);
        // init/finalize are not invoked on creation failure: free the context.
        delete ctx;
        return nullptr;
    }
    return tap;  // caller owns one reference
}

}  // namespace audiofx
}  // namespace just_audio
