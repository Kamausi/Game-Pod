#include "synth.h"

namespace mb {

// Robert Bristow-Johnson "Audio EQ Cookbook" formulas.
static void rbjCommon(float f, float q, float sr, float& w0, float& alpha) {
    f = std::fmin(std::fmax(f, 10.f), sr * 0.45f);
    w0 = 2.f * 3.14159265f * f / sr;
    alpha = std::sin(w0) / (2.f * std::fmax(q, 0.05f));
}

void Biquad::lowpass(float f, float q, float sr) {
    float w0, al;
    rbjCommon(f, q, sr, w0, al);
    float c = std::cos(w0), a0 = 1 + al;
    b0 = (1 - c) * 0.5f / a0; b1 = (1 - c) / a0; b2 = b0;
    a1 = -2 * c / a0; a2 = (1 - al) / a0;
}

void Biquad::highpass(float f, float q, float sr) {
    float w0, al;
    rbjCommon(f, q, sr, w0, al);
    float c = std::cos(w0), a0 = 1 + al;
    b0 = (1 + c) * 0.5f / a0; b1 = -(1 + c) / a0; b2 = b0;
    a1 = -2 * c / a0; a2 = (1 - al) / a0;
}

void Biquad::bandpass(float f, float q, float sr) {
    float w0, al;
    rbjCommon(f, q, sr, w0, al);
    float c = std::cos(w0), a0 = 1 + al;
    b0 = al / a0; b1 = 0; b2 = -al / a0;
    a1 = -2 * c / a0; a2 = (1 - al) / a0;
}

void Biquad::peak(float f, float q, float gainDb, float sr) {
    float w0, al;
    rbjCommon(f, q, sr, w0, al);
    float A = std::pow(10.f, gainDb / 40.f), c = std::cos(w0), a0 = 1 + al / A;
    b0 = (1 + al * A) / a0; b1 = -2 * c / a0; b2 = (1 - al * A) / a0;
    a1 = -2 * c / a0; a2 = (1 - al / A) / a0;
}

}  // namespace mb
