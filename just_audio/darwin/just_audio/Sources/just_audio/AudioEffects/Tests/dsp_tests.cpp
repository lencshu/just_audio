// dsp_tests.cpp
//
// Host-buildable unit tests for the pure-C++ EQ DSP core (proposal §29.1/§29.2).
// No CoreAudio / Foundation dependency, so these run on any toolchain:
//
//   c++ -std=c++17 -O2 -I../DSP dsp_tests.cpp ../DSP/EqualizerDSP.cpp //       ../DSP/ReverbDSP.cpp -o dsp_tests
//   ./dsp_tests
//
// Verifies: transparency at 0 dB, +/-6 dB peaking magnitude at the band centre,
// NaN/Inf freedom, reset behaviour, Nyquist bypass, preamp attenuation, buffer
// layouts (mono/stereo, interleaved/deinterleaved), and the parameter store.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

#include "Biquad.hpp"
#include "EQParameterStore.hpp"
#include "EqualizerConstants.hpp"
#include "EqualizerDSP.hpp"

using namespace just_audio::audiofx;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);       \
        }                                                                 \
    } while (0)

// Measure steady-state magnitude response of a single biquad at frequency f.
static double biquadMagnitude(const BiquadCoefficients& c, double f, double fs) {
    const int warmup = 4096;
    const int measure = 8192;
    BiquadState st;
    st.reset();
    double peakOut = 0.0, peakIn = 0.0;
    const double w = 2.0 * kPi * f / fs;
    for (int n = 0; n < warmup + measure; ++n) {
        const float x = static_cast<float>(std::sin(w * n));
        const float y = st.process(x, c);
        if (n >= warmup) {
            peakOut = std::max(peakOut, std::fabs((double)y));
            // Normalise by the input's own peak *sample* value: for f/fs ratios
            // like 1/6 the sampled sine never reaches 1.0 (max sample = 0.866),
            // so comparing to a nominal 1.0 would be a measurement artifact.
            peakIn = std::max(peakIn, std::fabs((double)x));
        }
    }
    return peakOut / peakIn;
}

static void testBiquadTransparency() {
    std::printf("testBiquadTransparency\n");
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (int b = 0; b < kBandCount; ++b) {
            const double f = kBandFrequencies[b];
            if (f >= fs * kNyquistBypassFactor) continue;
            auto c = BiquadCoefficients::peaking(f, fs, kDefaultQ, 0.0);
            const double mag = biquadMagnitude(c, f, fs);
            CHECK(std::fabs(mag - 1.0) < 0.02);  // ~0 dB
        }
    }
}

static void testBiquadPeakGain() {
    std::printf("testBiquadPeakGain\n");
    const double fs = 44100.0;
    for (double gainDb : {6.0, -6.0, 12.0, -12.0}) {
        // Use a mid band well below Nyquist.
        const double f = 1000.0;
        auto c = BiquadCoefficients::peaking(f, fs, kDefaultQ, gainDb);
        const double mag = biquadMagnitude(c, f, fs);
        const double measuredDb = 20.0 * std::log10(mag);
        CHECK(std::fabs(measuredDb - gainDb) < 0.5);
    }
}

static void testNoNaNExtreme() {
    std::printf("testNoNaNExtreme\n");
    const double fs = 44100.0;
    for (int b = 0; b < kBandCount; ++b) {
        auto c = BiquadCoefficients::peaking(kBandFrequencies[b], fs, kDefaultQ, 12.0);
        BiquadState st;
        st.reset();
        for (int n = 0; n < 100000; ++n) {
            float x = (n % 2 == 0) ? 1.0f : -1.0f;  // full-scale square
            float y = st.process(x, c);
            CHECK(std::isfinite(y));
            if (!std::isfinite(y)) break;
        }
    }
}

static void testResetClearsState() {
    std::printf("testResetClearsState\n");
    auto c = BiquadCoefficients::peaking(1000.0, 44100.0, kDefaultQ, 9.0);
    BiquadState st;
    st.reset();
    for (int n = 0; n < 1000; ++n) st.process(std::sin(0.1 * n), c);
    st.reset();
    // After reset, first output for input 0 must be exactly 0.
    CHECK(st.process(0.0f, c) == 0.0f);
}

static void testDSPBypassWhenDisabled() {
    std::printf("testDSPBypassWhenDisabled\n");
    EQParameterStore store;
    store.setEnabled(false);
    store.setBandGain(5, 12.0f);  // would be audible if applied
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(44100.0, 2, 1024));

    std::vector<float> buf(2 * 512);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = std::sin(0.05 * i);
    std::vector<float> orig = buf;
    dsp.processInterleaved(buf.data(), 512, 2);
    for (size_t i = 0; i < buf.size(); ++i) CHECK(buf[i] == orig[i]);
}

static void testDSPFlatIsApproxTransparent() {
    std::printf("testDSPFlatIsApproxTransparent\n");
    EQParameterStore store;
    store.setEnabled(true);
    store.setPreamp(0.0f);  // all bands 0 dB
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(48000.0, 2, 2048));

    std::vector<float> buf(2 * 1000);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = 0.3f * std::sin(0.02 * i);
    std::vector<float> orig = buf;
    dsp.processInterleaved(buf.data(), 1000, 2);
    double maxErr = 0.0;
    for (size_t i = 0; i < buf.size(); ++i)
        maxErr = std::max(maxErr, std::fabs((double)(buf[i] - orig[i])));
    CHECK(maxErr < 1e-4);
}

static void testPreampAttenuation() {
    std::printf("testPreampAttenuation\n");
    EQParameterStore store;
    store.setEnabled(true);
    store.setPreamp(-6.0f);  // 0.501x
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(44100.0, 1, 4096));

    const int frames = 4000;
    std::vector<float> buf(frames);
    for (int i = 0; i < frames; ++i) buf[i] = 0.5f;  // DC-ish, all bands flat
    float* chans[1] = {buf.data()};
    dsp.processDeinterleaved(chans, frames, 1);
    // After the preamp ramp settles, output should approach 0.5 * 10^(-6/20).
    const float expected = 0.5f * std::pow(10.0f, -6.0f / 20.0f);
    CHECK(std::fabs(buf[frames - 1] - expected) < 0.005f);
}

static void testNyquistBypass() {
    std::printf("testNyquistBypass\n");
    // At 32 kHz sample rate, the 16 kHz band (== Nyquist) must be bypassed.
    const double fs = 32000.0;
    CHECK(kBandFrequencies[9] >= fs * kNyquistBypassFactor);
    EQParameterStore store;
    store.setEnabled(true);
    store.setBandGain(9, 12.0f);
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(fs, 1, 1024));
    // A signal at 15 kHz should be essentially unaffected (band bypassed).
    const int frames = 1024;
    std::vector<float> buf(frames);
    const double w = 2.0 * kPi * 15000.0 / fs;
    for (int i = 0; i < frames; ++i) buf[i] = std::sin(w * i);
    std::vector<float> orig = buf;
    float* chans[1] = {buf.data()};
    // Warm up several blocks so smoothing settles, then compare a fresh block.
    for (int k = 0; k < 8; ++k) dsp.processDeinterleaved(chans, frames, 1);
    // Because the band is bypassed, no boost is applied; RMS stays ~equal.
    double e = 0, o = 0;
    for (int i = 0; i < frames; ++i) { e += buf[i]*buf[i]; o += orig[i]*orig[i]; }
    CHECK(std::fabs(std::sqrt(e) - std::sqrt(o)) / std::sqrt(o) < 0.05);
}

static void testUnsupportedFormatBypass() {
    std::printf("testUnsupportedFormatBypass\n");
    EQParameterStore store;
    EqualizerDSP dsp(&store);
    CHECK(!dsp.prepare(44100.0, 3, 1024));   // >2 channels rejected
    CHECK(!dsp.prepare(0.0, 2, 1024));       // bad sample rate
    CHECK(!dsp.prepare(44100.0, 2, 0));      // bad maxFrames
    CHECK(dsp.prepare(44100.0, 2, 1024));    // valid

    // frameCount exceeding maxFrames must be ignored, buffer untouched.
    std::vector<float> buf(2 * 2048, 0.7f);
    std::vector<float> orig = buf;
    dsp.processInterleaved(buf.data(), 2048, 2);  // > maxFrames
    for (size_t i = 0; i < buf.size(); ++i) CHECK(buf[i] == orig[i]);
}

static void testResetGeneration() {
    std::printf("testResetGeneration\n");
    EQParameterStore store;
    store.setEnabled(true);
    store.setBandGain(4, 10.0f);
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(44100.0, 1, 1024));
    std::vector<float> buf(1024);
    for (int i = 0; i < 1024; ++i) buf[i] = std::sin(0.03 * i);
    float* chans[1] = {buf.data()};
    dsp.processDeinterleaved(chans, 1024, 1);
    uint64_t before = dsp.diagnostics().resetCount;
    store.requestReset();
    dsp.processDeinterleaved(chans, 1024, 1);
    CHECK(dsp.diagnostics().resetCount == before + 1);
}

static void testStoreClamping() {
    std::printf("testStoreClamping\n");
    EQParameterStore store;
    store.setBandGain(0, 999.0f);
    store.setPreamp(999.0f);
    auto s = store.loadSnapshot();
    CHECK(s.bandGainsDb[0] == kMaxBandGainDb);
    CHECK(s.preampDb == kMaxPreampDb);
    store.setPreamp(-999.0f);
    CHECK(store.loadSnapshot().preampDb == kMinPreampDb);
    store.setBandGain(-1, 3.0f);   // out of range: ignored
    store.setBandGain(99, 3.0f);   // out of range: ignored
    CHECK(store.loadSnapshot().bandGainsDb[0] == kMaxBandGainDb);
}

// ---- Reverb ---------------------------------------------------------------

static void testReverbWetZeroPassthrough() {
    std::printf("testReverbWetZeroPassthrough\n");
    EQParameterStore store;
    store.setEnabled(true);  // EQ enabled, flat, reverb wet 0
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(44100.0, 2, 512));
    std::vector<float> buf(2 * 512);
    for (size_t i = 0; i < buf.size(); ++i) buf[i] = std::sin(0.05 * i);
    std::vector<float> orig = buf;
    dsp.processInterleaved(buf.data(), 512, 2);
    // Flat EQ + wet==0 reverb must be bit-exact passthrough.
    for (size_t i = 0; i < buf.size(); ++i) CHECK(buf[i] == orig[i]);
}

static void testReverbTailAndStability() {
    std::printf("testReverbTailAndStability\n");
    EQParameterStore store;
    store.setEnabled(true);
    store.setReverb(0.8f, 0.7f, 0.4f);
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(44100.0, 2, 512));

    // One impulse block, then silence blocks: the tail must ring audibly for a
    // while and remain finite throughout.
    std::vector<float> buf(2 * 512, 0.0f);
    buf[0] = 1.0f;
    buf[1] = 1.0f;
    dsp.processInterleaved(buf.data(), 512, 2);

    double tailEnergy = 0.0;
    bool allFinite = true;
    double lateEnergy = 0.0;
    for (int block = 0; block < 40; ++block) {  // ~0.46 s of tail
        std::fill(buf.begin(), buf.end(), 0.0f);
        dsp.processInterleaved(buf.data(), 512, 2);
        double e = 0.0;
        for (float v : buf) {
            if (!std::isfinite(v)) allFinite = false;
            e += static_cast<double>(v) * v;
        }
        tailEnergy += e;
        if (block >= 30) lateEnergy += e;
    }
    CHECK(allFinite);
    CHECK(tailEnergy > 1.0e-6);   // a tail exists
    CHECK(lateEnergy < tailEnergy);  // and it decays

    // The stereo-spread comb banks must decorrelate the two channels.
    std::fill(buf.begin(), buf.end(), 0.0f);
    buf[0] = 1.0f;
    buf[1] = 1.0f;
    dsp.processInterleaved(buf.data(), 512, 2);
    std::fill(buf.begin(), buf.end(), 0.0f);
    dsp.processInterleaved(buf.data(), 512, 2);
    bool channelsDiffer = false;
    for (int n = 0; n < 512; ++n) {
        if (buf[2 * n] != buf[2 * n + 1]) { channelsDiffer = true; break; }
    }
    CHECK(channelsDiffer);
}

static void testReverbResetClearsTail() {
    std::printf("testReverbResetClearsTail\n");
    EQParameterStore store;
    store.setEnabled(true);
    store.setReverb(1.0f, 0.9f, 0.2f);
    EqualizerDSP dsp(&store);
    CHECK(dsp.prepare(44100.0, 1, 512));
    std::vector<float> buf(512, 0.0f);
    buf[0] = 1.0f;
    float* chans[1] = {buf.data()};
    dsp.processDeinterleaved(chans, 512, 1);

    // Seek-style reset: the pre-seek tail must not leak into new audio.
    store.requestReset();
    std::fill(buf.begin(), buf.end(), 0.0f);
    dsp.processDeinterleaved(chans, 512, 1);
    double energy = 0.0;
    for (float v : buf) energy += static_cast<double>(v) * v;
    CHECK(energy == 0.0);
}

static void testReverbStoreClamping() {
    std::printf("testReverbStoreClamping\n");
    EQParameterStore store;
    store.setReverb(2.0f, -1.0f, 5.0f);
    auto s = store.loadSnapshot();
    CHECK(s.reverbWet == 1.0f);
    CHECK(s.reverbRoomSize == 0.0f);
    CHECK(s.reverbDamp == 1.0f);
    store.reset();
    s = store.loadSnapshot();
    CHECK(s.reverbWet == 0.0f);
    CHECK(s.reverbRoomSize == 0.5f);
    CHECK(s.reverbDamp == 0.5f);
}

int main() {
    testBiquadTransparency();
    testBiquadPeakGain();
    testNoNaNExtreme();
    testResetClearsState();
    testDSPBypassWhenDisabled();
    testDSPFlatIsApproxTransparent();
    testPreampAttenuation();
    testNyquistBypass();
    testUnsupportedFormatBypass();
    testResetGeneration();
    testStoreClamping();
    testReverbWetZeroPassthrough();
    testReverbTailAndStability();
    testReverbResetClearsTail();
    testReverbStoreClamping();

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
