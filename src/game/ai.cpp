#include "ai.h"
#include "../core/noise.h"

namespace mb {

std::vector<AiDriver> makeRivals(int count, uint32_t seed) {
    struct Preset {
        const char* name;
        const char* cfg;
        float skill;
    };
    static const Preset presets[] = {
        {"Dale", "body=bogcar76;engine=v8_454;tire=33;width=12.5;tread=mt;lift=3;axle=4.56;lockF=0;lockR=1;awd=0;psi=16;paint=e0e0dc", 0.62f},
        {"Tammy", "body=silverado;engine=duramax;tire=38;width=14.5;tread=paddle;lift=6;axle=5.38;lockF=1;lockR=1;awd=1;snorkel=1;psi=12;paint=9e8b6a", 0.8f},
        {"Skeeter", "body=utv;engine=twin_1000t;tire=32;width=11.5;tread=mt;lift=2;axle=4.88;lockF=1;lockR=1;awd=1;psi=10;paint=e8e8e4", 0.74f},
        {"Bubba", "body=comanche;engine=v8_350;tire=37;width=13.5;tread=mt;lift=5;axle=4.88;lockF=0;lockR=1;awd=1;psi=14;paint=6a2a1e", 0.68f},
        {"Rhonda", "body=comanche;engine=ls53_sc;tire=40;width=15.5;tread=paddle;lift=7;axle=5.38;lockF=1;lockR=1;awd=1;snorkel=1;psi=9;paint=1d4e8a", 0.86f},
    };
    Rng rng(seed + 31);
    std::vector<AiDriver> out;
    int start = int(rng.next() % 5);
    for (int i = 0; i < count; i++) {
        const Preset& p = presets[(start + i) % 5];
        AiDriver a;
        a.name = p.name;
        a.cfg = VehicleConfig::parse(std::string("name=") + p.name + ";" + p.cfg);
        a.skill = clampf(p.skill + rng.range(-0.06f, 0.06f), 0.4f, 0.95f);
        a.v.configure(a.cfg);
        a.lane = i + 1;
        out.push_back(std::move(a));
    }
    return out;
}

static float steerToward(const Vehicle& v, Vec3 target, float gain) {
    Vec3 l = v.rot.unrotate(target - v.pos);
    float ang = std::atan2(l.x, std::max(0.5f, l.z));  // +X is left
    return clampf(ang * gain, -1.f, 1.f);
}

void aiThink(AiDriver& ai, ModeId mode, const World& w, float dt, float elapsed, bool running) {
    Vehicle& v = ai.v;
    VehicleInput in;
    if (!running || ai.finished || ai.gaveUp) {
        in.brake = ai.finished || ai.gaveUp ? 0.f : 0.f;
        in.handbrake = true;
        // Rev on the line during the countdown like a real bogger.
        if (!running && mode != ModeId::FreeRoam) in.throttle = std::fmod(elapsed * 1.7f + ai.lane, 1.f) < 0.45f ? 0.9f : 0.f;
        ai.input = in;
        return;
    }
    ai.wander += dt * (0.4f + 0.6f * (1.f - ai.skill));
    float spd = v.speed();
    Vec3 target = v.pos + v.forward() * 10.f;
    float targetSpeed = 30.f, gain = 1.6f + ai.skill;
    switch (mode) {
        case ModeId::MudBog: {
            target = {v.pos.x + 14.f, 0, w.laneZ(ai.lane % w.lanes) + std::sin(ai.wander) * (1.f - ai.skill) * 1.2f};
            targetSpeed = 40.f;
            break;
        }
        case ModeId::Circuit: {
            const Vec3& cp = w.circuit[size_t(ai.nextCp) % w.circuit.size()];
            const Vec3& nx = w.circuit[size_t(ai.nextCp + 1) % w.circuit.size()];
            float dcp = length(Vec3{cp.x - v.pos.x, 0, cp.z - v.pos.z});
            float blend = clampf(1.f - dcp / 22.f, 0.f, 0.7f);
            target = lerp(cp, nx, blend);
            // Corner speed from the turn angle at the upcoming checkpoint, then the fastest speed
            // from which we can still brake down to it: v = sqrt(vc^2 + 2 a d).
            Vec3 d1 = normalize(cp - v.pos), d2 = normalize(nx - cp);
            float turn = std::acos(clampf(dot(normalize(Vec3{d1.x, 0, d1.z}), normalize(Vec3{d2.x, 0, d2.z})), -1.f, 1.f));
            float vc = lerpf(20.f, 6.5f, clampf(turn / 1.4f, 0.f, 1.f)) * (0.8f + 0.2f * ai.skill);
            targetSpeed = std::min(24.f, std::sqrt(vc * vc + 2.f * 3.5f * std::max(0.f, dcp - 6.f)));
            break;
        }
        case ModeId::Freestyle: {
            Vec3 c = w.bowlCenter;
            float ang = std::atan2(v.pos.z - c.z, v.pos.x - c.x) + 0.7f;
            float r = 8.f + 3.f * std::sin(elapsed * 0.3f + ai.lane);
            target = c + Vec3{std::cos(ang) * r, 0, std::sin(ang) * r};
            targetSpeed = 13.f;
            break;
        }
        case ModeId::King: {
            Vec3 c = w.bowlCenter;
            float d = length(Vec3{v.pos.x - c.x, 0, v.pos.z - c.z});
            if (d > w.kingRadius) target = c;
            else {
                float ang = std::atan2(v.pos.z - c.z, v.pos.x - c.x) + 0.9f;
                target = c + Vec3{std::cos(ang) * 3.f, 0, std::sin(ang) * 3.f};
            }
            targetSpeed = d > w.kingRadius ? 14.f : 5.f;
            break;
        }
        default: targetSpeed = 0; break;
    }
    in.steer = steerToward(v, target, gain);
    float err = targetSpeed - v.forwardSpeed();
    in.throttle = clampf(err * 0.35f + 0.25f, 0.f, 1.f);
    if (err < -3.f) { in.throttle = 0; in.brake = clampf(-err * 0.15f, 0.f, 1.f); }

    // Stuck strategy: air down -> rock it -> give up (bog) / keep trying (others).
    bool pinned = in.throttle > 0.4f && spd < 0.6f;
    ai.stuckTimer = pinned || ai.rockPhase ? ai.stuckTimer + dt : std::max(0.f, ai.stuckTimer - dt * 2.f);
    if (ai.stuckTimer > 2.2f) {
        if (!ai.aired) { in.airDelta = -1.f; if (v.psi <= 9.f) ai.aired = true; }
        ai.rockTimer += dt;
        float period = ai.rockPhase == 1 ? 0.9f : 1.7f;
        if (ai.rockTimer > period) {
            ai.rockTimer = 0;
            ai.rockPhase = ai.rockPhase == 1 ? 2 : 1;
            if (ai.rockPhase == 1) ai.rockCycles++;
        }
        if (ai.rockPhase == 1) { in.throttle = 0; in.brake = 1; }
        else { in.throttle = 1; in.brake = 0; }
        if (spd > 2.5f && ai.rockPhase == 2) { ai.stuckTimer = 0; ai.rockPhase = 0; ai.rockCycles = 0; }
        if (mode == ModeId::MudBog && ai.rockCycles > 4) ai.gaveUp = true;
    }
    ai.input = in;
}

void aiProgress(AiDriver& ai, ModeId mode, const World& w, float dt, float elapsed, float ejected) {
    Vehicle& v = ai.v;
    if (ai.finished) return;
    switch (mode) {
        case ModeId::MudBog:
            ai.distance = clampf(v.pos.x - w.pitX0, 0.f, w.pitX1 - w.pitX0);
            if (v.pos.x > ai.bestProgress + 0.3f) { ai.bestProgress = v.pos.x; ai.progressTimer = 0; }
            else ai.progressTimer += dt;
            if (v.pos.x >= w.pitX1) { ai.finished = true; ai.finishTime = elapsed; }
            else if (v.pos.x > w.pitX0 && ai.progressTimer > 5.f) ai.gaveUp = true;
            break;
        case ModeId::Circuit: {
            const Vec3& cp = w.circuit[size_t(ai.nextCp)];
            const Vec3& nx = w.circuit[size_t(ai.nextCp + 1) % w.circuit.size()];
            float d2 = sq(cp.x - v.pos.x) + sq(cp.z - v.pos.z);
            // Count it when close, or when we've clearly moved past it toward the next one.
            bool passed = d2 < 144.f || (d2 < 900.f && dot(v.pos - cp, normalize(nx - cp)) > 0.f);
            ai.cpTimer += dt;
            if (ai.cpTimer > 22.f) {
                // Lost the line (or stuck in the water hole): the dozer drags them to the checkpoint.
                Vec3 dir = normalize(Vec3{nx.x - cp.x, 0, nx.z - cp.z});
                Vec3 p{cp.x, 0, cp.z};
                p.y = w.terrain.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.3f;
                v.reset(p, std::atan2(dir.x, dir.z));
                ai.tows++;
                passed = true;
            }
            if (passed) {
                ai.cpTimer = 0;
                if (ai.nextCp == 0) {
                    ai.lap++;
                    if (ai.lap >= 3) { ai.finished = true; ai.finishTime = elapsed; }
                }
                ai.nextCp = (ai.nextCp + 1) % int(w.circuit.size());
            }
            {
                int n = int(w.circuit.size());
                ai.score = float(ai.lap) + float((ai.nextCp - 1 + n) % n) / float(n);
            }
            break;
        }
        case ModeId::Freestyle: {
            float d = length(Vec3{v.pos.x - w.bowlCenter.x, 0, v.pos.z - w.bowlCenter.z});
            if (d < w.bowlRadius + 4) ai.score += ejected * 4000.f * (1.f + ai.skill) + (v.airTime > 0.3f ? v.airTime * dt * 450.f : 0.f);
            break;
        }
        case ModeId::King: {
            float d = length(Vec3{v.pos.x - w.bowlCenter.x, 0, v.pos.z - w.bowlCenter.z});
            if (d < w.kingRadius && (v.speed() > 0.8f || v.avgSlip > 0.3f)) ai.kingTime += dt;
            ai.score = ai.kingTime;
            break;
        }
        default: break;
    }
}

}  // namespace mb
