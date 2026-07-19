// AudioEffectCapability.h
//
// Darwin-shared (iOS/macOS) EQ capability model (proposal §7). Reports whether
// the graphic EQ could be attached to a given player item. Attaching never
// fails playback — an unavailable capability just means the audio plays
// unprocessed (fail-open, proposal §7/§27).
//
// Objective-C so it can be surfaced across the existing MethodChannel. No
// UIKit / AVAudioSession here (proposal §28.2).

#ifndef JUST_AUDIO_AUDIO_EFFECT_CAPABILITY_H
#define JUST_AUDIO_AUDIO_EFFECT_CAPABILITY_H

#import <Foundation/Foundation.h>

typedef NS_ENUM(NSInteger, EqualizerCapability) {
    EqualizerCapabilityAvailable = 0,
    EqualizerCapabilityUnavailableForHLS,
    EqualizerCapabilityUnavailableForProtectedContent,
    EqualizerCapabilityUnavailableNoAudioTrack,
    EqualizerCapabilityUnavailableUnsupportedFormat,
    EqualizerCapabilityUnavailableUnsupportedChannelCount,
    EqualizerCapabilityUnavailableTapCreationFailed,
    EqualizerCapabilityUnavailableUnknown,
};

// Stable string keys sent to Dart. These MUST match the Dart
// EqualizerCapability enum decoder in just_audio_platform_interface.
static inline NSString *NSStringFromEqualizerCapability(EqualizerCapability c) {
    switch (c) {
        case EqualizerCapabilityAvailable: return @"available";
        case EqualizerCapabilityUnavailableForHLS: return @"unavailableForHLS";
        case EqualizerCapabilityUnavailableForProtectedContent:
            return @"unavailableForProtectedContent";
        case EqualizerCapabilityUnavailableNoAudioTrack:
            return @"unavailableNoAudioTrack";
        case EqualizerCapabilityUnavailableUnsupportedFormat:
            return @"unavailableUnsupportedFormat";
        case EqualizerCapabilityUnavailableUnsupportedChannelCount:
            return @"unavailableUnsupportedChannelCount";
        case EqualizerCapabilityUnavailableTapCreationFailed:
            return @"unavailableTapCreationFailed";
        case EqualizerCapabilityUnavailableUnknown:
        default:
            return @"unavailableUnknown";
    }
}

#endif  // JUST_AUDIO_AUDIO_EFFECT_CAPABILITY_H
