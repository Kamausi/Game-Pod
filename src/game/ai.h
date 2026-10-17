// AI rivals for solo events. They drive the same Vehicle physics as the player (and dig the
// same mud): pure-pursuit steering, curvature-limited speed targets, and a real stuck
// strategy - air down, rock it (alternate reverse / forward), then give up like a human would.
#pragma once
#include <string>
#include "../physics/vehicle.h"
#include "modes.h"
#include "world.h"

namespace mb {

struct AiDriver {
    std::string name;
    VehicleConfig cfg;
    Vehicle v;
    float skill = 0.7f;       // 0..1: line accuracy, throttle discipline
    int lane = 1;
    // Stuck handling
    float stuckTimer = 0, rockTimer = 0;
    int rockPhase = 0, rockCycles = 0;
    bool aired = false, gaveUp = false;
    // Progress / scoring (mirrors ModeRunner rules)
    float distance = 0, finishTime = 0, bestProgress = -1e9f, progressTimer = 0;
    bool finished = false;
    int nextCp = 1, lap = 0;
    float score = 0, kingTime = 0, wander = 0, cpTimer = 0;
    int tows = 0;
    VehicleInput input;
    float id_score() const { return kingTime > 0 ? kingTime : score; }
};

// Decide inputs for this frame.
void aiThink(AiDriver& ai, ModeId mode, const World& w, float dt, float elapsed, bool running);
// Advance mode-specific scoring for an AI.
void aiProgress(AiDriver& ai, ModeId mode, const World& w, float dt, float elapsed, float ejected);
// A field of rival rigs with distinct builds and personalities.
std::vector<AiDriver> makeRivals(int count, uint32_t seed);

}  // namespace mb
