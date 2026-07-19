// AudioTapAttachment.h
//
// Installs the EQ tap on an AVPlayerItem via AVAudioMix (proposal §15). This is
// the only correct way to attach an MTAudioProcessingTap to a player item — the
// tap is bound to a specific audio track through AVAudioMixInputParameters.
//
// Objective-C++ (the initializer takes a C++ shared_ptr): imported only by
// EqualizerController.mm, never by the pure-Objective-C player.

#ifndef JUST_AUDIO_AUDIOFX_AUDIO_TAP_ATTACHMENT_H
#define JUST_AUDIO_AUDIOFX_AUDIO_TAP_ATTACHMENT_H

#import <AVFoundation/AVFoundation.h>

#include <memory>

#import "../AudioEffectCapability.h"
#include "../DSP/EQParameterStore.hpp"
#include "../DSP/EqualizerDSP.hpp"

NS_ASSUME_NONNULL_BEGIN

// Disambiguation code carried in EqualizerDiagnostics.attachState: WHY a
// diagnostics snapshot for the playing item came back all-zero. Mirrored by
// the Dart-side DarwinEqualizerAttachState enum — keep the raw values stable.
typedef NS_ENUM(NSInteger, AudioTapAttachDiagState) {
    AudioTapAttachDiagStateUnknown = 0,      // not evaluated
    AudioTapAttachDiagStateNoItem = 1,       // player has no current item
    AudioTapAttachDiagStateNoAudioMix = 2,   // item exists, audioMix never set
    AudioTapAttachDiagStateNoTapInMix = 3,   // audioMix set, no tap inside
    AudioTapAttachDiagStateTapNoStorage = 4, // tap present, storage missing
    AudioTapAttachDiagStateTapPresent = 5,   // tap present with live context
};

@interface AudioTapAttachment : NSObject

- (instancetype)initWithStore:(std::shared_ptr<just_audio::audiofx::EQParameterStore>)store;

// Asynchronously loads the asset's tracks and installs the tap. Never blocks
// playback; `completion` is called with the resulting capability (possibly on a
// background queue). `sourceType` is the just_audio source type hint used for
// HLS detection ("hls" bypasses immediately, proposal §27).
- (void)attachToItem:(AVPlayerItem *)item
          sourceType:(nullable NSString *)sourceType
          completion:(void (^)(EqualizerCapability capability))completion;

// Installs the tap immediately when possible, WITHOUT blocking: returns YES
// only when the item could be handled definitively right now — either a
// definitive bypass (HLS) or the asset's keys are already loaded so the mix is
// installed on the spot. Returns NO when the asset still needs asynchronous
// key loading; the caller should then use -attachToItem:… . The point of the
// synchronous path is ordering: the audioMix must be present before the item
// is inserted into the player (setting it once the item is ReadyToPlay is too
// late — tap_Prepare never fires). Must be called on the main thread.
- (BOOL)attachIfResolvedToItem:(AVPlayerItem *)item
                    sourceType:(nullable NSString *)sourceType
                    capability:(nullable EqualizerCapability *)outCapability;

// Returns the current tap's DSP counters. If no tap is attached, all fields are
// zero. Called from the control thread for diagnostics only.
- (just_audio::audiofx::EqualizerDiagnostics)diagnostics;

// Counters for the tap actually installed on `item`, rather than whichever
// queued item's asynchronous attachment happened to finish most recently.
- (just_audio::audiofx::EqualizerDiagnostics)diagnosticsForItem:(nullable AVPlayerItem *)item;

@end

NS_ASSUME_NONNULL_END

#endif  // JUST_AUDIO_AUDIOFX_AUDIO_TAP_ATTACHMENT_H
