// EqualizerController.h
//
// Per-player façade for the graphic EQ (proposal §9/§26.1). Receives commands
// from the platform channel, validates/clamps them, updates the shared
// parameter store, and installs the processing tap on player items.
//
// This header is intentionally pure Objective-C (no C++ types) so the existing
// Objective-C AudioPlayer can import it without becoming Objective-C++. The C++
// DSP/store live behind the implementation (EqualizerController.mm).

#import <AVFoundation/AVFoundation.h>
#import "../AudioEffectCapability.h"

NS_ASSUME_NONNULL_BEGIN

@interface EqualizerController : NSObject

// Number of fixed bands (10). Exposed for callers that validate indices.
@property (class, readonly, nonatomic) NSInteger bandCount;

- (instancetype)init;

// --- Parameter commands (control thread) ---
- (void)setEnabled:(BOOL)enabled;
- (void)setBandGainAtIndex:(NSInteger)index gainDb:(float)gainDb;
- (void)setPreampDb:(float)gainDb;
// Freeverb-style reverb after the EQ; all values are normalised [0, 1] knob
// positions (wet == 0 removes the stage from the signal path entirely).
- (void)setReverbWet:(float)wet roomSize:(float)roomSize damp:(float)damp;
- (void)applyPresetNamed:(NSString *)presetName;
- (void)reset;  // clears gains + preamp + filter history

// Request a DSP filter-history reset without touching gains. Called on
// seek / time-jump / item replacement (proposal §23/§26.3).
- (void)requestReset;

// --- Item attachment ---
// Attempts to install the EQ tap on `item`. Never throws and never blocks
// playback: the completion reports the resulting capability. `sourceType` is
// the just_audio source type string ("progressive", "hls", "dash", ...) used
// as a first-pass HLS hint (proposal §27).
- (void)attachToItem:(AVPlayerItem *)item
          sourceType:(nullable NSString *)sourceType
          completion:(nullable void (^)(EqualizerCapability capability))completion;

// Non-blocking pre-insert attach (see AudioTapAttachment). Returns YES if the
// item was handled definitively right now (tap installed because the asset's
// keys were already loaded, or a definitive bypass such as HLS); NO if it must
// be attached asynchronously via -attachToItem:… . On YES, -lastCapability
// reflects the outcome.
- (BOOL)attachIfResolvedToItem:(AVPlayerItem *)item
                    sourceType:(nullable NSString *)sourceType;

// Last computed capability for the most recently attached item.
@property (readonly, nonatomic) EqualizerCapability lastCapability;

// Current DSP counters as a MethodChannel-safe dictionary for diagnostics.
@property (readonly, nonatomic) NSDictionary<NSString *, NSNumber *> *diagnostics;

- (NSDictionary<NSString *, NSNumber *> *)diagnosticsForItem:(nullable AVPlayerItem *)item;

@end

NS_ASSUME_NONNULL_END
