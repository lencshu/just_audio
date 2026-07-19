// EqualizerDSP.hpp
//
// Fixed 10-band peaking-EQ processor. Owns per-channel/per-band filter state,
// a shared coefficient set, and block-rate parameter smoothing. Consumes an
// EQParameterStore for realtime parameter hand-off.
//
// Threading:
//   * prepare()/unprepare()/reset() run on a non-realtime thread (Tap
//     prepare/unprepare, or a controlled reset request).
//   * processInterleaved()/processDeinterleaved() run on the realtime audio
//     thread and never allocate, lock, or log (proposal §24).
//
// The DSP takes plain float pointers, not CoreAudio buffer lists, so it stays
// free of platform headers and is unit-testable on any host (proposal §17).
// The AudioBufferList inspection lives in the Objective-C++ Tap layer.

#ifndef JUST_AUDIO_AUDIOFX_EQUALIZER_DSP_HPP
#define JUST_AUDIO_AUDIOFX_EQUALIZER_DSP_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "Biquad.hpp"
#include "EQParameterSnapshot.hpp"
#include "EQParameterStore.hpp"
#include "EqualizerConstants.hpp"
#include "ReverbDSP.hpp"

namespace just_audio {
namespace audiofx {

// Non-realtime diagnostics. The realtime path only ever increments the plain
// counters; the control thread reads a copy (proposal §25).
struct EqualizerDiagnostics {
    uint64_t processCallCount = 0;
    uint64_t bypassCount = 0;
    uint64_t resetCount = 0;
    uint64_t unsupportedFormatCount = 0;
    double lastSampleRate = 0.0;
    int lastChannelCount = 0;

    // ---- Tap/attachment-level fields, filled by the Objective-C++ tap layer
    // (AudioTapAttachment), never by the DSP itself. They disambiguate the
    // otherwise identical all-zero snapshots: "the playing item has no tap at
    // all" vs "the tap is installed but AVFoundation never called prepare" vs
    // "prepare ran but rejected the format".
    uint64_t tapInitCount = 0;          // tap_Init invocations on this tap
    uint64_t tapPrepareCount = 0;       // tap_Prepare invocations (any format)
    uint64_t tapUnprepareCount = 0;     // tap_Unprepare invocations
    uint64_t tapProcessEntryCount = 0;  // tap_Process entries incl. bypass
    int attachState = 0;   // AudioTapAttachDiagState (AudioTapAttachment.h)
    int itemStatus = -1;   // AVPlayerItemStatus of the inspected item; -1 n/a
};

class EqualizerDSP {
public:
    explicit EqualizerDSP(EQParameterStore* store) : store_(store) {}

    // Allocate all format-dependent state. Returns false if the format is
    // unsupported (caller should then bypass, never fail playback — §12.2).
    bool prepare(double sampleRate, int channelCount, int maxFrames);

    // Release format-dependent state; the object may be prepared again with a
    // different format afterwards (proposal §28.4).
    void unprepare();

    // Zero all filter history. Non-realtime callers may use this; the realtime
    // path resets via the resetGeneration mechanism inside process().
    void reset();

    bool isPrepared() const { return prepared_; }

    // ---- Realtime processing ----------------------------------------------
    // Interleaved: one buffer, samples laid out c0 c1 c0 c1 ...
    void processInterleaved(float* data, int frameCount, int channelCount);
    // Deinterleaved: `channelCount` separate buffers, one per channel.
    void processDeinterleaved(float* const* channels, int frameCount, int channelCount);

    // Snapshot of diagnostics for tests / debug (non-realtime).
    EqualizerDiagnostics diagnostics() const;

private:
    // Pull the latest parameter snapshot, apply reset if requested, and refresh
    // smoothing targets / coefficients. Shared by both process paths.
    void beginBlock(int channelCount, int frameCount);
    // Recompute coefficients for bands whose smoothed gain moved.
    void updateCoefficients(double sampleRate);
    // Advance smoothed band gains one block toward their targets.
    void smoothBandGains();

    inline float processSample(float x, int channel);

    EQParameterStore* store_ = nullptr;

    bool prepared_ = false;
    double sampleRate_ = 0.0;
    int channelCount_ = 0;
    int maxFrames_ = 0;

    // Shared coefficients (one set for all channels) and per-band bypass flags.
    BiquadCoefficients coeffs_[kBandCount];
    bool bandBypass_[kBandCount] = {false};

    // Smoothed / target gains driving the coefficients.
    float smoothedBandGainsDb_[kBandCount] = {0};
    float appliedBandGainsDb_[kBandCount] = {0};  // gains the coeffs reflect

    // Preamp is smoothed per-sample in the linear domain.
    double preampCurrentLin_ = 1.0;
    double preampTargetLin_ = 1.0;
    double preampSmoothCoef_ = 0.0;  // per-sample one-pole coefficient

    // Per-channel, per-band filter state: channelCount_ * kBandCount.
    std::vector<BiquadState> state_;

    // Local copies used by the realtime path.
    EQParameterSnapshot lastSnapshot_;
    uint64_t localResetGeneration_ = 0;
    uint64_t localParameterGeneration_ = ~uint64_t(0);  // force first update
    bool activeThisBlock_ = false;

    // Reverb stage, processed after the band filters (chain:
    // preamp → 10-band EQ → reverb). Gated per block; when the wet knob sits at
    // zero the stage is skipped entirely and its tail is cleared.
    ReverbDSP reverb_;
    bool reverbActiveThisBlock_ = false;
    bool reverbWasActive_ = false;

    // Diagnostics (realtime writes plain, non-atomic increments — single
    // audio thread — control thread reads with a relaxed atomic barrier).
    EqualizerDiagnostics diag_;
    mutable std::atomic<uint64_t> diagSeq_{0};
};

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_EQUALIZER_DSP_HPP
