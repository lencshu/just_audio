// EqualizerPresets.hpp
//
// Fixed-band EQ presets (proposal §8 setPreset). Pure C++ so it is shared by
// the Darwin controller and testable on the host. Gains are in dB, one per
// band, following kBandFrequencies order (31.25 Hz … 16 kHz). preampDb is the
// recommended attenuation so multi-band boosts do not clip (attenuation-only,
// §21). A preset name that is unknown resolves to "flat".

#ifndef JUST_AUDIO_AUDIOFX_EQUALIZER_PRESETS_HPP
#define JUST_AUDIO_AUDIOFX_EQUALIZER_PRESETS_HPP

#include <cstring>

#include "../DSP/EqualizerConstants.hpp"

namespace just_audio {
namespace audiofx {

struct EqualizerPreset {
    const char* name;
    float preampDb;
    float gains[kBandCount];
};

// Ordered table. Keep names in sync with the Dart EqualizerPreset enum.
static const EqualizerPreset kEqualizerPresets[] = {
    // name            preamp  31   62   125  250  500  1k   2k   4k   8k   16k
    {"flat",            0.0f, { 0,   0,   0,   0,   0,   0,   0,   0,   0,   0 }},
    {"bassBoost",      -6.0f, { 6,   5,   4,   2,   0,   0,   0,   0,   0,   0 }},
    {"bassReduce",      0.0f, {-6,  -5,  -4,  -2,   0,   0,   0,   0,   0,   0 }},
    {"trebleBoost",    -6.0f, { 0,   0,   0,   0,   0,   1,   2,   4,   5,   6 }},
    {"trebleReduce",    0.0f, { 0,   0,   0,   0,   0,  -1,  -2,  -4,  -5,  -6 }},
    {"vocal",          -3.0f, {-2,  -2,   0,   2,   3,   3,   2,   1,   0,  -1 }},
    {"rock",           -6.0f, { 5,   3,  -1,  -2,  -1,   1,   3,   4,   4,   4 }},
    {"pop",            -4.0f, {-1,   0,   2,   3,   3,   2,   0,  -1,  -1,  -1 }},
    {"jazz",           -4.0f, { 3,   2,   1,   2,  -1,  -1,   0,   1,   2,   3 }},
    {"classical",      -3.0f, { 4,   3,   2,   1,  -1,  -1,   0,   2,   3,   4 }},
    {"electronic",     -6.0f, { 5,   4,   1,   0,  -1,   1,   0,   1,   4,   5 }},
    {"loudness",       -8.0f, { 7,   5,   0,   0,  -2,   0,   0,   3,   6,   7 }},
};

static constexpr int kEqualizerPresetCount =
    static_cast<int>(sizeof(kEqualizerPresets) / sizeof(kEqualizerPresets[0]));

// Returns a pointer to the named preset, or the "flat" preset if not found.
inline const EqualizerPreset& equalizerPresetByName(const char* name) {
    if (name != nullptr) {
        for (int i = 0; i < kEqualizerPresetCount; ++i) {
            if (std::strcmp(kEqualizerPresets[i].name, name) == 0) {
                return kEqualizerPresets[i];
            }
        }
    }
    return kEqualizerPresets[0];  // flat
}

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_EQUALIZER_PRESETS_HPP
