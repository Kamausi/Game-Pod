// Competitive modes and scoring rules.
//   Mud Bog     - classic drag pit: full pass = time (lower wins), otherwise distance (farther wins).
//                 Run ends when forward progress stops for 5 s, on a tow, or leaving your lane.
//   Hill & Hole - 3-lap circuit through the two-track, whoops, water hole and spoil mound.
//   Mud Slinger - 90 s freestyle in the bowl: roost volume, airtime, splashes, combo multiplier.
//   King of Pit - 120 s, hold the center puddle while moving; most crown time wins.
//   Free Roam   - no clock.
#pragma once
#include <string>
#include "../physics/vehicle.h"
#include "world.h"

namespace mb {

enum class ModeId : int { FreeRoam = 0, MudBog = 1, Circuit = 2, Freestyle = 3, King = 4 };
enum class Phase : int { Idle, Countdown, Running, Finished };

struct ModeResult {
    bool valid = false;
    std::string board;     // leaderboard key
    float value = 0;       // time (s), distance (m) or score
    bool lowerIsBetter = false;
    std::string summary;
};

class ModeRunner {
public:
    void start(ModeId id, World& w, Vehicle& v, int lane, double startAt);
    // Advances timers/scoring. `now` is the shared (server) clock for synchronized starts.
    void update(float dt, double now, World& w, Vehicle& v, float ejectedVolume, float splash, int barrelsKnocked);
    void requestTow(World& w, Vehicle& v);
    void resetToSpawn(World& w, Vehicle& v);
    // Spawn location/heading for any lane of the current mode (also used for AI drivers).
    Vec3 spawnFor(const World& w, int lane, float& yaw) const;

    ModeId id = ModeId::FreeRoam;
    Phase phase = Phase::Idle;
    double startAt = 0;
    float elapsed = 0, timeLimit = 0;
    float distance = 0, score = 0, combo = 1, comboTimer = 0;
    int lap = 0, laps = 3, nextCp = 0;
    float lapStart = 0, bestLap = 0, lastLap = 0, penalty = 0;
    float kingTime = 0;
    float stuckTimer = 0, progressTimer = 0, bestProgress = -1e9f;
    bool stuck = false;
    int lane = 0;
    std::string message;
    float messageTimer = 0;
    ModeResult results[2];
    int resultCount = 0;
    float countdownLeft(double now) const { return float(startAt - now); }
    bool controlsLocked(double now) const { return phase == Phase::Countdown && now < startAt; }

private:
    void finish(const std::string& msg);
    void flash(const std::string& m, float t = 2.5f) { message = m; messageTimer = t; }
};

const char* modeName(ModeId id);

}  // namespace mb
