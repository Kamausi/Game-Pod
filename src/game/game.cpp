#include "game.h"
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include "../physics/terramechanics.h"

namespace mb {

namespace {
int lugCount(Tread t) { return t == Tread::Paddle ? 12 : (t == Tread::AllTerrain ? 26 : 18); }
const float kMudDry[3] = {0.42f, 0.34f, 0.25f}, kMudWet[3] = {0.22f, 0.17f, 0.12f};
bool g_userManual = false;
int g_finishSerial = 0;
Phase g_lastPhase = Phase::Idle;
bool g_pauseRequested = false;
VehicleSound g_playerSound;
}  // namespace

void VehicleVisual::build(Renderer& r, const VehicleConfig& c) {
    const BodySpec& b = findBody(c.body);
    body = r.mesh(b.cadPart, {{"paint_r", c.paint[0]}, {"paint_g", c.paint[1]}, {"paint_b", c.paint[2]}});
    float tr = c.tireIn * 0.0254f * 0.5f;
    wheel = r.mesh("wheels/wheel.mbcad", {{"tire_r", tr},
                                          {"tire_w", c.widthIn * 0.0254f},
                                          {"rim_r", c.tireIn >= 50 ? 0.3048f : (c.body == "atv" ? 0.1651f : (c.tireIn >= 38 ? 0.2159f : 0.1905f))},
                                          {"lugs", float(lugCount(c.tread))},
                                          {"depth", c.tread == Tread::AllTerrain ? 0.018f : 0.028f},
                                          {"tread", float(int(c.tread))},
                                          {"beadlock", c.beadlock ? 1.f : 0.f}});
}

double Game::serverNow() const { return emscripten_get_now() / 1000.0 + clockOffset; }

void Game::init(int w, int h) {
    cadSetRoot("/cad");
    world.generate(1976);
    renderer.init(w, h);
    renderer.loadWorld(world);
    input.init();
    audio.init(48000.f);
    VehicleConfig c;
    player.configure(c);
    player.pads = &recovery.pads;
    playerVis.build(renderer, c);
    mode.start(ModeId::FreeRoam, world, player, 0, 0);
    lastMs = emscripten_get_now();
}

void Game::setConfig(const std::string& s) {
    VehicleConfig c = VehicleConfig::parse(s);
    Vec3 p = player.pos, v = player.vel;
    Quat q = player.rot;
    float oldRide = -player.rideAxleY + player.wheels[0].radius;
    float dirt = player.dirt;
    bool manual = player.manual;
    int g = player.gear;
    player.configure(c);
    float newRide = -player.rideAxleY + player.wheels[0].radius;
    player.reset(p + Vec3{0, std::max(0.f, newRide - oldRide) + 0.05f, 0}, 0);
    player.rot = q;
    player.vel = v;
    player.dirt = dirt;
    player.manual = manual;
    player.gear = g;
    if (!c.winch) recovery.winch.active = false;
    playerVis.build(renderer, c);
}

void Game::setState(AppState s) {
    state = s;
    input.enabled = s == AppState::Playing || s == AppState::Garage;  // garage: orbit only
    g_pauseRequested = false;
    if (s == AppState::Garage || s == AppState::Attract) {
        if (mode.id != ModeId::FreeRoam || s == AppState::Garage) {
            mode.start(ModeId::FreeRoam, world, player, 0, 0);
            if (s == AppState::Garage) {
                Vec3 p = world.garageSpot;
                p.y = world.terrain.heightAt(p.x, p.z) - player.rideAxleY + player.wheels[0].radius + 0.1f;
                player.reset(p, PI * 0.5f);
                player.placeOnGround(world.terrain);
                player.repair();
                recovery.reset();
            }
        }
    }
}

void Game::startMode(ModeId m, int lane, double startAt) {
    lens.clear();
    if (startAt <= 0) startAt = serverNow() + 3.5;
    world.resetDynamics();
    particles.clear();
    g_userManual = player.manual;
    mode.start(m, world, player, lane, startAt);
    player.repair();
    recovery.reset();
    recoverMenu = false;
    if (m != ModeId::FreeRoam) player.manual = true;  // hold neutral during the countdown
    for (int i = 0; i < 3; i++) audio.removeVehicle(100 + i);
    ais.clear();
    aiVis.clear();
    aiReleased = false;
    runTrace.clear();
    traceTimer = 0;
    if (m != ModeId::FreeRoam && m != ModeId::Bounty && aiCount > 0 && remotes.empty()) {
        ais = makeRivals(aiCount, uint32_t(serverNow() * 7.0));
        for (size_t i = 0; i < ais.size(); i++) {
            AiDriver& a = ais[i];
            a.lane = (lane + 1 + int(i)) % 4;
            float yaw;
            Vec3 p = mode.spawnFor(world, a.lane, yaw);
            p.y = world.terrain.heightAt(p.x, p.z) - a.v.rideAxleY + a.v.wheels[0].radius + 0.25f;
            a.v.reset(p, yaw);
            a.v.placeOnGround(world.terrain);
            a.v.gear = 0;
            a.v.manual = true;
            VehicleVisual vis;
            vis.build(renderer, a.cfg);
            aiVis.push_back(vis);
        }
    }
    player.dirt *= 0.5f;
    camYaw = camPitch = 0;
    announceQ.clear();
    announce.clear();
    announceT = 0;
    aiState.assign(ais.size(), 0);
    auto who = [&](const VehicleConfig& c) {
        return std::string(findBody(c.body).name) + ", " + specSheet(c).cls + " class";
    };
    if (m == ModeId::MudBog) {
        announceLine("Lane " + std::to_string(lane % world.lanes + 1) + ": " + player.cfg.name + " in the " + who(player.cfg));
        for (auto& a : ais) announceLine("Lane " + std::to_string(a.lane % world.lanes + 1) + ": " + a.name + " in '" + a.rig + "' - " + who(a.cfg));
    } else if (m == ModeId::Bounty) {
        announceLine("Bounty Hole: $" + std::to_string(mode.bountyPot) + " on the line - clear the trench and it's yours");
        announceLine("Next up: " + player.cfg.name + " in the " + who(player.cfg));
    } else if (m == ModeId::Circuit) {
        announceLine("Hill & Hole, three laps: " + player.cfg.name + " in the " + who(player.cfg));
        for (auto& a : ais) announceLine(a.name + " in '" + a.rig + "'");
    } else if (m == ModeId::FreeRoam) {
        announceLine("Open pits are open! Paddock, staging, the bounty hole east, camping through the east gap");
    }
    setState(AppState::Playing);
}

bool Game::competitionRun() const {
    return mode.id != ModeId::FreeRoam && mode.phase == Phase::Running;
}

void Game::action(const std::string& a) {
    if (a == "tow" || a == "dozer") {
        recoverMenu = false;
        recovery.reset();
        mode.requestTow(world, player);
    } else if (a == "recover") {
        recoverMenu = !recoverMenu;
    } else if (a == "scout") {
        recoverMenu = false;
        say(recovery.scout(world, player), 6.f);
    } else if (a == "shovel" || a == "boards" || a == "winch") {
        recoverMenu = false;
        if (mode.judged()) { say("No self-recovery in a judged pull - call the dozer (it ends the run)"); return; }
        if (a == "shovel") say(recovery.startShovel(player), 3.f);
        else if (a == "boards") say(recovery.placeBoards(world, player), 3.f);
        else say(recovery.toggleWinch(world, player), 3.f);
    } else if (a == "wash") {   // garage: pressure-wash and fix everything that broke
        player.dirt = 0;
        player.repair();
        recovery.reset();
        say("Washed and repaired: mud off the wheels, radiator and brakes, parts replaced", 3.f);
    } else if (a == "locker") {
        // Cycle through the selectable lockers this truck has; a spool is always locked.
        bool selF = player.diffF == Diff::Locker && player.fourWD, selR = player.diffR == Diff::Locker;
        if (!selF && !selR) { say(player.diffR == Diff::Spool ? "Rear is spooled - always locked" : "No selectable lockers fitted"); return; }
        if (player.speed() > 4.5f || player.avgSlip > 0.5f) { say("Engage lockers BEFORE the hole - ease off and slow down"); return; }
        int st = (player.lockRear && selR ? 1 : 0) + (player.lockFront && selF ? 2 : 0);
        int next = st;
        for (int k = 0; k < 4; k++) {
            next = (next + 1) % 4;
            if ((next & 1) && !selR) continue;
            if ((next & 2) && !selF) continue;
            if (next == 2 && selR) continue;   // front-only isn't a useful state when the rear can lock
            break;
        }
        if (selR) player.lockRear = next & 1;
        if (selF) player.lockFront = (next & 2) != 0;
        bool f = player.locked(true), r = player.locked(false);
        say(f && r ? "Front + rear LOCKED - steering will push, keep it straight" : (r ? "Rear locked" : (f ? "Front locked" : "Lockers open")), 1.8f);
    } else if (a == "transfer") {
        int t = player.transfer();
        int next = (t + 1) % 3;
        if (next == 2 && player.speed() > 1.5f) next = 0;           // 4L only when nearly stopped
        if (t == 2 && player.speed() > 1.5f) { say("Stop to shift out of 4-LO"); return; }
        if (!player.cfg.fourWD && next >= 1) { say("2WD only - no transfer case"); return; }
        player.setTransfer(next);
        if (!player.fourWD) player.lockFront = false;
        say(next == 0 ? "2H - rear drive" : (next == 1 ? "4H - part-time 4WD: don't turn hard on grippy ground" : "4L - low range: torque x2.72, crawl and don't spin"), 2.f);
    } else if (a == "reset") {
        if (mode.judged()) mode.requestTow(world, player);
        else {
            Vec3 f = player.forward();
            float yaw = std::atan2(f.x, f.z);
            Vec3 p = player.pos;
            p.y = world.terrain.heightAt(p.x, p.z) - player.rideAxleY + player.wheels[0].radius + 1.0f;
            float d = player.dirt;
            player.reset(p, yaw);
            player.placeOnGround(world.terrain);
            player.dirt = d;
        }
    } else if (a == "camera") camMode = (camMode + 1) % 3;
    else if (a == "horn") { audio.trigger(SfxType::Horn, player.pos, 1.f); hornTimer = 0.7f; }
    else if (a == "restart") startMode(mode.id, mode.lane, 0);
    else if (a.rfind("dirt:", 0) == 0) player.dirt = clampf(float(std::atof(a.c_str() + 5)), 0.f, 1.f);  // photo mode
    else if (a.rfind("camyaw:", 0) == 0) {
        camYaw = float(std::atof(a.c_str() + 7));
        garageSpin = false;
        camTarget = player.pos + Vec3{0, 0.5f, 0};
        camPos = camTarget + Vec3{std::sin(camYaw) * 7.f, 2.2f, std::cos(camYaw) * 7.f};
    }
    else if (a.rfind("ruts:", 0) == 0) {  // photo mode: ruts:x,z,headingDeg,length - press a pair of tire ruts
        float x = 0, z = 0, hd = 0, len = 8;
        std::sscanf(a.c_str() + 5, "%f,%f,%f,%f", &x, &z, &hd, &len);
        Vec3 f{std::sin(hd * DEG), 0, std::cos(hd * DEG)}, l{f.z, 0, -f.x};
        for (float t = 0; t < len; t += 0.1f)
            for (int side = -1; side <= 1; side += 2) {
                Vec3 c = Vec3{x, 0, z} + f * t + l * (0.74f * float(side));
                c.y = world.terrain.heightAt(c.x, c.z);
                world.terrain.digPatch(c, f, 0.2f, 0.17f, 0.012f);
                world.terrain.scuff(c, 0.16f, 0.3f, 0.5f, std::atan2(f.z, f.x));
            }
    }
    else if (a.rfind("photo:", 0) == 0) {  // photo mode orbit: photo:yawDeg,dist,height (photo:off to release)
        photoCam = a != "photo:off";
        if (photoCam) std::sscanf(a.c_str() + 6, "%f,%f,%f", &photoYaw, &photoDist, &photoH);
    }
    else if (a.rfind("warp:", 0) == 0) {  // debug/photo-mode: warp:x,z,yawDegrees
        float x = 0, z = 0, yaw = 90;
        std::sscanf(a.c_str() + 5, "%f,%f,%f", &x, &z, &yaw);
        Vec3 p{x, world.terrain.heightAt(x, z) - player.rideAxleY + player.wheels[0].radius + 0.2f, z};
        player.reset(p, yaw * DEG);
        player.placeOnGround(world.terrain);
        camPos = p + Vec3{-std::sin(yaw * DEG) * 8.f, 3.f, -std::cos(yaw * DEG) * 8.f};
    }
}

void Game::simulate(float dt, const InputState& in) {
    VehicleInput vi;
    bool playing = state == AppState::Playing;
    if (playing) {
        vi.throttle = in.throttle;
        vi.brake = in.brake;
        vi.steer = in.steer;
        vi.handbrake = in.handbrake;
        vi.airDelta = in.air;
        // Light countersteer assist when the rear steps out on firm ground.
        if (steeringAssist && std::fabs(player.forwardSpeed()) > 5.f) {
            Vec3 v = player.vel;
            float slip = std::atan2(dot(v, player.left()), std::fabs(dot(v, player.forward())));
            vi.steer = clampf(vi.steer + slip * 0.35f, -1.f, 1.f);
        }
    }
    if (mode.controlsLocked(serverNow())) {
        vi.handbrake = true;
        vi.brake = 0;
        player.gear = 0;
    }
    player.step(world.terrain, vi, dt);
    world.collideStatic(player, dt);
    bool locked = mode.controlsLocked(serverNow());
    for (auto& a : ais) {
        VehicleInput ai = a.input;
        if (locked) { ai.handbrake = true; ai.brake = 0; a.v.gear = 0; }
        a.v.step(world.terrain, ai, dt);
        world.collideStatic(a.v, dt);
    }
    // Vehicle-vs-vehicle contacts: player and rivals are dynamic, network replicas kinematic.
    for (size_t i = 0; i < ais.size(); i++) {
        collideVehicles(player, ais[i].v, true);
        for (size_t k = i + 1; k < ais.size(); k++) collideVehicles(ais[i].v, ais[k].v, true);
    }
    for (auto& kv : remotes)
        if (kv.second.valid) collideVehicles(player, kv.second.geom, false);
}

float Game::spawnWheelFx(const Vehicle& vc, float dt) {
    Vehicle& v = const_cast<Vehicle&>(vc);
    float total = 0;
    Rng& R = particles.rng;
    Vec3 U = v.up();
    for (auto& w : v.wheels) {
        float vol = w.ejectVolume;
        w.ejectVolume = 0;
        if (!w.contact) continue;
        float wr = std::fabs(w.omega * w.radius);
        float dirS = signf(w.omega);
        Vec3 back = w.fwdDir * -dirS;
        Vec3 side = cross(U, w.fwdDir);
        Vec3 mcol{lerpf(kMudDry[0], kMudWet[0], w.moisture), lerpf(kMudDry[1], kMudWet[1], w.moisture),
                  lerpf(kMudDry[2], kMudWet[2], w.moisture)};
        if (vol > 0) {
            total += vol;
            int n = std::min(10, int(vol * 2500.f) + 1);
            float spd = clampf(wr, 2.f, 32.f);
            for (int k = 0; k < n; k++) {
                Vec3 vel = back * (spd * R.range(0.3f, 0.7f)) + U * (spd * R.range(0.18f, 0.5f) + 1.f) +
                           side * (R.bipolar() * spd * 0.15f) + v.vel * 0.6f;
                Vec3 p = w.contactPos + U * 0.12f + back * (w.radius * 0.6f) + side * (R.bipolar() * w.width * 0.4f);
                particles.emit(PType::MudChunk, p, vel, R.range(0.05f, 0.14f), 3.f, mcol * R.range(0.85f, 1.1f), vol / n * 0.8f);
            }
            for (int k = 0; k < n * 2; k++) {
                Vec3 vel = back * (spd * R.range(0.4f, 0.9f)) + U * (spd * R.range(0.25f, 0.6f)) + side * (R.bipolar() * spd * 0.25f) + v.vel * 0.6f;
                particles.emit(PType::MudSpray, w.contactPos + U * 0.15f, vel, R.range(0.025f, 0.06f), 1.2f, mcol);
            }
        }
        float vl = std::fabs(dot(v.vel, w.fwdDir));
        if (w.waterDepth > 0.03f && (vl > 1.5f || wr > 3.f)) {
            int n = int(clampf((vl + wr * 0.5f) * 0.35f, 1.f, 7.f));
            for (int k = 0; k < n; k++) {
                float sgn = R.uniform() < 0.5f ? -1.f : 1.f;
                Vec3 vel = side * (sgn * R.range(1.f, 4.f)) + U * R.range(1.f, 3.5f + vl * 0.15f) + back * (wr * 0.25f) + v.vel * 0.7f;
                particles.emit(PType::Water, w.contactPos + U * 0.2f, vel, R.range(0.04f, 0.08f), 1.4f, {0.5f, 0.46f, 0.38f});
            }
            if (R.uniform() < 0.25f) particles.emit(PType::Mist, w.contactPos + U * 0.3f, v.vel * 0.3f + U, 0.3f, 1.4f, {0.75f, 0.72f, 0.66f});
        }
        if (w.mudW < 0.2f && std::fabs(w.slipSpeed) > 3.f && R.uniform() < 0.5f)
            particles.emit(PType::Grass, w.contactPos + U * 0.1f, back * 4.f + U * 2.f, 0.03f, 1.f, {0.35f, 0.42f, 0.18f});
    }
    if (v.engine->diesel && v.throttleApplied > 0.55f && R.uniform() < dt * 40.f) {
        Vec3 tp = v.pos + v.rot.rotate({-0.6f, v.body->bellyY + 0.1f, -v.body->dims[2] * 0.5f});
        particles.emit(PType::Smoke, tp, v.forward() * -2.f + U * 1.2f, 0.25f, 2.5f, {0.12f, 0.12f, 0.12f});
    }
    return total;
}

void Game::updateCamera(float dt, const InputState& in) {
    orbitT += dt;
    Vec3 tgt, pos;
    if (state == AppState::Attract) {
        Vec3 c{0, world.terrain.heightAt(0, world.pitZ), world.pitZ};
        float a = orbitT * 0.05f;
        pos = c + Vec3{std::cos(a) * 58.f, 13.f, std::sin(a) * 58.f};
        tgt = c;
        camPos = lerp(camPos, pos, 1.f - std::exp(-2.f * dt));
        camTarget = lerp(camTarget, tgt, 1.f - std::exp(-2.f * dt));
        fov = 1.0f;
        return;
    }
    if (photoCam && state == AppState::Playing) {
        float yaw = photoYaw * DEG + std::atan2(player.forward().x, player.forward().z);
        Vec3 c = player.pos + Vec3{0, 0.3f, 0};
        camPos = c + Vec3{std::sin(yaw) * photoDist, photoH, std::cos(yaw) * photoDist};
        camTarget = c;
        fov = 0.8f;
        return;
    }
    if (state == AppState::Garage) {
        camYaw += in.lookX + (garageSpin ? dt * 0.25f : 0.f);
        Vec3 c = player.pos + Vec3{0, 0.5f, 0};
        pos = c + Vec3{std::sin(camYaw) * 7.f, 2.2f, std::cos(camYaw) * 7.f};
        camPos = lerp(camPos, pos, 1.f - std::exp(-4.f * dt));
        camTarget = lerp(camTarget, c, 1.f - std::exp(-6.f * dt));
        fov = 0.85f;
        return;
    }
    camYaw += in.lookX;
    camPitch = clampf(camPitch + in.lookY, -0.4f, 0.9f);
    if (std::fabs(in.lookX) < 1e-4f) camYaw = approach(camYaw, 0.f, 1.5f, dt);
    if (std::fabs(in.lookY) < 1e-4f) camPitch = approach(camPitch, 0.f, 1.5f, dt);
    Vec3 f = player.forward();
    Vec3 ff = normalize(Vec3{f.x, 0, f.z});
    float spd = player.speed();
    if (camMode == 2) {
        pos = player.pos + player.rot.rotate({0, player.body->dims[1] * 0.55f, player.body->dims[2] * 0.12f});
        tgt = pos + f * 20.f;
        camPos = pos;
        camTarget = tgt;
        fov = 1.15f;
        return;
    }
    float dist = (camMode == 1 ? 11.f : 6.8f) + player.body->dims[2] * 0.35f + spd * 0.04f;
    float height = (camMode == 1 ? 4.2f : 2.4f) + camPitch * 4.f;
    Quat yq = Quat::axisAngle({0, 1, 0}, camYaw);
    Vec3 dir = yq.rotate(ff);
    pos = player.pos - dir * dist + Vec3{0, height, 0};
    tgt = player.pos + Vec3{0, 1.0f, 0} + dir * 2.5f;
    float gy = world.terrain.heightAt(pos.x, pos.z) + 0.8f;
    if (pos.y < gy) pos.y = gy;
    camPos = lerp(camPos, pos, 1.f - std::exp(-7.f * dt));
    camTarget = lerp(camTarget, tgt, 1.f - std::exp(-12.f * dt));
    camShake = std::max(0.f, camShake - dt * 2.5f);
    float rpmBuzz = player.throttleApplied * (player.engineRpm / player.engine->redlineRpm) * 0.015f;
    Rng& R = particles.rng;
    camPos += Vec3{R.bipolar(), R.bipolar(), R.bipolar()} * (camShake * 0.25f + rpmBuzz);
    fov = approach(fov, 1.05f + clampf(spd / 30.f, 0.f, 1.f) * 0.22f, 3.f, dt);
}

VehicleSound Game::soundFor(const Vehicle& v, bool local, float ejectedVol, float dt) {
    VehicleSound s;
    s.engine = v.engine;
    s.pos = v.pos;
    s.vel = v.vel;
    s.rpm = v.engineRpm;
    s.throttle = v.throttleApplied;
    s.load = clampf(v.throttleApplied * (0.6f + 0.4f * (1.f - v.clutchSlip)), 0.f, 1.f);
    s.boost = v.boost;
    s.fuelCut = v.fuelCut;
    s.running = v.stallTimer <= 0;
    s.local = local;
    int lugs = lugCount(v.cfg.tread);
    float slap = 0, lug = 0, roll = 0, squeal = 0, firm = 0, water = 0, nMud = 0;
    for (auto& w : v.wheels) {
        if (!w.contact) continue;
        float ws = std::fabs(w.omega);
        if (w.mudW > 0.05f) {
            slap += w.mudW * clampf(std::fabs(w.slipSpeed) / 5.f + ws * w.radius / 20.f, 0.f, 1.f);
            lug += ws * lugs / (2.f * PI);
            roll += w.mudW * clampf(std::fabs(dot(v.vel, w.fwdDir)) / 4.f, 0.f, 1.f);
            nMud += 1;
        }
        firm += (1.f - w.mudW);
        squeal += (1.f - w.mudW) * clampf((std::fabs(w.slipAngle) - 0.12f) * 2.f + (std::fabs(w.slipRatio) - 0.25f), 0.f, 1.f);
        if (w.waterDepth > 0.03f) water = std::max(water, v.speed() + ws * w.radius * 0.3f);
    }
    s.mudSlap = clampf(slap / 2.f, 0.f, 1.f);
    s.lugRate = nMud > 0 ? lug / nMud : 0;
    s.mudRoll = clampf(roll / 3.f, 0.f, 1.f);
    s.waterSpeed = std::max(water, v.submerged > 0 ? v.speed() : 0.f);
    s.firmSpeed = v.speed() * firm / 4.f;
    s.squeal = clampf(squeal / 2.f, 0.f, 1.f);
    s.scrape = v.events.bellyScrape;
    s.debris = clampf(ejectedVol / std::max(dt, 1e-3f) * 4000.f, 0.f, 80.f);
    s.tireLugHz = v.speed() / (2.f * PI * v.wheels[0].radius) * lugs;
    return s;
}

void Game::feedAudio(float dt) {
    Vec3 cf = camTarget - camPos;
    audio.setListener(camPos, cf, player.vel);
    VehicleSound s = soundFor(player, true, ejected, dt);
    audio.setVehicle(localId, s);
    g_playerSound = s;
    for (size_t i = 0; i < ais.size(); i++) audio.setVehicle(100 + int(i), soundFor(ais[i].v, false, 0.f, dt));
    for (auto& kv : remotes) {
        Remote& r = kv.second;
        if (!r.valid) continue;
        VehicleSound rs;
        rs.engine = r.geom.engine;
        rs.pos = r.cur.pos;
        rs.vel = r.cur.vel;
        rs.rpm = r.cur.rpm;
        rs.throttle = r.cur.throttle;
        rs.load = r.cur.throttle;
        rs.boost = r.cur.boost;
        rs.fuelCut = r.cur.flags & 1;
        rs.running = (r.cur.flags & 2) != 0;
        rs.mudSlap = r.cur.mudSlap;
        rs.lugRate = r.cur.lugRate;
        rs.mudRoll = r.cur.mudSlap * 0.6f;
        rs.waterSpeed = r.cur.waterSpeed;
        rs.firmSpeed = length(r.cur.vel) * (1.f - r.cur.mudSlap);
        rs.tireLugHz = length(r.cur.vel) / (2.f * PI * r.geom.wheels[0].radius) * lugCount(r.cfg.tread);
        audio.setVehicle(kv.first, rs);
        if (r.cur.flags & 8) audio.trigger(SfxType::Horn, r.cur.pos, 0.02f);
    }
    float crowdD = length(camPos - world.crowdCenter);
    audio.setAmbient(world.dozerPos, 0.15f + 0.85f * clampf(1.f - crowdD / 90.f, 0.f, 1.f));
}

void Game::addVehicleDraws(RenderFrame& f, const Vehicle& g, const VehicleVisual& vis, Vec3 pos, Quat rot, const float* comp,
                           const float* spin, float steer, float dirt, float alpha) {
    DrawItem body;
    body.mesh = vis.body;
    body.model = Mat4::trs(pos, rot);
    body.dirt = dirt;
    body.dirtLo = g.body->bellyY - 0.1f;
    body.dirtHi = g.body->dims[1] * 0.75f;
    body.alpha = alpha;
    body.seed = float(uintptr_t(&g) % 97);
    for (int i = 0; i < 4; i++) {
        const WheelState& w = g.wheels[size_t(i)];
        Vec3 c = w.mount - Vec3{0, g.travel - comp[i], 0};
        float* o = body.wheels + i * 4;
        o[0] = c.x; o[1] = c.y; o[2] = c.z; o[3] = w.radius;
    }
    f.items.push_back(body);
    // Soft contact shadow on the ground, fading out as the truck gets air.
    if (alpha >= 0.99f) {
        Vec3 fw = rot.rotate({0, 0, 1});
        float rideH = g.wheels[0].radius - g.body->axleY;
        float air = pos.y - world.terrain.heightAt(pos.x, pos.z) - rideH;
        float fade = clampf(1.f - air / 1.5f, 0.f, 1.f);
        if (fade > 0.01f) f.occluders.insert(f.occluders.end(), {pos.x, pos.z, std::atan2(fw.x, fw.z), 0.82f * g.body->dims[2] * fade});
    }
    for (int i = 0; i < 4; i++) {
        const WheelState& w = g.wheels[size_t(i)];
        float sus = g.travel - comp[i];
        Vec3 c = w.mount - Vec3{0, sus, 0};
        Quat q = rot * Quat::axisAngle({0, 1, 0}, w.front ? steer : 0.f) * Quat::axisAngle({1, 0, 0}, spin[i]);
        DrawItem it;
        it.mesh = vis.wheel;
        it.model = Mat4::trs(pos + rot.rotate(c), q, {w.left ? 1.f : -1.f, 1, 1});
        it.mirror = !w.left;
        it.dirt = clampf(dirt * 1.6f + 0.15f, 0.f, 1.f);
        it.dirtLo = -0.6f;
        it.dirtHi = 0.55f;
        it.alpha = alpha;
        it.seed = float(i) * 3.1f;
        it.tireR = w.radius;
        f.items.push_back(it);
    }
}

void Game::updateLens(float dt) {
    for (auto& l : lens) {
        l.a -= dt * (l.a > 0 ? 0.12f : -0.6f);   // mud fades slowly, water drops evaporate fast
        if (l.a < 0) l.y -= dt * 0.03f;          // water runs down the lens
    }
    lens.erase(std::remove_if(lens.begin(), lens.end(), [](const LensSplat& l) { return std::fabs(l.a) < 0.02f; }), lens.end());
    if (state != AppState::Playing || !renderer.postEnabled) return;
    Rng& R = particles.rng;
    int added = 0;
    for (const Particle& p : particles.all()) {
        if (added >= 2) break;
        if (p.type != PType::MudChunk && p.type != PType::MudSpray && p.type != PType::Water) continue;
        Vec3 d = camPos - p.p;
        float dd = dot(d, d);
        if (dd > 2.6f * 2.6f || dot(p.v, d) <= 0) continue;
        Vec3 ndc = renderer.viewProj.transformPoint(p.p);
        if (std::fabs(ndc.x) > 1 || std::fabs(ndc.y) > 1 || ndc.z > 1) continue;
        if (R.uniform() > 0.35f) continue;
        bool water = p.type == PType::Water;
        LensSplat l{ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f, R.range(0.02f, 0.07f) * (water ? 0.6f : 1.f), water ? -0.9f : R.range(0.6f, 0.95f)};
        if (lens.size() >= 16) lens.erase(lens.begin());
        lens.push_back(l);
        added++;
    }
}

void Game::buildFrame(RenderFrame& f) {
    for (auto& l : lens) f.lens.insert(f.lens.end(), {l.x, l.y, l.r, l.a});
    f.camPos = camPos;
    f.camTarget = camTarget;
    f.fov = fov;
    f.time = time;
    f.particles = &particles.all();
    float comp[4], spin[4];
    for (int i = 0; i < 4; i++) { comp[i] = player.wheels[size_t(i)].compression; spin[i] = player.wheels[size_t(i)].spin; }
    if (camMode != 2 || state != AppState::Playing)
        addVehicleDraws(f, player, playerVis, player.pos, player.rot, comp, spin, player.steerApplied, player.dirt, 1.f);
    for (auto& kv : remotes) {
        Remote& r = kv.second;
        if (!r.valid) continue;
        float c2[4];
        for (int i = 0; i < 4; i++) c2[i] = r.cur.wheelComp[i] * r.geom.travel;
        addVehicleDraws(f, r.geom, r.vis, r.cur.pos, r.cur.rot, c2, r.spin, r.cur.steer * 0.5f, r.cur.dirt, 1.f);
    }
    for (size_t i = 0; i < ais.size(); i++) {
        const Vehicle& v = ais[i].v;
        for (int k = 0; k < 4; k++) { comp[k] = v.wheels[size_t(k)].compression; spin[k] = v.wheels[size_t(k)].spin; }
        addVehicleDraws(f, v, aiVis[i], v.pos, v.rot, comp, spin, v.steerApplied, v.dirt, 1.f);
    }
    // Recovery gear on the ground: traction boards, and the winch line to its anchor.
    for (const SupportPad& p : recovery.pads) {
        DrawItem it;
        it.mesh = renderer.mesh("props/tractionboard.mbcad");
        Quat q = Quat::axisAngle({0, 1, 0}, std::atan2(p.dir.x, p.dir.z)) * Quat::axisAngle({1, 0, 0}, -std::atan(p.slope));
        it.model = Mat4::trs({p.center.x, p.top - 0.02f, p.center.z}, q);
        it.dirt = clampf(0.3f + p.wear, 0.f, 1.f);
        it.dirtLo = 0; it.dirtHi = 0.06f;
        f.items.push_back(it);
    }
    if (recovery.winch.active) {
        Vec3 a = recovery.hookPoint(player), d = recovery.winch.anchor - a;
        float len = length(d);
        if (len > 0.1f) {
            DrawItem it;
            it.mesh = renderer.mesh("props/winchline.mbcad");
            Quat q = Quat::axisAngle({0, 1, 0}, std::atan2(d.x, d.z)) * Quat::axisAngle({1, 0, 0}, -std::asin(clampf(d.y / len, -1.f, 1.f)));
            it.model = Mat4::trs(a, q, {1, 1, len});
            f.items.push_back(it);
        }
    }
    // Translucent ghost of the personal best, sampled at the current race time.
    if (ghostTrace.size() >= 16 && mode.phase == Phase::Running) {
        size_t n = ghostTrace.size() / 8, lo = 0, hi = n - 1;
        float t = mode.elapsed;
        if (t <= ghostTrace[(n - 1) * 8]) {
            while (hi - lo > 1) { size_t mid = (lo + hi) / 2; (ghostTrace[mid * 8] <= t ? lo : hi) = mid; }
            const float *a = &ghostTrace[lo * 8], *b = &ghostTrace[hi * 8];
            float u = clampf((t - a[0]) / std::max(1e-4f, b[0] - a[0]), 0.f, 1.f);
            Vec3 gp = lerp(Vec3{a[1], a[2], a[3]}, Vec3{b[1], b[2], b[3]}, u);
            Quat gq = slerp(Quat{a[4], a[5], a[6], a[7]}, Quat{b[4], b[5], b[6], b[7]}, u);
            float gc[4], gs[4];
            for (int k = 0; k < 4; k++) { gc[k] = player.travel * 0.38f; gs[k] = t * 20.f; }
            addVehicleDraws(f, player, playerVis, gp, gq, gc, gs, 0.f, 0.f, 0.32f);
        }
    }
}

void Game::frame(double nowMs) {
    float dt = float(std::min(0.05, (nowMs - lastMs) / 1000.0));
    lastMs = nowMs;
    time += dt;
    InputState in = input.poll(dt);
    if (state == AppState::Playing) {
        if (in.pause) g_pauseRequested = true;
        if (in.locker) action("locker");
        if (in.lowRange) action("transfer");
        if (in.camera) action("camera");
        if (in.reset) action("reset");
        if (in.tow) action("recover");
        if (in.scout) action("scout");
        if (in.shovel) action("shovel");
        if (in.boards) action("boards");
        if (in.winch) action("winch");
        if (in.horn && hornTimer <= 0) action("horn");
        if (in.manualToggle) {
            player.manual = !player.manual;
            g_userManual = player.manual;
            mode.message = player.manual ? "Manual shifting (X/Y or Q/E)" : "Automatic";
            mode.messageTimer = 1.5f;
        }
    }
    hornTimer -= dt;

    bool running = mode.phase == Phase::Running;
    for (auto& a : ais) aiThink(a, mode.id, world, dt, running ? mode.elapsed : time, running);

    // Fixed-step physics.
    accum += dt;
    int steps = 0;
    ejected = 0;
    float splashMax = 0;
    VehicleEvents ev;
    while (accum >= Vehicle::DT && steps < 14) {
        InputState sub = in;
        say(recovery.stepWinch(player, Vehicle::DT), 3.f);
        if (recovery.digTimer > 0) { sub.throttle = 0; sub.brake = 1; }
        simulate(Vehicle::DT, sub);
        in.shiftUp = in.shiftDown = false;
        // accumulate events across substeps
        ev.impact = std::max(ev.impact, player.events.impact);
        if (player.events.impact >= ev.impact) ev.impactPos = player.events.impactPos;
        ev.bellyScrape = std::max(ev.bellyScrape, player.events.bellyScrape);
        if (player.events.splash > ev.splash) { ev.splash = player.events.splash; ev.splashPos = player.events.splashPos; }
        ev.limiterHits += player.events.limiterHits;
        ev.backfires += player.events.backfires;
        ev.shifts += player.events.shifts;
        ev.hydrolocked = ev.hydrolocked || player.events.hydrolocked;
        ev.beadPopped = ev.beadPopped || player.events.beadPopped;
        ev.shaftBroke = ev.shaftBroke || player.events.shaftBroke;
        ev.beltBurnt = ev.beltBurnt || player.events.beltBurnt;
        ev.rodBent = ev.rodBent || player.events.rodBent;
        ev.restarted = ev.restarted || player.events.restarted;
        player.events.clear();
        accum -= Vehicle::DT;
        steps++;
    }
    if (steps == 14) accum = 0;
    player.events = ev;
    splashMax = ev.splash;

    if (running && !aiReleased) {
        for (auto& a : ais) { a.v.manual = false; a.v.gear = 1; }
        aiReleased = true;
    }
    // Countdown over: drop it in gear.
    if (mode.phase == Phase::Running && mode.id != ModeId::FreeRoam && player.gear == 0 && player.manual && !g_userManual) {
        player.manual = false;
        player.gear = 1;
    }

    // Remote replicas
    std::vector<Vehicle*> vehicles{&player};
    for (auto& a : ais) vehicles.push_back(&a.v);
    double rt = serverNow() - kInterpDelay;
    for (auto& kv : remotes) {
        Remote& r = kv.second;
        r.valid = r.net.sample(rt, r.cur);
        if (!r.valid) continue;
        r.geom.pos = r.cur.pos;
        r.geom.rot = r.cur.rot;
        r.geom.vel = r.cur.vel;
        for (int i = 0; i < 4; i++) r.spin[i] += r.cur.wheelOmega[i] * dt;
        vehicles.push_back(&r.geom);
        // Remote trucks also churn our local copy of the mud (cosmetic, not authoritative).
        if (r.cur.mudSlap > 0.3f) world.terrain.scuff(r.cur.pos, 1.2f, 0.f, dt * 0.6f);
    }
    wev.clear();
    world.stepDynamics(dt, vehicles, wev);
    relaxTimer += dt;
    if (relaxTimer > 0.05f) { world.terrain.relax(player.pos, 7.f, relaxTimer); relaxTimer = 0; }
    ejected = spawnWheelFx(player, dt);
    for (size_t i = 0; i < ais.size(); i++) {
        AiDriver& a = ais[i];
        float e = spawnWheelFx(a.v, dt);
        if (mode.phase == Phase::Running) aiProgress(a, mode.id, world, dt, mode.elapsed, e);
        if (a.v.events.impact > 1500.f) audio.trigger(SfxType::Impact, a.v.events.impactPos, clampf(a.v.events.impact / 9000.f, 0.f, 1.2f));
        if (a.v.events.splash > 0.4f) audio.trigger(SfxType::Splash, a.v.events.splashPos, clampf(a.v.events.splash / 2.f, 0.2f, 1.2f));
        for (int k = 0; k < std::min(a.v.events.backfires, 2); k++) audio.backfire(100 + int(i));
        a.v.events.clear();
        if (relaxTimer == 0 && length(a.v.pos - player.pos) < 70.f) world.terrain.relax(a.v.pos, 6.f, 0.05f);
    }
    // Ghost: record this run, and replay the stored personal best.
    if (mode.phase == Phase::Running && (mode.id == ModeId::MudBog || mode.id == ModeId::Circuit)) {
        traceTimer -= dt;
        if (traceTimer <= 0) {
            traceTimer = 0.05f;
            const float smp[8] = {mode.elapsed, player.pos.x, player.pos.y, player.pos.z, player.rot.x, player.rot.y, player.rot.z, player.rot.w};
            if (runTrace.size() < 8 * 20 * 600) runTrace.insert(runTrace.end(), smp, smp + 8);
        }
    }
    particles.update(dt, world.terrain);

    // Events -> audio, rumble, camera, crowd
    if (ev.impact > 1200.f) {
        float s = clampf(ev.impact / 9000.f, 0.f, 1.5f);
        audio.trigger(SfxType::Impact, ev.impactPos, s);
        input.rumble(clampf(s, 0, 1), 0.3f, 180);
        camShake = std::max(camShake, s);
    }
    if (splashMax > 0.25f) {
        audio.trigger(SfxType::Splash, ev.splashPos, clampf(splashMax / 2.f, 0.2f, 1.5f));
        Rng& R = particles.rng;
        int n = int(clampf(splashMax * 12.f, 6.f, 50.f));
        for (int k = 0; k < n; k++) {
            float a = R.range(0, 2 * PI);
            Vec3 vel{std::cos(a) * R.range(1, 5), R.range(2, 6) * clampf(splashMax, 0.5f, 2.f), std::sin(a) * R.range(1, 5)};
            particles.emit(PType::Water, ev.splashPos + Vec3{0, 0.3f, 0}, vel + player.vel * 0.5f, R.range(0.05f, 0.1f), 1.6f, {0.5f, 0.46f, 0.38f});
        }
        for (int k = 0; k < 5; k++) particles.emit(PType::Mist, ev.splashPos + Vec3{0, 0.5f, 0}, Vec3{0, 1.2f, 0}, 0.6f, 2.f, {0.78f, 0.75f, 0.7f});
        if (length(ev.splashPos - world.crowdCenter) < 60.f && splashMax > 1.f) audio.trigger(SfxType::Cheer, ev.splashPos, 0.5f);
        input.rumble(0.4f, 0.6f, 220);
    }
    for (int k = 0; k < std::min(ev.backfires, 3); k++) {
        audio.backfire(localId);
        input.rumble(0.2f, 0.5f, 60);
    }
    if (ev.limiterHits) input.rumble(0.f, 0.25f, 40);
    if (ev.shifts) audio.trigger(SfxType::Shift, player.pos, 0.6f);
    if (ev.hydrolocked) { say("HYDROLOCKED! Engine dead. Get out of the water (winch/tow) - do NOT crank it", 4.f); input.rumble(1, 1, 500); }
    if (ev.rodBent) { say("BENT A ROD - you cranked a flooded engine. It's done: dozer tow", 5.f); input.rumble(1, 1, 700); }
    if (ev.restarted) say("Water cleared - she fired back up", 3.f);
    if (ev.beadPopped) { say("POPPED A BEAD - too little pressure without beadlocks", 4.f); input.rumble(0.6f, 0.6f, 250); }
    if (ev.shaftBroke) { say("SNAPPED THE FRONT U-JOINT - 4H windup on grippy ground. Rear drive only now", 4.5f); input.rumble(1, 0.8f, 400); camShake = std::max(camShake, 0.6f); }
    if (ev.beltBurnt) say("CVT BELT BURNT - use low range in the mud. Belt is slipping", 4.5f);
    say(recovery.update(world, player, dt), 3.f);
    updateCoach(dt);
    updateVenue(dt);
    for (size_t k = 0; k < wev.barrelHits.size(); k++) audio.trigger(SfxType::Barrel, wev.barrelHits[k], wev.barrelStrength[k]);
    for (auto& p : wev.fenceHits) audio.trigger(SfxType::Fence, p, 0.8f);
    if (wev.barrelsKnocked && length(player.pos - world.crowdCenter) < 70.f) audio.trigger(SfxType::Cheer, player.pos, 0.25f);
    // Slurps: tires pulling out of deep sticky slop.
    slurpTimer -= dt;
    for (auto& w : player.wheels)
        if (slurpTimer <= 0 && w.contact && w.rutWall > 0.18f && w.mudW > 0.5f && std::fabs(w.omega) > 1.f && player.speed() < 4.f) {
            audio.trigger(SfxType::Slurp, w.contactPos, 0.7f);
            slurpTimer = particles.rng.range(0.25f, 0.9f);
            break;
        }
    static float lastAir = 0;
    if (lastAir > 0.7f && player.airTime == 0 && length(player.pos - world.crowdCenter) < 80.f) audio.trigger(SfxType::Cheer, player.pos, 0.6f);
    lastAir = player.airTime;

    splash = splashMax;
    barrelsKnocked = wev.barrelsKnocked;
    mode.update(dt, serverNow(), world, player, ejected, splash, barrelsKnocked);
    if (mode.phase == Phase::Finished && g_lastPhase != Phase::Finished) {
        g_finishSerial++;
        if (mode.id == ModeId::MudBog && mode.distance > 79.f) audio.trigger(SfxType::Cheer, player.pos, 1.f);
        if (mode.id == ModeId::Bounty && mode.claimed) audio.trigger(SfxType::Cheer, player.pos, 1.f);
    }
    g_lastPhase = mode.phase;

    updateCamera(dt, in);
    updateLens(dt);
    feedAudio(dt);
    RenderFrame f;
    buildFrame(f);
    renderer.render(world, f);
}

// Field-guide coaching: the single most useful thing to know right now.
void Game::updateCoach(float dt) {
    coach.clear();
    if (state != AppState::Playing) return;
    const Vehicle& v = player;
    float spd = v.speed();
    bool spinning = v.avgSlip > 0.6f && spd < 0.8f && v.throttleApplied > 0.4f;
    diggingTimer = spinning ? diggingTimer + dt : std::max(0.f, diggingTimer - dt * 2.f);
    if (diggingTimer > 1.5f && spd > 2.f) tractionBackTimer = 2.5f;
    tractionBackTimer -= dt;
    float pack = 0;
    bool debead = false;
    for (auto& w : v.wheels) { pack += w.pack * 0.25f; debead = debead || w.debeaded; }
    if (v.engineBroken) coach = "Engine is broken - call the dozer";
    else if (v.flooded) coach = v.floodDry > 0 ? "Let the water drain... then throttle to restart" : "Flooded: off the throttle! Get out of the water first";
    else if (recovery.digTimer > 0) coach = "Shovelling...";
    else if (recovery.winch.active) coach = "Winching: light throttle to help, never jerk the line";
    else if (v.isCvt() && !v.lowRange && v.beltTemp > 120.f) coach = "CVT belt is cooking - shift to LOW range";
    else if (v.windup > 0.45f) coach = "Driveline binding - straighten up or go 2H on grippy ground";
    else if (debead) coach = "Tire bead is off - limp it out, air up / beadlocks";
    else if (diggingTimer > 1.5f) coach = v.bellyLoad > 0.25f ? "Bellied out: stop spinning - shovel (K), boards (B) or winch (J)" :
                                                              "You're digging - ease off, rock it back and forth, lock up";
    else if (v.bellyLoad > 0.4f && spd < 0.5f) coach = "Hung on the belly - dig it out or winch";
    else if (pack > 0.65f && spd < 6.f) coach = "Tread packed with mud - a burst of wheel speed cleans the lugs";
    else if (tractionBackTimer > 0) coach = "Traction back - keep the momentum";
    else if (v.brakeWet > 0.35f) coach = "Wet brakes - drag them lightly to dry them";
}

// Venue life: the PA announcer, rival milestones, and the wash bay on the return road.
void Game::updateVenue(float dt) {
    if (mode.id == ModeId::FreeRoam && state == AppState::Playing) {
        int c = challenges.update(world, player, dt);
        if (c >= 0) {
            const auto& g = challenges.goals[size_t(c)];
            say(std::string("CHALLENGE COMPLETE: ") + g.name + " +$" + std::to_string(g.pay), 4.f);
            announceLine(player.cfg.name + " just did the " + g.name + "! That's $" + std::to_string(g.pay));
            audio.trigger(SfxType::Cheer, player.pos, 0.8f);
        }
    }
    for (size_t i = 0; i < ais.size() && i < aiState.size(); i++) {
        const AiDriver& a = ais[i];
        int st = a.finished ? 1 : (a.gaveUp ? 2 : 0);
        if (st == aiState[i]) continue;
        aiState[i] = st;
        if (mode.id == ModeId::MudBog) {
            if (st == 1) announceLine("'" + a.rig + "' makes a FULL PASS! Listen to that crowd!");
            else announceLine("'" + a.rig + "' is done at " + std::to_string(int(a.distance * 3.281f)) + " ft - bring the dozer");
        } else if (st == 1) announceLine(a.name + " takes the checkered flag in '" + a.rig + "'");
    }
    announceT -= dt;
    if (announceT <= 0) {
        if (!announceQ.empty()) { announce = announceQ.front(); announceQ.pop_front(); announceT = 3.6f; }
        else { announce.clear(); announceT = 0.5f; }
    }
    // Wash bay: drive onto the pad and stop; the crew pressure-washes the truck.
    Vec3 d = player.pos - world.washCenter;
    bool onPad = state == AppState::Playing && std::fabs(d.x) < world.washHalfX && std::fabs(d.z) < world.washHalfZ + 1.f && player.speed() < 1.5f;
    float muck = player.dirt + player.radiatorClog;
    for (auto& w : player.wheels) muck += w.cake + w.pack;
    if (onPad && muck > 0.02f) {
        if (washT <= 0) say("Wash bay: hold still - pressure washing", 2.5f);
        washT += dt;
        player.dirt = std::max(0.f, player.dirt - dt * 0.3f);
        player.radiatorClog = std::max(0.f, player.radiatorClog - dt * 0.3f);
        player.brakeWet = 1.f;   // they'll be soaked after this
        for (auto& w : player.wheels) { w.cake = std::max(0.f, w.cake - dt * 0.4f); w.pack = std::max(0.f, w.pack - dt * 0.5f); }
        if (player.dirt <= 0 && player.radiatorClog <= 0) say("Clean! Wheels balanced, radiator flushed. Mind the wet brakes", 3.f);
    } else if (!onPad) washT = 0;
}

std::string Game::standingsJson() {
    struct Row { std::string name, text; float key; bool you; };
    std::vector<Row> rows;
    auto ft = [](float m) { return std::to_string(int(m * 3.281f)) + " ft"; };
    auto tm = [](float t) { char b[24]; int mi = int(t / 60); std::snprintf(b, sizeof b, "%d:%05.2f", mi, t - mi * 60); return std::string(b); };
    const int n = int(world.circuit.size());
    auto add = [&](const std::string& nm, bool you, float dist, bool fin, float ftime, float lapProg, float score) {
        Row r{nm, "", 0, you};
        switch (mode.id) {
            case ModeId::MudBog: r.key = fin ? 1000.f - ftime : dist; r.text = fin ? "PASS " + tm(ftime) : ft(dist); break;
            case ModeId::Circuit: r.key = fin ? 100.f - ftime / 1000.f : lapProg; r.text = fin ? tm(ftime) : "Lap " + std::to_string(std::min(3, int(lapProg) + 1)); break;
            case ModeId::Freestyle: r.key = score; r.text = std::to_string(int(score)) + " pts"; break;
            case ModeId::King: r.key = score; r.text = tm(score); break;
            default: r.key = 0; r.text = ""; break;
        }
        rows.push_back(r);
    };
    bool pfin = mode.phase == Phase::Finished;
    float pLap = float(mode.lap) + float((mode.nextCp - 1 + n) % n) / float(n);
    add(player.cfg.name, true, mode.distance, pfin && (mode.id != ModeId::MudBog || mode.distance > 79.9f),
        mode.elapsed + mode.penalty, pLap, mode.id == ModeId::King ? mode.kingTime : mode.score);
    for (auto& a : ais) add(a.name + " · " + a.rig, false, a.distance, a.finished, a.finishTime, a.score, a.id_score());
    for (auto& kv : remotes)
        if (kv.second.valid) add(kv.second.net.name, false, kv.second.cur.distance, false, 0, kv.second.cur.score, kv.second.cur.score);
    std::stable_sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return x.key > y.key; });
    std::string out = "[";
    for (size_t i = 0; i < rows.size(); i++) {
        std::string nm;
        for (char c : rows[i].name) if (c != '"' && c != '\\' && c >= 32) nm += c;
        out += (i ? "," : "") + std::string("{\"name\":\"") + nm + "\",\"text\":\"" + rows[i].text + "\",\"you\":" + (rows[i].you ? "true" : "false") + "}";
    }
    return out + "]";
}

const char* Game::hud() {
    char buf[12288];
    const EngineSpec& e = *player.engine;
    double now = serverNow();
    std::string res = "[";
    for (int i = 0; i < mode.resultCount; i++) {
        char r[256];
        std::snprintf(r, sizeof r, "%s{\"board\":\"%s\",\"value\":%.3f,\"lower\":%s,\"summary\":\"%s\"}", i ? "," : "",
                      mode.results[i].board.c_str(), mode.results[i].value, mode.results[i].lowerIsBetter ? "true" : "false",
                      mode.results[i].summary.c_str());
        res += r;
    }
    res += "]";
    std::string others = "[";
    bool first = true;
    for (auto& kv : remotes) {
        if (!kv.second.valid) continue;
        char r[160];
        std::snprintf(r, sizeof r, "%s{\"id\":%d,\"x\":%.1f,\"z\":%.1f,\"score\":%.1f,\"dist\":%.1f}", first ? "" : ",", kv.first,
                      kv.second.cur.pos.x, kv.second.cur.pos.z, kv.second.cur.score, kv.second.cur.distance);
        others += r;
        first = false;
    }
    for (size_t i = 0; i < ais.size(); i++) {
        char r[160];
        std::snprintf(r, sizeof r, "%s{\"id\":%d,\"x\":%.1f,\"z\":%.1f,\"ai\":true}", first ? "" : ",", 100 + int(i), ais[i].v.pos.x, ais[i].v.pos.z);
        others += r;
        first = false;
    }
    others += "]";
    std::string standings = standingsJson();
    Vec3 f = player.forward();
    int soilCount[int(Soil::Count)] = {0};
    bool debead = false;
    for (auto& w : player.wheels) { if (w.contact) soilCount[w.soil]++; debead = debead || w.debeaded; }
    int soil = 0;
    for (int k = 1; k < int(Soil::Count); k++) if (soilCount[k] > soilCount[soil]) soil = k;
    const char* soilName = soilMods(uint8_t(soil)).name;
    std::string chal = "[";
    for (size_t i = 0; i < challenges.goals.size(); i++) {
        const auto& g = challenges.goals[i];
        chal += std::string(i ? "," : "") + "{\"name\":\"" + g.name + "\",\"hint\":\"" + g.hint + "\",\"pay\":" + std::to_string(g.pay) +
                ",\"done\":" + (g.done ? "true" : "false") + "}";
    }
    chal += "]";
    std::string announceSafe;
    for (char c : announce) if (c != '"' && c != '\\' && c >= 32) announceSafe += c;
    int winchState = !player.cfg.winch ? 0 : (recovery.winch.broken ? 3 : (recovery.winch.active ? 2 : 1));
    std::snprintf(buf, sizeof buf,
                  "{\"state\":%d,\"mph\":%.1f,\"rpm\":%.0f,\"redline\":%.0f,\"gear\":%d,\"manual\":%s,\"low\":%s,\"awd\":%s,"
                  "\"lockF\":%s,\"lockR\":%s,\"psi\":%.1f,\"temp\":%.1f,\"clog\":%.2f,\"boost\":%.2f,\"stalled\":%s,"
                  "\"mode\":%d,\"modeName\":\"%s\",\"phase\":%d,\"countdown\":%.2f,\"elapsed\":%.2f,\"limit\":%.0f,"
                  "\"distance\":%.2f,\"score\":%.1f,\"combo\":%.2f,\"lap\":%d,\"laps\":%d,\"bestLap\":%.2f,\"lastLap\":%.2f,"
                  "\"penalty\":%.1f,\"king\":%.2f,\"stuck\":%s,\"belly\":%.2f,\"slip\":%.2f,\"air\":%.2f,\"message\":\"%s\","
                  "\"finishSerial\":%d,\"results\":%s,\"x\":%.1f,\"z\":%.1f,\"heading\":%.3f,\"others\":%s,"
                  "\"pause\":%s,\"pad\":%s,\"padName\":\"%s\",\"engine\":\"%s\",\"dirt\":%.2f,\"lane\":%d,\"standings\":%s,\"ghost\":%s,"
                  "\"coach\":\"%s\",\"soil\":\"%s\",\"tcase\":%d,\"diffF\":\"%s\",\"diffR\":\"%s\",\"belt\":%.0f,\"cvt\":%s,"
                  "\"windup\":%.2f,\"winch\":%d,\"winchKn\":%.1f,\"winchHeat\":%.2f,\"boards\":%d,\"dig\":%.1f,"
                  "\"flooded\":%s,\"broken\":%s,\"recover\":%s,\"brakeWet\":%.2f,\"bead\":%s,\"shaft\":%s,\"announce\":\"%s\",\"pot\":%d,\"payout\":%d,\"beltBurnt\":%s,\"rewardSerial\":%d,\"reward\":%d,\"challenges\":%s}",
                  int(state), player.speed() * 2.237f, player.engineRpm, e.redlineRpm, player.gear, player.manual ? "true" : "false",
                  player.lowRange ? "true" : "false", player.fourWD ? "true" : "false", player.lockFront ? "true" : "false",
                  player.lockRear ? "true" : "false", player.psi, player.engineTemp, player.radiatorClog, player.boost,
                  player.stallTimer > 0 || player.flooded || player.engineBroken ? "true" : "false", int(mode.id), modeName(mode.id), int(mode.phase),
                  mode.countdownLeft(now), mode.elapsed, mode.timeLimit, mode.distance, mode.score, mode.combo, mode.lap, mode.laps,
                  mode.bestLap, mode.lastLap, mode.penalty, mode.kingTime, mode.stuck ? "true" : "false", player.bellyLoad,
                  player.avgSlip, player.airTime, mode.messageTimer > 0 ? mode.message.c_str() : "", g_finishSerial, res.c_str(),
                  player.pos.x, player.pos.z, std::atan2(f.x, f.z), others.c_str(), g_pauseRequested ? "true" : "false",
                  input.gamepadConnected ? "true" : "false", input.gamepadName, e.name.c_str(), player.dirt, mode.lane,
                  standings.c_str(), ghostTrace.size() >= 16 ? "true" : "false", coach.c_str(), soilName, player.transfer(),
                  diffName(player.diffF), diffName(player.diffR), player.beltTemp, player.isCvt() ? "true" : "false", player.windup,
                  winchState, recovery.winch.force / 1000.f, recovery.winch.heat, recovery.boardsCarried, std::max(0.f, recovery.digTimer),
                  player.flooded ? "true" : "false", player.engineBroken ? "true" : "false", recoverMenu ? "true" : "false",
                  player.brakeWet, debead ? "true" : "false", player.frontShaftBroken ? "true" : "false", announceSafe.c_str(), mode.bountyPot, mode.payout, player.beltBurnt ? "true" : "false", challenges.rewardSerial, challenges.lastReward, chal.c_str());
    hudJson = buf;
    g_pauseRequested = false;
    return hudJson.c_str();
}

int Game::netSnapshot(uint8_t* out) {
    NetSnapshot s;
    s.id = uint16_t(localId);
    s.seq = ++netSeq;
    s.time = serverNow();
    s.pos = player.pos;
    s.vel = player.vel;
    s.rot = player.rot;
    s.rpm = player.engineRpm;
    s.throttle = player.throttleApplied;
    s.steer = player.steerApplied / 0.62f;
    s.boost = player.boost;
    s.dirt = player.dirt;
    s.gear = int8_t(player.gear);
    s.flags = uint8_t((player.fuelCut ? 1 : 0) | (player.stallTimer <= 0 ? 2 : 0) | (player.lockRear ? 4 : 0) | (hornTimer > 0 ? 8 : 0));
    for (int i = 0; i < 4; i++) {
        s.wheelOmega[i] = player.wheels[size_t(i)].omega;
        s.wheelComp[i] = player.wheels[size_t(i)].compression / std::max(0.01f, player.travel);
    }
    s.mudSlap = g_playerSound.mudSlap;
    s.waterSpeed = g_playerSound.waterSpeed;
    s.lugRate = g_playerSound.lugRate;
    s.score = mode.score;
    s.distance = mode.distance;
    return int(s.write(out));
}

void Game::netReceive(const uint8_t* data, int len) {
    NetSnapshot s;
    if (!s.read(data, size_t(len)) || s.id == localId) return;
    auto it = remotes.find(s.id);
    if (it == remotes.end()) return;
    it->second.net.push(s);
}

void Game::addRemote(int id, const std::string& cfg, const std::string& name) {
    if (id == localId) return;
    Remote& r = remotes[id];
    r.cfg = VehicleConfig::parse(cfg);
    r.geom.configure(r.cfg);
    r.vis.build(renderer, r.cfg);
    r.net.config = cfg;
    r.net.name = name;
}

void Game::removeRemote(int id) {
    remotes.erase(id);
    audio.removeVehicle(id);
}

}  // namespace mb
