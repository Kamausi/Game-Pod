// DSP building blocks: RBJ biquads, one-pole filters, noise, fractional delay lines and a
// 1-D digital waveguide (used for exhaust pipe resonance).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mb {

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void lowpass(float f, float q, float sr);
    void highpass(float f, float q, float sr);
    void bandpass(float f, float q, float sr);  // constant 0 dB peak gain
    void peak(float f, float q, float gainDb, float sr);
    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }
};

struct OnePole {
    float a = 0.5f, z = 0;
    void setCutoff(float f, float sr) { a = 1.f - std::exp(-2.f * 3.14159265f * f / sr); }
    float lp(float x) { z += a * (x - z); return z; }
    float hp(float x) { return x - lp(x); }
};

struct DCBlock {
    float x1 = 0, y1 = 0, R = 0.995f;
    float process(float x) { float y = x - x1 + R * y1; x1 = x; y1 = y; return y; }
};

struct Noise {
    uint32_t s = 22222;
    float b0 = 0, b1 = 0, b2 = 0, br = 0;
    explicit Noise(uint32_t seed = 22222) : s(seed ? seed : 1) {}
    float white() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (s & 0xFFFFFF) / float(0x7FFFFF) - 1.f;
    }
    float uni() { return (white() + 1.f) * 0.5f; }
    float pink() {  // Paul Kellet's economy filter
        float w = white();
        b0 = 0.99765f * b0 + w * 0.0990460f;
        b1 = 0.96300f * b1 + w * 0.2965164f;
        b2 = 0.57000f * b2 + w * 1.0526913f;
        return (b0 + b1 + b2 + w * 0.1848f) * 0.2f;
    }
    float brown() { br = (br + 0.02f * white()) / 1.02f; return br * 3.5f; }
};

struct DelayLine {
    std::vector<float> buf;
    int w = 0;
    void init(int maxSamples) { buf.assign(size_t(std::max(4, maxSamples)), 0.f); w = 0; }
    void write(float x) { buf[size_t(w)] = x; w = (w + 1) % int(buf.size()); }
    float read(float d) const {
        int n = int(buf.size());
        d = std::fmin(std::fmax(d, 1.f), float(n - 2));
        float rp = float(w) - d;
        while (rp < 0) rp += float(n);
        int i0 = int(rp);
        float fr = rp - float(i0);
        return buf[size_t(i0)] * (1 - fr) + buf[size_t((i0 + 1) % n)] * fr;
    }
};

// Exhaust pipe as a lossy waveguide: pressure pulses travel to the open end and reflect
// with inverted phase (open-end reflection coefficient ~ -0.6..-0.8), low-passed by losses.
struct Waveguide {
    DelayLine dl;
    OnePole loss;
    float refl = -0.7f;
    void init(float sr) { dl.init(int(sr * 0.05f)); loss.setCutoff(2800.f, sr); }
    float process(float x, float delaySamples) {
        float back = dl.read(delaySamples);
        float y = x + refl * loss.lp(back);
        dl.write(y);
        return y;
    }
};

inline float softclip(float x) {
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);  // Padé tanh
}

}  // namespace mb
