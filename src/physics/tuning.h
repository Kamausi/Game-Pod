// Live-tunable multipliers (edited from the Dear ImGui dev panel, F1 / backtick).
// Defaults of 1.0 reproduce the calibrated model documented in docs/PHYSICS.md.
#pragma once

namespace mb {

struct Tuning {
    float soilStiffness = 1.f;   // Bekker k_eq
    float soilViscosity = 1.f;   // sinkage rate resistance (higher = momentum matters less)
    float cohesion = 1.f;        // slurry cohesion c
    float friction = 1.f;        // slurry tan(phi)
    float digRate = 1.f;         // slip excavation
    float churn = 1.f;           // paddle-wheel drag on spinning tires
    float suction = 1.f;         // belly + tire suction
    float reflow = 1.f;          // soupy mud filling ruts back in
    float spring = 1.f, damper = 1.f;
    float engineTorque = 1.f;
    float firmGrip = 1.f;
};

extern Tuning g_tuning;

}  // namespace mb
