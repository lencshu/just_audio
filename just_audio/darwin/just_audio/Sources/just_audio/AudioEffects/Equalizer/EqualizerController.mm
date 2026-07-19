// EqualizerController.mm — see EqualizerController.h.

#import "EqualizerController.h"

#include <memory>

#import "../Tap/AudioTapAttachment.h"
#include "../DSP/EQParameterStore.hpp"
#include "../DSP/EqualizerConstants.hpp"
#include "EqualizerPresets.hpp"

using namespace just_audio::audiofx;

@implementation EqualizerController {
    std::shared_ptr<EQParameterStore> _store;
    AudioTapAttachment *_attachment;
    EqualizerCapability _lastCapability;
}

+ (NSInteger)bandCount {
    return kBandCount;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _store = std::make_shared<EQParameterStore>();
        _attachment = [[AudioTapAttachment alloc] initWithStore:_store];
        _lastCapability = EqualizerCapabilityUnavailableUnknown;
    }
    return self;
}

- (EqualizerCapability)lastCapability {
    return _lastCapability;
}

- (NSDictionary<NSString *, NSNumber *> *)diagnostics {
    EqualizerDiagnostics diag = [_attachment diagnostics];
    return [self dictionaryForDiagnostics:diag];
}

- (NSDictionary<NSString *, NSNumber *> *)diagnosticsForItem:(AVPlayerItem *)item {
    EqualizerDiagnostics diag = [_attachment diagnosticsForItem:item];
    return [self dictionaryForDiagnostics:diag];
}

- (NSDictionary<NSString *, NSNumber *> *)dictionaryForDiagnostics:(EqualizerDiagnostics)diag {
    return @{
        @"processCallCount": @(diag.processCallCount),
        @"bypassCount": @(diag.bypassCount),
        @"resetCount": @(diag.resetCount),
        @"unsupportedFormatCount": @(diag.unsupportedFormatCount),
        @"lastSampleRate": @(diag.lastSampleRate),
        @"lastChannelCount": @(diag.lastChannelCount),
        @"tapInitCount": @(diag.tapInitCount),
        @"tapPrepareCount": @(diag.tapPrepareCount),
        @"tapUnprepareCount": @(diag.tapUnprepareCount),
        @"tapProcessEntryCount": @(diag.tapProcessEntryCount),
        @"attachState": @(diag.attachState),
        @"itemStatus": @(diag.itemStatus),
    };
}

- (void)setEnabled:(BOOL)enabled {
    _store->setEnabled(enabled ? true : false);
}

- (void)setBandGainAtIndex:(NSInteger)index gainDb:(float)gainDb {
    if (index < 0 || index >= kBandCount) return;  // validate (proposal §9)
    _store->setBandGain(static_cast<int>(index), gainDb);  // store clamps
}

- (void)setPreampDb:(float)gainDb {
    _store->setPreamp(gainDb);  // store clamps to [-24, 0]
}

- (void)setReverbWet:(float)wet roomSize:(float)roomSize damp:(float)damp {
    _store->setReverb(wet, roomSize, damp);  // store clamps to [0, 1]
}

- (void)applyPresetNamed:(NSString *)presetName {
    const EqualizerPreset &preset =
        equalizerPresetByName(presetName.UTF8String);
    _store->setAllBandGains(preset.gains);
    _store->setPreamp(preset.preampDb);
}

- (void)reset {
    _store->reset();
}

- (void)requestReset {
    _store->requestReset();
}

- (void)attachToItem:(AVPlayerItem *)item
          sourceType:(NSString *)sourceType
          completion:(void (^)(EqualizerCapability))completion {
    __weak EqualizerController *weakSelf = self;
    [_attachment attachToItem:item
                   sourceType:sourceType
                   completion:^(EqualizerCapability capability) {
        EqualizerController *strongSelf = weakSelf;
        if (strongSelf) strongSelf->_lastCapability = capability;
        if (completion) completion(capability);
    }];
}

- (BOOL)attachIfResolvedToItem:(AVPlayerItem *)item
                    sourceType:(NSString *)sourceType {
    EqualizerCapability capability = EqualizerCapabilityUnavailableUnknown;
    BOOL handled = [_attachment attachIfResolvedToItem:item
                                            sourceType:sourceType
                                            capability:&capability];
    if (handled) _lastCapability = capability;
    return handled;
}

@end
