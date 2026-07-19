// AudioTapAttachment.mm — see AudioTapAttachment.h.

#import "AudioTapAttachment.h"

#import <MediaToolbox/MediaToolbox.h>

#include "AudioTapContext.hpp"
#include "AudioTapFactory.hpp"

using namespace just_audio::audiofx;

// Attachment-side diagnostic logging (control thread only, never realtime).
#define JA_EQ_LOG(fmt, ...) NSLog(@"[JustAudioEQ][attach] " fmt, ##__VA_ARGS__)

namespace {

// Copies the tap lifecycle counters out of a tap's context into a diagnostics
// snapshot. Returns an empty snapshot (attachState=TapNoStorage) when the tap
// has no storage.
EqualizerDiagnostics DiagnosticsFromTap(MTAudioProcessingTapRef tap) {
    auto* ctx = static_cast<AudioTapContext*>(MTAudioProcessingTapGetStorage(tap));
    if (ctx == nullptr) {
        EqualizerDiagnostics diag{};
        diag.attachState = AudioTapAttachDiagStateTapNoStorage;
        return diag;
    }
    EqualizerDiagnostics diag = ctx->dsp.diagnostics();
    diag.tapInitCount = ctx->tapInitCount.load(std::memory_order_relaxed);
    diag.tapPrepareCount = ctx->tapPrepareCount.load(std::memory_order_relaxed);
    diag.tapUnprepareCount = ctx->tapUnprepareCount.load(std::memory_order_relaxed);
    diag.tapProcessEntryCount =
        ctx->tapProcessEntryCount.load(std::memory_order_relaxed);
    diag.attachState = AudioTapAttachDiagStateTapPresent;
    return diag;
}

}  // namespace

@implementation AudioTapAttachment {
    std::shared_ptr<EQParameterStore> _store;
    MTAudioProcessingTapRef _currentTap;
}

- (instancetype)initWithStore:(std::shared_ptr<EQParameterStore>)store {
    self = [super init];
    if (self) {
        _store = std::move(store);
        _currentTap = nullptr;
    }
    return self;
}

- (void)dealloc {
    [self clearCurrentTap];
}

- (void)clearCurrentTap {
    if (_currentTap != nullptr) {
        CFRelease(_currentTap);
        _currentTap = nullptr;
    }
}

- (void)setCurrentTap:(MTAudioProcessingTapRef)tap {
    if (tap == _currentTap) return;
    [self clearCurrentTap];
    if (tap != nullptr) {
        _currentTap = (MTAudioProcessingTapRef)CFRetain(tap);
    }
}

- (EqualizerDiagnostics)diagnostics {
    if (_currentTap == nullptr) {
        EqualizerDiagnostics diag{};
        diag.attachState = AudioTapAttachDiagStateNoTapInMix;
        return diag;
    }
    return DiagnosticsFromTap(_currentTap);
}

- (EqualizerDiagnostics)diagnosticsForItem:(AVPlayerItem *)item {
    EqualizerDiagnostics diag{};
    if (item == nil) {
        diag.attachState = AudioTapAttachDiagStateNoItem;
        return diag;
    }
    if (item.audioMix == nil) {
        // The decisive "EQ silently dead" signature: commands succeed against
        // the parameter store while the PLAYING item never received a mix.
        diag.attachState = AudioTapAttachDiagStateNoAudioMix;
        diag.itemStatus = (int)item.status;
        return diag;
    }
    for (AVAudioMixInputParameters *params in item.audioMix.inputParameters) {
        MTAudioProcessingTapRef tap = params.audioTapProcessor;
        if (tap == nullptr) continue;
        diag = DiagnosticsFromTap(tap);
        diag.itemStatus = (int)item.status;
        return diag;
    }
    diag.attachState = AudioTapAttachDiagStateNoTapInMix;
    diag.itemStatus = (int)item.status;
    return diag;
}

// Builds and installs the AVAudioMix (with a fresh tap) on `item`, given an
// asset whose `tracks`/`hasProtectedContent` keys are already loaded. MUST run
// on the main thread — AVFoundation expects audioMix assignment there, and the
// item must not be racing playback setup. Returns the resulting capability.
- (EqualizerCapability)installTapOnItem:(AVPlayerItem *)item asset:(AVAsset *)asset {
    if ([asset statusOfValueForKey:@"hasProtectedContent" error:nil] ==
            AVKeyValueStatusLoaded &&
        asset.hasProtectedContent) {
        [self clearCurrentTap];
        return EqualizerCapabilityUnavailableForProtectedContent;
    }

    NSArray<AVAssetTrack *> *audioTracks =
        [asset tracksWithMediaType:AVMediaTypeAudio];
    if (audioTracks.count == 0) {
        JA_EQ_LOG("no audio track item=%p itemStatus=%ld", item, (long)item.status);
        [self clearCurrentTap];
        return EqualizerCapabilityUnavailableNoAudioTrack;
    }

    // Default to the first enabled audio track (proposal §16). just_audio's
    // Darwin backend does not currently switch audio tracks mid-item, and
    // Jellyfin music sources are typically single-track, so this is safe;
    // dynamic track switching is a documented follow-up.
    AVAssetTrack *track = nil;
    for (AVAssetTrack *t in audioTracks) {
        if (t.isEnabled) { track = t; break; }
    }
    if (track == nil) track = audioTracks.firstObject;

    OSStatus status = noErr;
    MTAudioProcessingTapRef tap = MakeEqualizerTap(_store, &status);
    if (tap == nullptr) {
        [self clearCurrentTap];
        return EqualizerCapabilityUnavailableTapCreationFailed;
    }

    AVMutableAudioMixInputParameters *params =
        [AVMutableAudioMixInputParameters audioMixInputParametersWithTrack:track];
    params.audioTapProcessor = tap;

    AVMutableAudioMix *audioMix = [AVMutableAudioMix audioMix];
    audioMix.inputParameters = @[ params ];
    item.audioMix = audioMix;

    // item.status here is the smoking gun for a dead tap: a mix assigned to an
    // item already at ReadyToPlay (status=1) sits outside the frozen render
    // graph and its tap never receives prepare/process.
    NSURL *assetURL =
        [asset isKindOfClass:[AVURLAsset class]] ? ((AVURLAsset *)asset).URL : nil;
    JA_EQ_LOG("installed mix item=%p itemStatus=%ld trackID=%d tracks=%lu "
              "scheme=%@ tap=%p",
              item, (long)item.status, (int)track.trackID,
              (unsigned long)audioTracks.count,
              assetURL.scheme ?: @"?", tap);

    [self setCurrentTap:tap];
    CFRelease(tap);  // audioMix now owns the tap
    return EqualizerCapabilityAvailable;
}

- (BOOL)attachIfResolvedToItem:(AVPlayerItem *)item
                    sourceType:(NSString *)sourceType
                    capability:(EqualizerCapability *)outCapability {
    if (item == nil) return NO;

    // First-pass HLS bypass: audioMix is invalid for HLS (proposal §6/§27).
    if (sourceType != nil && [sourceType isEqualToString:@"hls"]) {
        [self clearCurrentTap];
        if (outCapability) *outCapability = EqualizerCapabilityUnavailableForHLS;
        return YES;  // definitively handled — bypass, no async retry needed
    }

    AVAsset *asset = item.asset;
    if (asset == nil) return NO;

    // Strictly non-blocking: only proceed when the keys are ALREADY loaded
    // (always true for local file:// assets by the time they are enqueued, and
    // true for remote assets whose keys a previous async attach resolved).
    // Anything else must go through -attachToItem:… — a semaphore wait here
    // would freeze the platform thread for every remote track insertion.
    if ([asset statusOfValueForKey:@"tracks" error:nil] != AVKeyValueStatusLoaded) {
        return NO;
    }

    EqualizerCapability cap = [self installTapOnItem:item asset:asset];
    if (outCapability) *outCapability = cap;
    return YES;
}

- (void)attachToItem:(AVPlayerItem *)item
          sourceType:(NSString *)sourceType
          completion:(void (^)(EqualizerCapability))completion {
    void (^report)(EqualizerCapability) = ^(EqualizerCapability cap) {
        if (completion) completion(cap);
    };

    if (item == nil) {
        [self clearCurrentTap];
        report(EqualizerCapabilityUnavailableUnknown);
        return;
    }

    // First-pass HLS bypass: audioMix is only valid for file-based media, never
    // for HTTP Live Streaming (proposal §6/§27). Do not even attempt a tap.
    if (sourceType != nil && [sourceType isEqualToString:@"hls"]) {
        [self clearCurrentTap];
        report(EqualizerCapabilityUnavailableForHLS);
        return;
    }

    AVAsset *asset = item.asset;
    if (asset == nil) {
        [self clearCurrentTap];
        report(EqualizerCapabilityUnavailableNoAudioTrack);
        return;
    }

    // iOS 12 / macOS 10.14 minimum: use the legacy async key loading rather than
    // the iOS 15+ loadTracks(withMediaType:) API (proposal §15 compat note).
    NSArray<NSString *> *keys = @[ @"tracks", @"hasProtectedContent", @"playable" ];

    [asset loadValuesAsynchronouslyForKeys:keys
                         completionHandler:^{
        NSError *error = nil;
        AVKeyValueStatus tracksStatus = [asset statusOfValueForKey:@"tracks" error:&error];
        if (tracksStatus != AVKeyValueStatusLoaded) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [self clearCurrentTap];
            });
            report(EqualizerCapabilityUnavailableUnknown);
            return;
        }

        // Assign the audio mix on the main thread to avoid racing with playback
        // setup. The item retains the mix, which retains the tap; installTap
        // releases our creation reference after handing it over.
        dispatch_async(dispatch_get_main_queue(), ^{
            report([self installTapOnItem:item asset:asset]);
        });
    }];
}

@end
