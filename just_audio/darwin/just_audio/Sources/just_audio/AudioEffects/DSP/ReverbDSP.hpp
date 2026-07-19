// ReverbDSP.hpp
//
// Freeverb-style stereo reverb (Schroeder/Moorer: 8 parallel lowpass-feedback
// combs into 4 series allpasses per channel; the classic public-domain tuning
// by Jezar at Dreampoint). Runs AFTER the graphic EQ in the tap's processing
// chain and mixes as: out = dry(wet) * x + wet * reverb(x).
//
// Threading contract mirrors EqualizerDSP:
//   * prepare()/unprepare()/reset() run on a non-realtime thread.
//   * processFrame() runs on the realtime audio thread — no allocation, no
//     locking, no logging.
//
// Pure C++ (no platform headers) so the whole reverb is unit-testable with the
// host toolchain via Tests/dsp_tests.cpp.

#ifndef JUST_AUDIO_AUDIOFX_REVERB_DSP_HPP
#define JUST_AUDIO_AUDIOFX_REVERB_DSP_HPP

#include <cstddef>
#include <vector>

#include "EqualizerConstants.hpp"

namespace just_audio {
namespace audiofx {

class ReverbDSP {
public:
    // Number of parallel combs / series allpasses per channel (Freeverb).
    static constexpr int kCombCount = 8;
    static constexpr int kAllpassCount = 4;

    // Allocates delay lines for the given format. Delay lengths follow the
    // 44.1 kHz Freeverb tuning scaled to `sampleRate`. Returns false on an
    // unsupported format (caller bypasses; playback must never fail).
    bool prepare(double sampleRate, int channelCount);
    void unprepare();

    // Clears all delay lines and damping state (seek / item swap) so no
    // pre-seek tail leaks into the new position.
    void reset();

    bool isPrepared() const { return prepared_; }

    // Block-rate parameter update (values already clamped by the store).
    // roomSize/damp map onto comb feedback / lowpass exactly as in Freeverb;
    // wet is smoothed per sample inside processFrame for click-free knob turns.
    void setParams(float wet, float roomSize, float damp);

    // True when the reverb contributes audibly; when false the caller can skip
    // processFrame entirely (bit-exact passthrough, zero cost).
    bool isActive() const { return prepared_ && (wetTarget_ > kSilenceWet || wetCurrent_ > kSilenceWet); }

    // Processes one frame in place. `frame` points at `channelCount` contiguous
    // floats (one sample per channel — an interleaved frame, or a small buffer
    // the caller gathered from planar channels). Realtime-safe.
    void processFrame(float* frame, int channelCount);

private:
    static constexpr float kSilenceWet = 1.0e-4f;

    struct Comb {
        std::vector<float> buffer;
        int index = 0;
        float filterStore = 0.0f;
        inline float process(float input, float feedback, float damp) {
            float output = buffer[static_cast<size_t>(index)];
            filterStore = output * (1.0f - damp) + filterStore * damp;
            buffer[static_cast<size_t>(index)] = input + filterStore * feedback;
            if (++index >= static_cast<int>(buffer.size())) index = 0;
            return output;
        }
        void clear() {
            for (auto& v : buffer) v = 0.0f;
            filterStore = 0.0f;
            index = 0;
        }
    };

    struct Allpass {
        std::vector<float> buffer;
        int index = 0;
        inline float process(float input) {
            const float bufout = buffer[static_cast<size_t>(index)];
            buffer[static_cast<size_t>(index)] = input + bufout * 0.5f;
            if (++index >= static_cast<int>(buffer.size())) index = 0;
            return bufout - input;
        }
        void clear() {
            for (auto& v : buffer) v = 0.0f;
            index = 0;
        }
    };

    bool prepared_ = false;
    int channelCount_ = 0;
    double sampleRate_ = 0.0;

    // Freeverb parameter mapping (see .cpp for the constants).
    float feedback_ = 0.0f;   // comb feedback derived from roomSize
    float damp_ = 0.0f;       // comb lowpass derived from damp
    float wetTarget_ = 0.0f;  // 0..1 user wet amount
    float wetCurrent_ = 0.0f; // per-sample smoothed
    float wetSmoothCoef_ = 0.0f;

    // Per channel: kCombCount combs + kAllpassCount allpasses.
    std::vector<Comb> combs_;        // channelCount_ * kCombCount
    std::vector<Allpass> allpasses_; // channelCount_ * kAllpassCount
};

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_REVERB_DSP_HPP
