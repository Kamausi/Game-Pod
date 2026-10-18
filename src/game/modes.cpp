#include "modes.h"
#include <cstdio>

namespace mb {

const char* modeName(ModeId id) {
    switch (id) {
        case ModeId::FreeRoam: return "Free Roam";
        case ModeId::MudBog: return "Mud Bog";
        case ModeId::Circuit: return "Hill & Hole";
        case ModeId::Freestyle: return "Mud Slinger";
        case ModeId::King: return "King of the Pit";
        case ModeId::Bounty: return "Bounty Hole";
    }
    return "?";
}

static std::string fmtTime(float t) {
    char b[32];
    int m = int(t / 60);
    std::snprintf(b, sizeof b, "%d:%05.2f", m, t - m * 60);
    return b;
}

Vec3 ModeRunner::spawnFor(const World& w, int lane, float& yaw) const {
    switch (id) {
        case ModeId::MudBog:
            yaw = PI * 0.5f;
            return {w.pitX0 - 14.f, 0, w.laneZ(lane % w.lanes)};
        case ModeId::Circuit: {
            Vec3 a = w.circuit[0], b = w.circuit[1];
            Vec3 d = normalize(b - a);
            yaw = std::atan2(d.x, d.z);
            Vec3 side{d.z, 0, -d.x};
            return a - d * (6.f + 7.f * (lane / 2)) + side * ((lane % 2) ? 1.6f : -1.6f);
        }
        case ModeId::Bounty:
            yaw = PI * 0.5f;
            return {w.bountyX0 - 8.f, 0, w.bountyZ + ((lane % 2) ? 2.6f : -2.6f) * (lane > 0 ? 1.f : 0.f)};
        case ModeId::Freestyle:
        case ModeId::King:
            yaw = PI * 0.5f;
            return {w.bowlCenter.x - w.bowlRadius - 9.f, 0, w.bowlCenter.z + (lane - 1.5f) * 3.5f};
        default:
            yaw = PI * 0.5f;
            return w.garageSpot + Vec3{0, 0, float(lane) * 4.f};
    }
}

void ModeRunner::resetToSpawn(World& w, Vehicle& v) {
    float yaw;
    Vec3 p = spawnFor(w, lane, yaw);
    p.y = w.terrain.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.25f;
    v.reset(p, yaw);
    v.placeOnGround(w.terrain);
}

void ModeRunner::start(ModeId m, World& w, Vehicle& v, int ln, double at) {
    id = m;
    lane = ln;
    startAt = at;
    phase = m == ModeId::FreeRoam ? Phase::Running : Phase::Countdown;
    elapsed = distance = score = penalty = kingTime = 0;
    combo = 1; comboTimer = 0;
    lap = 0; nextCp = 1; lapStart = 0; bestLap = 0; lastLap = 0;
    stuckTimer = progressTimer = 0;
    bestProgress = -1e9f;
    stuck = false;
    resultCount = 0;
    claimed = false;
    timeLimit = m == ModeId::Freestyle ? 90.f : (m == ModeId::King ? 120.f : 0.f);
    resetToSpawn(w, v);
    if (m != ModeId::FreeRoam) v.gear = 0;  // rev it on the line
    flash(m == ModeId::Bounty ? "Bounty Hole - $" + std::to_string(bountyPot) + " on the line" : std::string(modeName(m)), 2.5f);
}

void ModeRunner::finish(const std::string& msg) {
    if (phase == Phase::Finished) return;
    phase = Phase::Finished;
    resultCount = 0;
    auto add = [&](const char* board, float val, bool lower, const std::string& s) {
        if (resultCount < 2) results[resultCount++] = {true, board, val, lower, s};
    };
    switch (id) {
        case ModeId::MudBog:
            if (distance >= 79.9f) add("bog_time", elapsed, true, "Full pass " + fmtTime(elapsed));
            add("bog_distance", distance, false, std::to_string(int(distance * 3.281f)) + " ft");
            break;
        case ModeId::Circuit:
            if (lap >= laps) add("circuit_race", elapsed + penalty, true, "Race " + fmtTime(elapsed + penalty));
            if (bestLap > 0) add("circuit_lap", bestLap, true, "Best lap " + fmtTime(bestLap));
            break;
        case ModeId::Freestyle: add("freestyle_score", score, false, std::to_string(int(score)) + " pts"); break;
        case ModeId::King: add("king_time", kingTime, false, fmtTime(kingTime) + " crowned"); break;
        case ModeId::Bounty:
            if (claimed) add("bounty_time", elapsed, true, "Bounty claimed in " + fmtTime(elapsed));
            add("bounty_distance", distance, false, std::to_string(int(distance * 3.281f)) + " ft into the hole");
            break;
        default: break;
    }
    flash(msg, 6.f);
}

void ModeRunner::requestTow(World& w, Vehicle& v) {
    if (phase == Phase::Running && id == ModeId::Bounty && v.pos.x > w.bountyX0 - 2.f) {
        bountyPot += 250;
        finish("Dozer tow - the hole wins. Pot grows to $" + std::to_string(bountyPot));
        return;
    }
    if (phase == Phase::Running && id == ModeId::MudBog && v.pos.x > w.pitX0) {
        finish("Towed out by the dozer - " + std::to_string(int(distance * 3.281f)) + " ft");
        return;
    }
    // Find the nearest firm spot, preferring the direction of travel.
    Vec3 fwd = normalize(Vec3{v.forward().x, 0, v.forward().z});
    if (id == ModeId::Circuit) fwd = normalize(w.circuit[size_t(nextCp) % w.circuit.size()] - v.pos);
    Vec3 best = v.pos;
    float bestScore = 1e9f;
    for (float r = 3; r <= 30; r += 1.5f)
        for (int a = 0; a < 24; a++) {
            float ang = 2 * PI * a / 24;
            Vec3 c = v.pos + Vec3{std::cos(ang), 0, std::sin(ang)} * r;
            TerrainSample s = w.terrain.sample(c.x, c.z);
            if (s.mudDepth() > 0.06f && s.moisture > 0.5f) continue;
            if (s.waterDepth > 0.02f) continue;
            float sc = r - 6.f * dot(normalize(c - v.pos), fwd);
            if (sc < bestScore) { bestScore = sc; best = c; }
        }
    best.y = w.terrain.heightAt(best.x, best.z) - v.rideAxleY + v.wheels[0].radius + 0.3f;
    float yaw = std::atan2(fwd.x, fwd.z);
    float keepDirt = v.dirt;
    v.reset(best, yaw);
    v.placeOnGround(w.terrain);
    v.dirt = keepDirt;
    if (id == ModeId::Circuit) penalty += 10.f;
    if (id == ModeId::Freestyle) { score = std::max(0.f, score - 250.f); combo = 1; }
    flash(id == ModeId::Circuit ? "Dozer tow: +10 s" : "The dozer pulled you out");
    stuckTimer = 0;
}

void ModeRunner::update(float dt, double now, World& w, Vehicle& v, float ejected, float splash, int barrels) {
    if (messageTimer > 0) messageTimer -= dt;
    if (phase == Phase::Countdown && now >= startAt) {
        phase = Phase::Running;
        elapsed = 0;
        flash("GO!", 1.2f);
    }
    // Stuck detection (all modes): pinned throttle, no motion, spinning or high-centered.
    bool pinned = v.throttleApplied > 0.4f && v.speed() < 0.5f && (v.avgSlip > 0.5f || v.bellyLoad > 0.3f);
    stuckTimer = pinned ? stuckTimer + dt : std::max(0.f, stuckTimer - 2.f * dt);
    stuck = stuckTimer > 2.5f;

    if (phase != Phase::Running) return;
    elapsed += dt;
    switch (id) {
        case ModeId::MudBog: {
            distance = clampf(v.pos.x - w.pitX0, 0.f, w.pitX1 - w.pitX0);
            if (v.pos.x > bestProgress + 0.3f) { bestProgress = v.pos.x; progressTimer = 0; }
            else progressTimer += dt;
            // No clock: the run lasts until a full pass, leaving the lane, or calling the dozer
            // (T / View / TOW), which scores the distance reached.
            if (v.pos.x >= w.pitX1) finish("FULL PASS! " + fmtTime(elapsed));
            else if (std::fabs(v.pos.z - w.laneZ(lane % w.lanes)) > 7.5f && v.pos.x > w.pitX0) finish("Out of lane!");
            else if (stuck && progressTimer > 8.f && messageTimer <= 0) flash("Stuck! Rock it, air down - or T to call the dozer", 3.f);
            break;
        }
        case ModeId::Circuit: {
            const Vec3& cp = w.circuit[size_t(nextCp)];
            if (sq(cp.x - v.pos.x) + sq(cp.z - v.pos.z) < 81.f) {
                if (nextCp == 0) {
                    lap++;
                    lastLap = elapsed - lapStart;
                    lapStart = elapsed;
                    if (bestLap <= 0 || lastLap < bestLap) bestLap = lastLap;
                    if (lap >= laps) { finish("FINISH " + fmtTime(elapsed + penalty)); break; }
                    flash("Lap " + std::to_string(lap) + "  " + fmtTime(lastLap));
                }
                nextCp = (nextCp + 1) % int(w.circuit.size());
            }
            score = float(lap);
            break;
        }
        case ModeId::Freestyle: {
            float d = length(Vec3{v.pos.x - w.bowlCenter.x, 0, v.pos.z - w.bowlCenter.z});
            float pts = 0;
            if (d < w.bowlRadius + 4) {
                pts += ejected * 4000.f;
                if (v.airTime > 0.3f) pts += v.airTime * dt * 450.f;
                pts += splash * 40.f;
                float yawRate = std::fabs(dot(v.angVel, v.up()));
                if (v.speed() > 4.f) pts += yawRate * v.speed() * dt * 6.f;
                pts += barrels * 60.f;
            }
            if (stuck) { combo = 1; comboTimer = 0; }
            if (pts > 0.5f * dt * 60.f) { comboTimer = 2.f; combo = std::min(5.f, combo + dt * 0.3f); }
            else if ((comboTimer -= dt) <= 0) combo = 1;
            score += pts * combo;
            if (elapsed >= timeLimit) finish("Time! " + std::to_string(int(score)) + " pts");
            break;
        }
        case ModeId::Bounty: {
            float len = w.bountyX1 - w.bountyX0;
            distance = clampf(v.pos.x - w.bountyX0, 0.f, len);
            score = float(bountyPot);
            if (v.pos.x > bestProgress + 0.3f) { bestProgress = v.pos.x; progressTimer = 0; }
            else progressTimer += dt;
            if (v.pos.x >= w.bountyX1) {
                claimed = true;
                int won = bountyPot;
                bountyPot = 1500;
                finish("BOUNTY CLAIMED! $" + std::to_string(won) + " in " + fmtTime(elapsed));
            } else if (std::fabs(v.pos.z - w.bountyZ) > w.bountyHalfW + 3.f && v.pos.x > w.bountyX0) {
                bountyPot += 250;
                finish("Out of bounds! Pot grows to $" + std::to_string(bountyPot));
            } else if (stuck && progressTimer > 8.f && messageTimer <= 0) flash("The hole's got you - rock it, or T for the dozer", 3.f);
            break;
        }
        case ModeId::King: {
            float d = length(Vec3{v.pos.x - w.bowlCenter.x, 0, v.pos.z - w.bowlCenter.z});
            if (d < w.kingRadius && (v.speed() > 0.8f || v.avgSlip > 0.3f)) kingTime += dt;
            score = kingTime;
            if (elapsed >= timeLimit) finish("Crown time " + fmtTime(kingTime));
            break;
        }
        default: break;
    }
}

}  // namespace mb
