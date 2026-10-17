// Terramechanics + tire formulas used by Mud Buddies.
// References:
//   Bekker, M.G. "Introduction to Terrain-Vehicle Systems" (1969)
//   Wong, J.Y. "Theory of Ground Vehicles", 4th ed. (2008) - soil table 2.3
//   Janosi & Hanamoto (1961) shear stress-displacement relation
//   Pacejka, H. "Tire and Vehicle Dynamics" - Magic Formula
// See docs/PHYSICS.md for derivations and calibration notes.
#pragma once
#include <cmath>
#include "../core/math.h"

namespace mb {

// Wong table 2.3 (kc in kN/m^(n+1), kphi in kN/m^(n+2), c in kPa, phi in degrees).
// K (shear deformation modulus, m) from Wong ch.2 typical clay values.
struct SoilParams {
    const char* name;
    float n, kc, kphi, c, phiDeg, K;
};
inline constexpr SoilParams kWongSoils[] = {
    {"Dry sand (Land Locomotion Lab)", 1.10f, 0.99f, 1528.43f, 1.04f, 28.f, 0.025f},
    {"Sandy loam, 15% moisture", 0.70f, 5.27f, 1515.04f, 1.72f, 29.f, 0.025f},
    {"Sandy loam, 22% moisture", 0.20f, 2.56f, 43.12f, 1.38f, 38.f, 0.025f},
    {"Clayey soil, 38% moisture", 0.50f, 13.19f, 692.15f, 4.14f, 13.f, 0.010f},
    {"Clayey soil, 55% moisture", 0.70f, 16.03f, 1262.53f, 2.07f, 10.f, 0.010f},
    {"Heavy clay, 25% moisture", 0.13f, 12.70f, 1555.95f, 68.95f, 34.f, 0.006f},
    {"Heavy clay, 40% moisture", 0.11f, 1.84f, 103.27f, 20.69f, 6.f, 0.006f},
    {"Lean clay, 22% moisture", 0.20f, 16.43f, 1724.69f, 68.95f, 20.f, 0.006f},
    {"Lean clay, 32% moisture", 0.15f, 1.52f, 119.61f, 13.79f, 11.f, 0.006f},
};

// Bekker pressure-sinkage: p = (kc/b + kphi) * z^n      [Pa when k in Pa/m^n]
inline float bekkerPressure(float kEq, float n, float z) { return kEq * std::pow(std::max(z, 0.f), n); }
inline float bekkerSinkage(float p, float kEq, float n) { return std::pow(std::max(p, 0.f) / kEq, 1.f / n); }

// Compaction (rolling) resistance from integrating Bekker over sinkage:
// Rc = b * kEq * z^(n+1) / (n+1)
inline float compactionResistance(float b, float kEq, float n, float z) {
    return b * kEq * std::pow(std::max(z, 0.f), n + 1.f) / (n + 1.f);
}

// Janosi-Hanamoto gross tractive effort of a contact patch with slip i (0..1):
// F = (A c + W tan(phi)) * [1 - K/(i L) * (1 - exp(-i L / K))]
inline float janosiHanamoto(float A, float W, float c, float phi, float i, float L, float K) {
    float fmax = A * c + W * std::tan(phi);
    float ai = std::fabs(i);
    if (ai < 1e-4f) return 0.f;
    float x = ai * L / K;
    float shape = 1.f - (1.f - std::exp(-x)) / x;
    return signf(i) * fmax * shape;
}

// Rankine passive earth pressure on a wall of height h, width b (bulldozing resistance):
// Rb = b * (0.5 * gamma * h^2 * Kp + 2 c h sqrt(Kp)),  Kp = tan^2(45deg + phi/2)
inline float rankineBulldozing(float b, float h, float gamma, float c, float phi) {
    if (h <= 0) return 0.f;
    float kp = sq(std::tan(PI * 0.25f + phi * 0.5f));
    return b * (0.5f * gamma * h * h * kp + 2.f * c * h * std::sqrt(kp));
}

// Pacejka Magic Formula: y = D sin(C atan(Bx - E(Bx - atan(Bx))))
inline float magicFormula(float x, float B, float C, float D, float E) {
    float bx = B * x;
    return D * std::sin(C * std::atan(bx - E * (bx - std::atan(bx))));
}

// Minnaert resonance of a gas bubble of radius r (m) in water at 1 atm:
// f = 1/(2 pi r) * sqrt(3 gamma p0 / rho)  ~= 3.26 / r  Hz
inline float minnaertFrequency(float r) {
    const float gamma = 1.4f, p0 = 101325.f, rho = 998.f;
    return std::sqrt(3.f * gamma * p0 / rho) / (2.f * PI * r);
}

// ---------------------------------------------------------------------------------------
// Calibrated "bog slurry" model. Wong's in-situ soils are far too stiff for an excavated,
// water-saturated mud pit (a 56 kPa tire would sink < 1 cm). These curves keep Bekker's
// functional form but are fitted so a ~56 kPa contact pressure sinks:
//   moisture 0.0 -> ~5 mm, 0.3 -> ~2 cm, 0.6 -> ~10 cm, 1.0 -> ~60 cm (straight to hardpan).
// Shear strength comes from interpolating Wong's clay rows (lean clay 22% -> heavy clay 40%)
// down toward a slurry with almost no cohesion.
struct MudModel {
    static float n(float m) { return lerpf(0.9f, 0.8f, clampf(m, 0, 1)); }
    static float kEq(float m) {  // Pa / m^n
        m = clampf(m, 0.f, 1.f);
        // log-linear between calibration points
        const float lk[4] = {std::log(3860e3f), std::log(1281e3f), std::log(354e3f), std::log(84e3f)};
        const float mm[4] = {0.f, 0.3f, 0.6f, 1.0f};
        int s = m < 0.3f ? 0 : (m < 0.6f ? 1 : 2);
        float t = (m - mm[s]) / (mm[s + 1] - mm[s]);
        return std::exp(lerpf(lk[s], lk[s + 1], t));
    }
    static float cohesion(float m) {  // Pa
        m = clampf(m, 0, 1);
        return m < 0.5f ? lerpf(68.95e3f, 20.69e3f, m / 0.5f) * 0.25f : lerpf(5.17e3f, 1.2e3f, (m - 0.5f) / 0.5f);
    }
    static float phi(float m) { return lerpf(20.f, 4.f, clampf(m, 0, 1)) * DEG; }
    static float shearK(float m) { return lerpf(0.012f, 0.04f, clampf(m, 0, 1)); }
    // Pseudo-viscosity (Pa per m/s of sinkage rate): high = slow to sink (momentum helps).
    static float viscosity(float m) {
        m = clampf(m, 0.f, 1.f);
        const float lv[4] = {std::log(2.0e6f), std::log(2.0e5f), std::log(2.2e4f), std::log(7.0e3f)};
        const float mm[4] = {0.f, 0.5f, 0.8f, 1.0f};
        int s = m < 0.5f ? 0 : (m < 0.8f ? 1 : 2);
        float t = (m - mm[s]) / (mm[s + 1] - mm[s]);
        return std::exp(lerpf(lv[s], lv[s + 1], t));
    }
    // "Stickiness" of clay: peaks around the plastic limit (~55-75% moisture in our scale).
    static float stickiness(float m) { return std::exp(-sq((m - 0.65f) / 0.18f)); }
    static constexpr float unitWeight = 17000.f;  // N/m^3 wet clay
    static constexpr float slurryDensity = 1450.f; // kg/m^3
};

}  // namespace mb
