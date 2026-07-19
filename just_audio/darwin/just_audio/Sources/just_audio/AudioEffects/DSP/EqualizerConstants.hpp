// EqualizerConstants.hpp
//
// Darwin-shared (iOS/macOS) audio effects module.
// Central definition of the fixed 10-band graphic EQ configuration.
//
// Pure C++: no Foundation / AVFoundation / CoreAudio dependency so it can be
// unit-tested on any host toolchain. Do NOT add platform imports here.

#ifndef JUST_AUDIO_AUDIOFX_EQUALIZER_CONSTANTS_HPP
#define JUST_AUDIO_AUDIOFX_EQUALIZER_CONSTANTS_HPP

#include <cstddef>

namespace just_audio {
namespace audiofx {

// Fixed 10-band graphic EQ. Band count is a compile-time constant on purpose:
// the realtime path must never read a dynamically sized array (proposal §10).
static constexpr int kBandCount = 10;

// ISO-ish octave centre frequencies, matching the Dart-side table.
static constexpr double kBandFrequencies[kBandCount] = {
    31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};

// Shared constant Q for every peaking band. Kept in one place (proposal §18):
// do not sprinkle magic numbers across the DSP.
static constexpr double kDefaultQ = 1.4;

// Gain limits (dB).
static constexpr float kMinBandGainDb = -12.0f;
static constexpr float kMaxBandGainDb = 12.0f;

// Preamp is attenuation-only in phase 1 to avoid accidental clipping (§8/§21).
static constexpr float kMinPreampDb = -24.0f;
static constexpr float kMaxPreampDb = 0.0f;

// Any band whose centre frequency is at or above sampleRate * this factor is
// bypassed to avoid ill-conditioned coefficients near Nyquist (proposal §19).
static constexpr double kNyquistBypassFactor = 0.45;

// Supported channel range for the realtime path.
static constexpr int kMaxSupportedChannels = 2;

inline float clampBandGainDb(float db) {
    if (db < kMinBandGainDb) return kMinBandGainDb;
    if (db > kMaxBandGainDb) return kMaxBandGainDb;
    return db;
}

inline float clampPreampDb(float db) {
    if (db < kMinPreampDb) return kMinPreampDb;
    if (db > kMaxPreampDb) return kMaxPreampDb;
    return db;
}

// Reverb wet / room-size / damp are all normalised knob values in [0, 1].
inline float clampReverbAmount(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_EQUALIZER_CONSTANTS_HPP
