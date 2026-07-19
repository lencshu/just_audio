# Darwin Audio Effects — 10-band Graphic Equalizer (iOS, macOS-ready)

A fail-open, real-time 10-band graphic equalizer for the AVPlayer/AVQueuePlayer
backend, implemented with `MTAudioProcessingTap`. Phase 1 targets **iOS**; all
core code is Darwin-shared so macOS only needs plugin wiring later (the plugin
already declares `sharedDarwinSource: true`).

## Language decision

The existing Darwin backend is **Objective-C**, not Swift. Rather than force
Swift + bridging into a pure-ObjC Flutter plugin (heavy, invasive), this module
follows the proposal's preferred **C/C++ core** path:

- **Pure C++** DSP core (`DSP/`) — no Foundation/AVFoundation/CoreAudio, so it is
  unit-testable on any host toolchain (verified with MSVC on Windows).
- **Objective-C++ (`.mm`)** tap / attachment / controller layer — same language
  family as the existing plugin; `MTAudioProcessingTap` is a C API.

No type or file name carries an `IOS` prefix.

## File map

```
AudioEffects/
├── AudioEffectCapability.h          # ObjC capability enum + string keys
├── DSP/                             # pure C++, host-testable
│   ├── EqualizerConstants.hpp       # bands, Q, gain/preamp ranges, Nyquist factor
│   ├── Biquad.hpp                   # RBJ peaking coeffs + TDF-II state (double)
│   ├── EQParameterSnapshot.hpp      # POD realtime parameter view (fixed arrays)
│   ├── EQParameterStore.hpp         # lock-free seqlock control→realtime handoff
│   ├── EqualizerDSP.hpp/.cpp        # 10-band processor, smoothing, reset
├── Equalizer/
│   ├── EqualizerController.h/.mm    # per-player façade (pure-ObjC header)
│   └── EqualizerPresets.hpp         # preset gain table (C++)
├── Tap/
│   ├── AudioTapContext.hpp          # per-item DSP+store state, buffer-list glue
│   ├── AudioTapFactory.hpp/.mm      # MTAudioProcessingTap + 5 callbacks
│   └── AudioTapAttachment.h/.mm     # installs AVAudioMix on the item
└── Tests/
    └── dsp_tests.cpp                # host unit tests (excluded from plugin build)
```

## just_audio integration points (minimal, in `AudioPlayer.m`)

- **Player creation** — one `EqualizerController` per native player.
- **Item attachment** — `addItemObservers:` is the single choke point every
  `AVPlayerItem` (including the loop/gapless `playerItem2`) passes through, so
  each item gets its own tap exactly once, with independent DSP/filter state.
- **Source-type tag** — `decodeAudioSource:` stamps the just_audio source type
  on the item via an associated object, used for HLS detection.
- **Seek** — `seek:index:completionHandler:` calls `requestReset` (bumps the
  reset generation; the actual `reset()` runs inside the audio callback).
- **Method routing** — `setEqualizerEnabled` / `setEqualizerBandGain` /
  `setEqualizerPreamp` / `setEqualizerPreset` / `resetEqualizer` /
  `getEqualizerCapability` on the existing per-player channel (no new channel).
- **Capability** — surfaced as a new `equalizerCapability` key on the existing
  playback event (backward compatible), consumed by `DarwinEqualizer.capabilityStream`.
- **Dispose** — controller released; items free their `AVAudioMix`/tap naturally;
  each tap frees its context once in `finalize`.

No changes to buffering, seek, playlist, network, audio-session, or background
semantics.

## Dart API

`AudioPlayer.darwinEqualizer` → `DarwinEqualizer`:

```dart
await player.darwinEqualizer.setEnabled(true);
await player.darwinEqualizer.setBandGain(5, 3.0);   // band index, dB
await player.darwinEqualizer.setPreamp(-6.0);        // dB, attenuation only
await player.darwinEqualizer.setPreset(DarwinEqualizerPreset.rock);
await player.darwinEqualizer.reset();
player.darwinEqualizer.capabilityStream.listen(print);
```

Bands: `kDarwinEqualizerFrequencies` (31.25 Hz … 16 kHz). Band gain
±12 dB; preamp −24…0 dB. Safe to call on any platform (no-op off Darwin).

## Thread model

- **Control thread** (Flutter/MethodChannel): validates/clamps, writes the
  `EQParameterStore` (single writer), installs taps.
- **Realtime audio thread** (tap `process` callback): reads a bounded seqlock
  snapshot, applies preamp + biquads in place. No allocation, locks, logging,
  Dart calls, or exceptions (proposal §24).
- Handoff is a **seqlock**: writer bumps an odd→even sequence around the payload;
  the realtime reader retries a bounded number of times and falls back to its
  last good snapshot — wait-free, never spins unbounded.

## Context ownership

`AudioTapContext` is heap-allocated by the factory and handed to the tap via
`clientInfo`. On success the tap owns it and frees it exactly once in
`tap_Finalize`; on `MTAudioProcessingTapCreate` failure the factory frees it.
The `EQParameterStore` is a `shared_ptr` held by both the controller and each
context, so it outlives any teardown ordering.

## Supported / unsupported sources

**Supported:** local files, AVFoundation-decodable remote progressive files,
Jellyfin direct-play / non-HLS transcodes, mono & stereo Float32 Linear PCM
(interleaved or non-interleaved).

**Unsupported → automatic bypass (audio still plays):** HLS (`audioMix` is
invalid for HLS — bypassed up front), DRM/protected assets, assets with no audio
track, >2 channels, non-Float32 / non-LinearPCM processing formats, tap creation
failure.

## PCM format handling

Format is read in the tap `prepare` callback (never hard-coded). Float32 Linear
PCM with 1–2 channels is accepted; anything else sets `formatSupported = false`
and the realtime path leaves the buffer untouched. Bands whose centre frequency
is ≥ `sampleRate × 0.45` are individually bypassed (Nyquist safety).

## macOS next steps (no code duplication)

Everything under `AudioEffects/` is already Darwin-shared. macOS phase needs
only: (1) confirm the pod/SPM build includes these files on macOS (it does via
`sharedDarwinSource`), (2) verify `audioTapProcessor` behaviour on macOS output
devices, (3) run the device test matrix. No DSP is reimplemented.

## Known limitations / risks

- HLS has no `audioMix` EQ (Apple restriction) — reported as `unavailableForHLS`.
- >2-channel audio is bypassed (no multichannel EQ in phase 1).
- No limiter: multiple positive band boosts can still clip; presets ship with
  negative preamp and UIs should prefer auto-preamp.
- Unsupported PCM formats bypass **silently** at prepare time; the attach-time
  capability is optimistically `available` (format rejection is only known post-
  prepare). Audio remains correct; only the EQ is inert.
- No dynamic audio-track switching mid-item (first enabled track is used).
- Various Jellyfin transcode configurations still need on-device verification.

## Testing status

- **DSP core**: host unit tests pass (MSVC, 1,005,173 checks, 0 failures) —
  transparency, ±6/±12 dB peaking magnitude, NaN/Inf freedom, reset, Nyquist
  bypass, preamp attenuation, buffer layouts, format rejection, reset generation,
  parameter clamping.

  ```
  c++ -std=c++17 -O2 -IDSP Tests/dsp_tests.cpp DSP/EqualizerDSP.cpp -o dsp_tests
  ```

- **Tap lifecycle, ObjC++ layer, integration**: require Xcode/an iOS device and
  are **not yet run** (development host is Windows). Outstanding before merge:
  on-device matrix (proposal §29.4), Instruments Allocations/Time-Profiler for
  the realtime callback (§30), and the disabled-EQ regression checks (§29.5).
