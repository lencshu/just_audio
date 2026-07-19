// EqualizerDSP.cpp  — see EqualizerDSP.hpp for the threading contract.

#include "EqualizerDSP.hpp"

#include <cmath>

namespace just_audio {
namespace audiofx {

namespace {
// Time constants for click-free parameter changes (proposal §22).
constexpr double kPreampTauSeconds = 0.010;   // per-sample preamp ramp
constexpr double kGainTauSeconds = 0.020;      // per-block band-gain ramp
// A band whose |gain| is below this is bit-exact pass-through (A==1 makes the
// peaking transfer function identically 1), so it can be skipped safely.
constexpr float kGainActiveThresholdDb = 0.02f;
constexpr float kGainRecomputeThresholdDb = 0.01f;

inline double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }
}  // namespace

bool EqualizerDSP::prepare(double sampleRate, int channelCount, int maxFrames) {
    if (sampleRate <= 0.0 || channelCount < 1 ||
        channelCount > kMaxSupportedChannels || maxFrames <= 0) {
        prepared_ = false;
        return false;
    }
    sampleRate_ = sampleRate;
    channelCount_ = channelCount;
    maxFrames_ = maxFrames;

    // Per-band Nyquist bypass (proposal §19).
    for (int i = 0; i < kBandCount; ++i) {
        bandBypass_[i] = kBandFrequencies[i] >= sampleRate * kNyquistBypassFactor;
    }

    state_.assign(static_cast<size_t>(channelCount) * kBandCount, BiquadState{});
    for (auto& s : state_) s.reset();

    // Start smoothing at the current target so a fresh prepare does not ramp.
    EQParameterSnapshot snap = store_ ? store_->loadSnapshot() : EQParameterSnapshot{};
    lastSnapshot_ = snap;
    localResetGeneration_ = snap.resetGeneration;
    localParameterGeneration_ = ~uint64_t(0);  // force coefficient rebuild
    for (int i = 0; i < kBandCount; ++i) {
        smoothedBandGainsDb_[i] = snap.bandGainsDb[i];
        appliedBandGainsDb_[i] = 1e9f;  // force recompute for every band
    }
    preampTargetLin_ = dbToLinear(snap.preampDb);
    preampCurrentLin_ = preampTargetLin_;
    preampSmoothCoef_ = 1.0 - std::exp(-1.0 / (kPreampTauSeconds * sampleRate_));

    updateCoefficients(sampleRate_);

    // Reverb allocates its delay lines here (non-realtime). A failed prepare
    // simply leaves the stage inactive; the EQ still works.
    reverb_.setParams(snap.reverbWet, snap.reverbRoomSize, snap.reverbDamp);
    reverb_.prepare(sampleRate, channelCount);
    reverbActiveThisBlock_ = false;
    reverbWasActive_ = false;

    diag_ = EqualizerDiagnostics{};
    diag_.lastSampleRate = sampleRate;
    diag_.lastChannelCount = channelCount;

    prepared_ = true;
    return true;
}

void EqualizerDSP::unprepare() {
    prepared_ = false;
    state_.clear();
    reverb_.unprepare();
    channelCount_ = 0;
    maxFrames_ = 0;
    sampleRate_ = 0.0;
}

void EqualizerDSP::reset() {
    for (auto& s : state_) s.reset();
    preampCurrentLin_ = preampTargetLin_;
    reverb_.reset();
}

void EqualizerDSP::updateCoefficients(double sampleRate) {
    for (int i = 0; i < kBandCount; ++i) {
        if (bandBypass_[i]) {
            coeffs_[i] = BiquadCoefficients::identity();
            appliedBandGainsDb_[i] = 0.0f;
            continue;
        }
        const float g = smoothedBandGainsDb_[i];
        if (std::fabs(g - appliedBandGainsDb_[i]) > kGainRecomputeThresholdDb) {
            coeffs_[i] = BiquadCoefficients::peaking(kBandFrequencies[i], sampleRate,
                                                     kDefaultQ, g);
            appliedBandGainsDb_[i] = g;
        }
    }
}

void EqualizerDSP::smoothBandGains() {
    // Handled in beginBlock via the block coefficient; kept for clarity.
}

void EqualizerDSP::beginBlock(int channelCount, int frameCount) {
    EQParameterSnapshot snap = store_ ? store_->realtimeSnapshot(lastSnapshot_)
                                      : lastSnapshot_;
    lastSnapshot_ = snap;

    if (snap.resetGeneration != localResetGeneration_) {
        for (auto& s : state_) s.reset();
        preampCurrentLin_ = preampTargetLin_;
        reverb_.reset();
        localResetGeneration_ = snap.resetGeneration;
        ++diag_.resetCount;
    }

    activeThisBlock_ = snap.enabled;
    if (!activeThisBlock_) {
        // Master switch off also silences the reverb; drop its tail so a later
        // re-enable starts clean instead of replaying stale reverberation.
        if (reverbWasActive_) reverb_.reset();
        reverbActiveThisBlock_ = false;
        reverbWasActive_ = false;
        return;
    }

    reverb_.setParams(snap.reverbWet, snap.reverbRoomSize, snap.reverbDamp);
    reverbActiveThisBlock_ = reverb_.isActive();
    if (!reverbActiveThisBlock_ && reverbWasActive_) reverb_.reset();
    reverbWasActive_ = reverbActiveThisBlock_;

    preampTargetLin_ = dbToLinear(snap.preampDb);

    // Block-rate one-pole smoothing toward the target band gains.
    const double coef =
        1.0 - std::exp(-static_cast<double>(frameCount) / (kGainTauSeconds * sampleRate_));
    bool anyChanged = false;
    for (int i = 0; i < kBandCount; ++i) {
        const float target = snap.bandGainsDb[i];
        const float g = smoothedBandGainsDb_[i] +
                        static_cast<float>((target - smoothedBandGainsDb_[i]) * coef);
        smoothedBandGainsDb_[i] = g;
        if (std::fabs(g - appliedBandGainsDb_[i]) > kGainRecomputeThresholdDb) {
            anyChanged = true;
        }
    }
    if (anyChanged) updateCoefficients(sampleRate_);

    (void)channelCount;
}

inline float EqualizerDSP::processSample(float x, int channel) {
    const int base = channel * kBandCount;
    for (int i = 0; i < kBandCount; ++i) {
        if (bandBypass_[i]) continue;
        // Skip bit-exact pass-through bands (0 dB peaking == identity TF).
        if (std::fabs(appliedBandGainsDb_[i]) < kGainActiveThresholdDb) continue;
        x = state_[base + i].process(x, coeffs_[i]);
    }
    return x;
}

void EqualizerDSP::processInterleaved(float* data, int frameCount, int channelCount) {
    if (!prepared_ || data == nullptr || frameCount <= 0) return;
    if (channelCount != channelCount_ || frameCount > maxFrames_) {
        ++diag_.unsupportedFormatCount;
        return;
    }
    beginBlock(channelCount, frameCount);
    ++diag_.processCallCount;
    if (!activeThisBlock_) {
        ++diag_.bypassCount;
        return;
    }
    for (int n = 0; n < frameCount; ++n) {
        preampCurrentLin_ += (preampTargetLin_ - preampCurrentLin_) * preampSmoothCoef_;
        const float pre = static_cast<float>(preampCurrentLin_);
        const int off = n * channelCount;
        for (int c = 0; c < channelCount; ++c) {
            data[off + c] = processSample(data[off + c] * pre, c);
        }
        if (reverbActiveThisBlock_) {
            reverb_.processFrame(data + off, channelCount);
        }
    }
    diagSeq_.fetch_add(1, std::memory_order_release);
}

void EqualizerDSP::processDeinterleaved(float* const* channels, int frameCount,
                                        int channelCount) {
    if (!prepared_ || channels == nullptr || frameCount <= 0) return;
    if (channelCount != channelCount_ || frameCount > maxFrames_) {
        ++diag_.unsupportedFormatCount;
        return;
    }
    for (int c = 0; c < channelCount; ++c) {
        if (channels[c] == nullptr) {
            ++diag_.unsupportedFormatCount;
            return;
        }
    }
    beginBlock(channelCount, frameCount);
    ++diag_.processCallCount;
    if (!activeThisBlock_) {
        ++diag_.bypassCount;
        return;
    }
    // Frame-outer so the per-sample preamp ramp stays coherent across channels.
    for (int n = 0; n < frameCount; ++n) {
        preampCurrentLin_ += (preampTargetLin_ - preampCurrentLin_) * preampSmoothCoef_;
        const float pre = static_cast<float>(preampCurrentLin_);
        for (int c = 0; c < channelCount; ++c) {
            channels[c][n] = processSample(channels[c][n] * pre, c);
        }
        if (reverbActiveThisBlock_) {
            // Gather the planar frame into an interleaved scratch on the stack;
            // kMaxSupportedChannels is tiny so this stays allocation-free.
            float frame[kMaxSupportedChannels];
            for (int c = 0; c < channelCount; ++c) frame[c] = channels[c][n];
            reverb_.processFrame(frame, channelCount);
            for (int c = 0; c < channelCount; ++c) channels[c][n] = frame[c];
        }
    }
    diagSeq_.fetch_add(1, std::memory_order_release);
}

EqualizerDiagnostics EqualizerDSP::diagnostics() const {
    // Debug-only read. A memory fence gives us a coherent view of the counters
    // written by the audio thread; an occasional torn value is acceptable here.
    (void)diagSeq_.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_acquire);
    return diag_;
}

}  // namespace audiofx
}  // namespace just_audio
