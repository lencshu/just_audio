// AudioTapFactory.hpp
//
// Creates an MTAudioProcessingTap wired to an EQ DSP context (proposal §14).
// Objective-C++ (imports MediaToolbox); only .mm files include this.

#ifndef JUST_AUDIO_AUDIOFX_AUDIO_TAP_FACTORY_HPP
#define JUST_AUDIO_AUDIOFX_AUDIO_TAP_FACTORY_HPP

#import <MediaToolbox/MediaToolbox.h>

#include <memory>

#include "../DSP/EQParameterStore.hpp"

namespace just_audio {
namespace audiofx {

// Creates a retained MTAudioProcessingTap. On success returns a non-null tap
// (caller owns one reference and must CFRelease it after handing it to the
// audio mix) and sets *statusOut to noErr. On failure returns nullptr, sets
// *statusOut to the OSStatus, and the internal context is freed — no leak
// (proposal §13). The tap takes ownership of its context and frees it exactly
// once in the finalize callback.
MTAudioProcessingTapRef MakeEqualizerTap(std::shared_ptr<EQParameterStore> store,
                                         OSStatus* statusOut);

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_AUDIO_TAP_FACTORY_HPP
