// Hash noise, value noise, fBm and a small PCG-style RNG.
#pragma once
#include <cmath>
#include <cstdint>

namespace mb {

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline uint32_t hash2i(int x, int y, uint32_t seed = 0) {
    return hash32(uint32_t(x) * 0x9E3779B1u ^ hash32(uint32_t(y) + 0x85ebca6bu + seed));
}
inline float hashf(int x, int y, uint32_t seed = 0) { return (hash2i(x, y, seed) & 0xFFFFFF) / float(0xFFFFFF); }

inline float valueNoise(float x, float y, uint32_t seed = 0) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    float a = hashf(ix, iy, seed), b = hashf(ix + 1, iy, seed);
    float c = hashf(ix, iy + 1, seed), d = hashf(ix + 1, iy + 1, seed);
    return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

inline float fbm(float x, float y, int octaves, uint32_t seed = 0) {
    float sum = 0, amp = 0.5f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * valueNoise(x, y, seed + i * 101u);
        norm += amp;
        x *= 2.03f; y *= 2.03f; amp *= 0.5f;
    }
    return sum / norm;
}

struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed = 0x853c49e6748fea9bULL) : s(seed ? seed : 1) {}
    uint32_t next() {
        uint64_t old = s;
        s = old * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t xs = uint32_t(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = uint32_t(old >> 59u);
        return (xs >> rot) | (xs << ((-rot) & 31));
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    float bipolar() { return uniform() * 2.f - 1.f; }
};

}  // namespace mb
