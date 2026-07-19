// Biquad.hpp
//
// Single peaking-EQ biquad: coefficient computation (RBJ cookbook) plus a
// transposed-direct-form-II sample processor.
//
// Coefficients and state are kept in double precision. The lowest band
// (31.25 Hz) sits very close to DC relative to typical sample rates, which
// places the poles near the unit circle; float state there is prone to
// coefficient quantisation error and slow drift, so we pay the small cost of
// double math for stability (proposal §18/§29 numerical-stability tests).
//
// Pure C++. No platform headers.

#ifndef JUST_AUDIO_AUDIOFX_BIQUAD_HPP
#define JUST_AUDIO_AUDIOFX_BIQUAD_HPP

#include <cmath>

namespace just_audio {
namespace audiofx {

// Local Pi constant. M_PI is not guaranteed by the C++ standard (MSVC hides it
// behind _USE_MATH_DEFINES); defining our own keeps the DSP toolchain-agnostic.
static constexpr double kPi = 3.14159265358979323846;

// Normalised biquad coefficients (a0 divided out).
struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;

    // Identity (pass-through) filter.
    static BiquadCoefficients identity() { return BiquadCoefficients{1.0, 0.0, 0.0, 0.0, 0.0}; }

    // Peaking EQ, RBJ Audio-EQ-Cookbook formulation.
    //   f0        centre frequency in Hz
    //   sampleRate in Hz
    //   q          quality factor
    //   gainDb     peak gain in dB (positive = boost)
    static BiquadCoefficients peaking(double f0, double sampleRate, double q, double gainDb) {
        if (sampleRate <= 0.0 || f0 <= 0.0 || q <= 0.0) {
            return identity();
        }
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * (f0 / sampleRate);
        const double cosw0 = std::cos(w0);
        const double sinw0 = std::sin(w0);
        const double alpha = sinw0 / (2.0 * q);

        const double a0 = 1.0 + alpha / A;
        const double inv = 1.0 / a0;

        BiquadCoefficients c;
        c.b0 = (1.0 + alpha * A) * inv;
        c.b1 = (-2.0 * cosw0) * inv;
        c.b2 = (1.0 - alpha * A) * inv;
        c.a1 = (-2.0 * cosw0) * inv;
        c.a2 = (1.0 - alpha / A) * inv;
        return c;
    }
};

// Transposed Direct Form II state (two delay elements).
class BiquadState {
public:
    inline void reset() {
        z1_ = 0.0;
        z2_ = 0.0;
    }

    // Process one sample. Coefficients are passed by const-ref so a whole
    // band can share one coefficient set across channels (proposal §18).
    inline float process(float x, const BiquadCoefficients& c) {
        const double in = static_cast<double>(x);
        const double y = c.b0 * in + z1_;
        z1_ = c.b1 * in - c.a1 * y + z2_;
        z2_ = c.b2 * in - c.a2 * y;
        return static_cast<float>(y);
    }

private:
    double z1_ = 0.0;
    double z2_ = 0.0;
};

}  // namespace audiofx
}  // namespace just_audio

#endif  // JUST_AUDIO_AUDIOFX_BIQUAD_HPP
