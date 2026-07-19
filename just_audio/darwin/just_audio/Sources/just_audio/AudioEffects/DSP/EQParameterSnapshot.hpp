// EQParameterSnapshot.hpp
//
// Immutable, plain-old-data view of the EQ parameters that the realtime audio
// callback consumes. Fixed-size arrays only — never a heap container
// (proposal §10). Copyable by value; a snapshot is produced by EQParameterStore
// and read on the audio thread without locks.
//
// Pure C++. No platform headers.

#ifndef JUST_AUDIO_AUDIOFX_EQ_PARAMETER_SNAPSHOT_HPP
#define JUST_AUDIO_AUDIOFX_EQ_PARAMETER_SNAPSHOT_HPP

#include <cstdint>

#include "EqualizerConstants.hpp"

namespace just_audio {
namespace audiofx {

struct EQParameterSnapshot {
    bool enabled = false;
    float preampDb = 0.0f;
    float bandGainsDb[kBandCount] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    // Reverb (Freeverb-style, processed after the EQ). All normalised [0, 1];
    // wet == 0 keeps the reverb entirely out of the signal path.
    float reverbWet = 0.0f;
    float reverbRoomSize = 0.5f;
    float reverbDamp = 0.5f;

    // Bumped whenever any gain/preamp/enabled value changes. The realtime path
    // compares this against its local copy to decide whether to recompute
    // filter coefficients (proposal §11/§30).
    uint64_t parameterGeneration = 0;

    // Bumped on seek / item replacement / format change. The realtime path
    // resets filter history when this differs from its local copy (proposal §23).
    uint64_t resetGeneration = 0;
};

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_EQ_PARAMETER_SNAPSHOT_HPP
