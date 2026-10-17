#include "game.h"
#include <emscripten.h>
#include <cstdio>

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
                                          {"rim_r", c.tireIn >= 38 ? 0.2159f : 0.1905f},
                                          {"lugs", float(lugCount(c.tread))},
                                          {"depth", c.tread == Tread::AllTerrain ? 0.018f : 0.028f},
                                          {"tread", float(int(c.tread))},
                                          {"beadlock", c.tireIn >= 38 ? 1.f : 0.f}});
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
    lockerState = c.lockFront ? 2 : (c.lockRear ? 1 : 0);
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
            }
        }
    }
}

void Game::startMode(ModeId m, int lane, double startAt) {
    if (startAt <= 0) startAt = serverNow() + 3.5;
    world.resetDynamics();
    particles.clear();
    g_userManual = player.manual;
    mode.start(m, world, player, lane, startAt);
    if (m != ModeId::FreeRoam) player.manual = true;  // hold neutral during the countdown
    player.dirt *= 0.5f;
    camYaw = camPitch = 0;
    setState(AppState::Playing);
}

void Game::action(const std::string& a) {
    if (a == "tow") mode.requestTow(world, player);
    else if (a == "reset") {
        if (mode.id == ModeId::MudBog && mode.phase == Phase::Running) mode.requestTow(world, player);
        else {
            Vec3 f = player.forward();
            float yaw = std::atan2(f.x, f.z);
            Vec3 p = player.pos;
            p.y = world.terrain.heightAt(p.x, p.z) - player.rideAxleY + player.wheels[0].radius + 1.0f;
            float d = player.dirt;
            player.reset(p, yaw);
            player.dirt = d;
        }
    } else if (a == "camera") camMode = (camMode + 1) % 3;
    else if (a == "horn") { audio.trigger(SfxType::Horn, player.pos, 1.f); hornTimer = 0.7f; }
    else if (a == "restart") startMode(mode.id, mode.lane, 0);
    else if (a.rfind("warp:", 0) == 0) {  // debug/photo-mode: warp:x,z,yawDegrees
        float x = 0, z = 0, yaw = 90;
        std::sscanf(a.c_str() + 5, "%f,%f,%f", &x, &z, &yaw);
        Vec3 p{x, world.terrain.heightAt(x, z) - player.rideAxleY + player.wheels[0].radius + 0.2f, z};
        player.reset(p, yaw * DEG);
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
}

void Game::spawnWheelFx(const Vehicle& vc, float dt) {
    Vehicle& v = const_cast<Vehicle&>(vc);
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
            ejected += vol;
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
    if (state == AppState::Garage) {
        camYaw += in.lookX + dt * 0.25f;
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

void Game::feedAudio(float dt) {
    Vec3 cf = camTarget - camPos;
    audio.setListener(camPos, cf, player.vel);
    VehicleSound s;
    s.engine = player.engine;
    s.pos = player.pos;
    s.vel = player.vel;
    s.rpm = player.engineRpm;
    s.throttle = player.throttleApplied;
    s.load = clampf(player.throttleApplied * (0.6f + 0.4f * (1.f - player.clutchSlip)), 0.f, 1.f);
    s.boost = player.boost;
    s.fuelCut = player.fuelCut;
    s.running = player.stallTimer <= 0;
    s.local = true;
    int lugs = lugCount(player.cfg.tread);
    float slap = 0, lug = 0, roll = 0, squeal = 0, firm = 0, water = 0, nMud = 0;
    for (auto& w : player.wheels) {
        if (!w.contact) continue;
        float ws = std::fabs(w.omega);
        if (w.mudW > 0.05f) {
            slap += w.mudW * clampf(std::fabs(w.slipSpeed) / 5.f + ws * w.radius / 20.f, 0.f, 1.f);
            lug += ws * lugs / (2.f * PI);
            roll += w.mudW * clampf(std::fabs(dot(player.vel, w.fwdDir)) / 4.f, 0.f, 1.f);
            nMud += 1;
        }
        firm += (1.f - w.mudW);
        squeal += (1.f - w.mudW) * clampf((std::fabs(w.slipAngle) - 0.12f) * 2.f + (std::fabs(w.slipRatio) - 0.25f), 0.f, 1.f);
        if (w.waterDepth > 0.03f) water = std::max(water, player.speed() + ws * w.radius * 0.3f);
    }
    s.mudSlap = clampf(slap / 2.f, 0.f, 1.f);
    s.lugRate = nMud > 0 ? lug / nMud : 0;
    s.mudRoll = clampf(roll / 3.f, 0.f, 1.f);
    s.waterSpeed = std::max(water, player.submerged > 0 ? player.speed() : 0.f);
    s.firmSpeed = player.speed() * firm / 4.f;
    s.squeal = clampf(squeal / 2.f, 0.f, 1.f);
    s.scrape = player.events.bellyScrape;
    s.debris = clampf(ejected / std::max(dt, 1e-3f) * 4000.f, 0.f, 80.f);
    s.tireLugHz = player.speed() / (2.f * PI * player.wheels[0].radius) * lugs;
    audio.setVehicle(localId, s);
    g_playerSound = s;
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
    f.items.push_back(body);
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
        f.items.push_back(it);
    }
}

void Game::buildFrame(RenderFrame& f) {
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
}

void Game::frame(double nowMs) {
    float dt = float(std::min(0.05, (nowMs - lastMs) / 1000.0));
    lastMs = nowMs;
    time += dt;
    InputState in = input.poll(dt);
    if (state == AppState::Playing) {
        if (in.pause) g_pauseRequested = true;
        if (in.locker) {
            lockerState = (lockerState + 1) % 3;
            player.lockRear = lockerState >= 1;
            player.lockFront = lockerState == 2;
            mode.message = lockerState == 0 ? "Lockers OFF" : (lockerState == 1 ? "Rear locker ON" : "Front + rear LOCKED");
            mode.messageTimer = 1.5f;
        }
        if (in.lowRange && player.speed() < 4.f) {
            player.lowRange = !player.lowRange;
            mode.message = player.lowRange ? "4-LO engaged" : "4-HI";
            mode.messageTimer = 1.5f;
        }
        if (in.camera) action("camera");
        if (in.reset) action("reset");
        if (in.tow) action("tow");
        if (in.horn && hornTimer <= 0) action("horn");
        if (in.manualToggle) {
            player.manual = !player.manual;
            g_userManual = player.manual;
            mode.message = player.manual ? "Manual shifting (X/Y or Q/E)" : "Automatic";
            mode.messageTimer = 1.5f;
        }
    }
    hornTimer -= dt;

    // Fixed-step physics.
    accum += dt;
    int steps = 0;
    ejected = 0;
    float splashMax = 0;
    VehicleEvents ev;
    while (accum >= Vehicle::DT && steps < 14) {
        InputState sub = in;
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
        player.events.clear();
        accum -= Vehicle::DT;
        steps++;
    }
    if (steps == 14) accum = 0;
    player.events = ev;
    splashMax = ev.splash;

    // Countdown over: drop it in gear.
    if (mode.phase == Phase::Running && mode.id != ModeId::FreeRoam && player.gear == 0 && player.manual && !g_userManual) {
        player.manual = false;
        player.gear = 1;
    }

    // Remote replicas
    std::vector<Vehicle*> vehicles{&player};
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
    spawnWheelFx(player, dt);
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
    if (ev.hydrolocked) { mode.message = "HYDROLOCKED! Water in the intake - restarting"; mode.messageTimer = 3.5f; input.rumble(1, 1, 500); }
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
    }
    g_lastPhase = mode.phase;

    updateCamera(dt, in);
    feedAudio(dt);
    RenderFrame f;
    buildFrame(f);
    renderer.render(world, f);
}

const char* Game::hud() {
    char buf[4096];
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
    others += "]";
    Vec3 f = player.forward();
    std::snprintf(buf, sizeof buf,
                  "{\"state\":%d,\"mph\":%.1f,\"rpm\":%.0f,\"redline\":%.0f,\"gear\":%d,\"manual\":%s,\"low\":%s,\"awd\":%s,"
                  "\"lockF\":%s,\"lockR\":%s,\"psi\":%.1f,\"temp\":%.1f,\"clog\":%.2f,\"boost\":%.2f,\"stalled\":%s,"
                  "\"mode\":%d,\"modeName\":\"%s\",\"phase\":%d,\"countdown\":%.2f,\"elapsed\":%.2f,\"limit\":%.0f,"
                  "\"distance\":%.2f,\"score\":%.1f,\"combo\":%.2f,\"lap\":%d,\"laps\":%d,\"bestLap\":%.2f,\"lastLap\":%.2f,"
                  "\"penalty\":%.1f,\"king\":%.2f,\"stuck\":%s,\"belly\":%.2f,\"slip\":%.2f,\"air\":%.2f,\"message\":\"%s\","
                  "\"finishSerial\":%d,\"results\":%s,\"x\":%.1f,\"z\":%.1f,\"heading\":%.3f,\"others\":%s,"
                  "\"pause\":%s,\"pad\":%s,\"padName\":\"%s\",\"engine\":\"%s\",\"dirt\":%.2f,\"lane\":%d}",
                  int(state), player.speed() * 2.237f, player.engineRpm, e.redlineRpm, player.gear, player.manual ? "true" : "false",
                  player.lowRange ? "true" : "false", player.fourWD ? "true" : "false", player.lockFront ? "true" : "false",
                  player.lockRear ? "true" : "false", player.psi, player.engineTemp, player.radiatorClog, player.boost,
                  player.stallTimer > 0 ? "true" : "false", int(mode.id), modeName(mode.id), int(mode.phase),
                  mode.countdownLeft(now), mode.elapsed, mode.timeLimit, mode.distance, mode.score, mode.combo, mode.lap, mode.laps,
                  mode.bestLap, mode.lastLap, mode.penalty, mode.kingTime, mode.stuck ? "true" : "false", player.bellyLoad,
                  player.avgSlip, player.airTime, mode.messageTimer > 0 ? mode.message.c_str() : "", g_finishSerial, res.c_str(),
                  player.pos.x, player.pos.z, std::atan2(f.x, f.z), others.c_str(), g_pauseRequested ? "true" : "false",
                  input.gamepadConnected ? "true" : "false", input.gamepadName, e.name.c_str(), player.dirt, mode.lane);
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
