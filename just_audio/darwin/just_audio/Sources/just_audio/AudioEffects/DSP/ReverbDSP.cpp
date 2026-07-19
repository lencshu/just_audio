// ReverbDSP.cpp — see ReverbDSP.hpp for the threading contract.

#include "ReverbDSP.hpp"

#include <algorithm>
#include <cmath>

namespace just_audio {
namespace audiofx {

namespace {
// Classic Freeverb tuning (samples at 44100 Hz), public domain by Jezar at
// Dreampoint. Delay lengths are scaled linearly with the actual sample rate.
constexpr int kCombTuning[ReverbDSP::kCombCount] = {1116, 1188, 1277, 1356,
                                                    1422, 1491, 1557, 1617};
constexpr int kAllpassTuning[ReverbDSP::kAllpassCount] = {556, 441, 341, 225};
// The right channel's delay lines are offset to decorrelate the two channels.
constexpr int kStereoSpread = 23;
constexpr double kTuningSampleRate = 44100.0;

// Freeverb parameter scaling.
constexpr float kFixedGain = 0.015f;   // input gain into the comb bank
constexpr float kScaleRoom = 0.28f;    // feedback = offset + room * scale
constexpr float kOffsetRoom = 0.7f;
constexpr float kScaleDamp = 0.4f;     // comb lowpass = damp * scale

// Per-sample one-pole smoothing time for the wet knob (click-free turns).
constexpr double kWetTauSeconds = 0.030;
}  // namespace

bool ReverbDSP::prepare(double sampleRate, int channelCount) {
    if (sampleRate <= 0.0 || channelCount < 1 ||
        channelCount > kMaxSupportedChannels) {
        prepared_ = false;
        return false;
    }
    sampleRate_ = sampleRate;
    channelCount_ = channelCount;

    const double scale = sampleRate / kTuningSampleRate;
    combs_.assign(static_cast<size_t>(channelCount) * kCombCount, Comb{});
    allpasses_.assign(static_cast<size_t>(channelCount) * kAllpassCount, Allpass{});
    for (int c = 0; c < channelCount; ++c) {
        const int spread = (c == 0) ? 0 : kStereoSpread;
        for (int i = 0; i < kCombCount; ++i) {
            const int len = std::max(
                2, static_cast<int>(std::lround((kCombTuning[i] + spread) * scale)));
            combs_[static_cast<size_t>(c) * kCombCount + i].buffer.assign(
                static_cast<size_t>(len), 0.0f);
        }
        for (int i = 0; i < kAllpassCount; ++i) {
            const int len = std::max(
                2,
                static_cast<int>(std::lround((kAllpassTuning[i] + spread) * scale)));
            allpasses_[static_cast<size_t>(c) * kAllpassCount + i].buffer.assign(
                static_cast<size_t>(len), 0.0f);
        }
    }

    wetSmoothCoef_ = static_cast<float>(
        1.0 - std::exp(-1.0 / (kWetTauSeconds * sampleRate)));
    wetCurrent_ = wetTarget_;  // fresh prepare does not ramp
    prepared_ = true;
    return true;
}

void ReverbDSP::unprepare() {
    prepared_ = false;
    combs_.clear();
    allpasses_.clear();
    channelCount_ = 0;
    sampleRate_ = 0.0;
}

void ReverbDSP::reset() {
    for (auto& comb : combs_) comb.clear();
    for (auto& ap : allpasses_) ap.clear();
    wetCurrent_ = wetTarget_;
}

void ReverbDSP::setParams(float wet, float roomSize, float damp) {
    wetTarget_ = clampReverbAmount(wet);
    feedback_ = kOffsetRoom + clampReverbAmount(roomSize) * kScaleRoom;
    damp_ = clampReverbAmount(damp) * kScaleDamp;
}

void ReverbDSP::processFrame(float* frame, int channelCount) {
    if (!prepared_ || channelCount != channelCount_) return;

    // Freeverb feeds the mono sum of the input into every channel's comb bank
    // (the banks differ by kStereoSpread, which decorrelates the outputs).
    float input = 0.0f;
    for (int c = 0; c < channelCount; ++c) {
        input += frame[c];
    }
    input *= kFixedGain;

    wetCurrent_ += (wetTarget_ - wetCurrent_) * wetSmoothCoef_;
    const float wet = wetCurrent_;
    // Keep most of the dry body; the slight dry roll-off keeps the summed peak
    // bounded while the tail stays clearly audible at high wet settings.
    const float dry = 1.0f - 0.35f * wet;

    for (int c = 0; c < channelCount; ++c) {
        float out = 0.0f;
        const size_t combBase = static_cast<size_t>(c) * kCombCount;
        for (int i = 0; i < kCombCount; ++i) {
            out += combs_[combBase + i].process(input, feedback_, damp_);
        }
        const size_t apBase = static_cast<size_t>(c) * kAllpassCount;
        for (int i = 0; i < kAllpassCount; ++i) {
            out = allpasses_[apBase + i].process(out);
        }
        frame[c] = frame[c] * dry + out * wet;
    }
}

}  // namespace audiofx
}  // namespace just_audio
