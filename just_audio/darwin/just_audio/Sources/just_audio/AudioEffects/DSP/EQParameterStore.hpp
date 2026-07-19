// EQParameterStore.hpp
//
// Lock-free hand-off of EQ parameters from the control thread (Flutter /
// MethodChannel) to the realtime audio callback.
//
// Design: a seqlock (proposal §11). The single writer increments a sequence
// counter to an odd value, writes the payload, then increments it to an even
// value. The reader samples the counter, copies the payload, samples again and
// retries if the counter changed or was odd. The realtime reader uses a bounded
// number of retries and, on the (astronomically unlikely) failure to get a
// clean read, falls back to its last good snapshot — so it never blocks and
// never spins unbounded (proposal §24: no lock waits on the audio thread).
//
// One writer only. Multiple readers are safe. Pure C++.

#ifndef JUST_AUDIO_AUDIOFX_EQ_PARAMETER_STORE_HPP
#define JUST_AUDIO_AUDIOFX_EQ_PARAMETER_STORE_HPP

#include <atomic>
#include <cstdint>

#include "EQParameterSnapshot.hpp"
#include "EqualizerConstants.hpp"

namespace just_audio {
namespace audiofx {

class EQParameterStore {
public:
    EQParameterStore() {
        // Publish the default snapshot so a reader before any write still gets
        // a consistent, well-defined value.
        publish(payload_);
    }

    // ---- Control-thread API (single writer) --------------------------------

    void setEnabled(bool enabled) {
        payload_.enabled = enabled;
        bumpParameters();
    }

    // index must be in [0, kBandCount); gainDb is clamped.
    void setBandGain(int index, float gainDb) {
        if (index < 0 || index >= kBandCount) return;
        payload_.bandGainsDb[index] = clampBandGainDb(gainDb);
        bumpParameters();
    }

    void setPreamp(float gainDb) {
        payload_.preampDb = clampPreampDb(gainDb);
        bumpParameters();
    }

    // Replace all band gains at once (e.g. applying a preset). preampDb is set
    // separately by the caller if the preset carries one.
    void setAllBandGains(const float gains[kBandCount]) {
        for (int i = 0; i < kBandCount; ++i) {
            payload_.bandGainsDb[i] = clampBandGainDb(gains[i]);
        }
        bumpParameters();
    }

    // All values are normalised knob positions in [0, 1] (clamped here).
    void setReverb(float wet, float roomSize, float damp) {
        payload_.reverbWet = clampReverbAmount(wet);
        payload_.reverbRoomSize = clampReverbAmount(roomSize);
        payload_.reverbDamp = clampReverbAmount(damp);
        bumpParameters();
    }

    void reset() {
        payload_.preampDb = 0.0f;
        for (int i = 0; i < kBandCount; ++i) payload_.bandGainsDb[i] = 0.0f;
        payload_.reverbWet = 0.0f;
        payload_.reverbRoomSize = 0.5f;
        payload_.reverbDamp = 0.5f;
        // reset() clears the DSP filter history too, so bump both generations.
        ++resetGeneration_;
        payload_.resetGeneration = resetGeneration_;
        bumpParameters();
    }

    // Request a DSP history reset without changing any gains (seek / item swap).
    void requestReset() {
        ++resetGeneration_;
        payload_.resetGeneration = resetGeneration_;
        publish(payload_);
    }

    // ---- Realtime-thread API (readers) -------------------------------------

    // Bounded, wait-free read. On contention it retries a few times, then
    // returns `fallback` unchanged. The caller (DSP) keeps its previous
    // snapshot as the fallback so a rare torn read is simply ignored for one
    // block.
    EQParameterSnapshot realtimeSnapshot(const EQParameterSnapshot& fallback) const {
        constexpr int kMaxRetries = 4;
        for (int attempt = 0; attempt < kMaxRetries; ++attempt) {
            const uint64_t s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1u) continue;  // writer in progress
            EQParameterSnapshot out = shadow_;
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t s2 = seq_.load(std::memory_order_relaxed);
            if (s1 == s2) return out;
        }
        return fallback;
    }

    // Non-realtime convenience read (control thread / tests).
    EQParameterSnapshot loadSnapshot() const {
        EQParameterSnapshot fallback = payload_;
        return realtimeSnapshot(fallback);
    }

private:
    void bumpParameters() {
        ++parameterGeneration_;
        payload_.parameterGeneration = parameterGeneration_;
        publish(payload_);
    }

    void publish(const EQParameterSnapshot& p) {
        const uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release);  // odd: write in progress
        std::atomic_thread_fence(std::memory_order_release);
        shadow_ = p;
        std::atomic_thread_fence(std::memory_order_release);
        seq_.store(s + 2, std::memory_order_release);  // even: stable
    }

    // Writer-owned working copy.
    EQParameterSnapshot payload_;
    // Published copy guarded by the seqlock.
    EQParameterSnapshot shadow_;
    mutable std::atomic<uint64_t> seq_{0};

    uint64_t parameterGeneration_ = 0;
    uint64_t resetGeneration_ = 0;
};

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_EQ_PARAMETER_STORE_HPP
