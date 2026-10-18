// Native test suite: formulas, CAD kernel, netcode, world generation, vehicle physics
// behaviors (getting stuck, build differentiation), mode scoring and audio sanity.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>
#include "audio/audio.h"
#include "cad/cad.h"
#include "game/modes.h"
#include "game/world.h"
#include "net/net.h"
#include "physics/terramechanics.h"
#include "physics/vehicle.h"
#include "physics/collide.h"
#include "game/ai.h"

using namespace mb;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) g_pass++;                                           \
        else { g_fail++; std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } \
    } while (0)

static void test(const char* name, const std::function<void()>& f) {
    int before = g_fail;
    f();
    std::printf("%s %s\n", g_fail == before ? "[ ok ]" : "[FAIL]", name);
}

// Flat test field with a 60 m slurry pit (x 0..60).
static void makePit(Terrain& t, float moist, float water) {
    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            int k = t.idx(i, j);
            Vec3 w = t.cellToWorld(i, j);
            t.h[k] = 0; t.base[k] = -0.05f; t.moist[k] = 0.2f; t.grass[k] = 200; t.comp[k] = 0.05f; t.water[k] = Terrain::NO_WATER;
            if (w.x > 0 && w.x < 60 && std::fabs(w.z) < 10) {
                t.h[k] = -0.1f; t.base[k] = -0.9f; t.moist[k] = moist; t.comp[k] = 0.01f; t.grass[k] = 0;
                if (water > 0) t.water[k] = -0.1f + water;
            }
        }
}

static float runPit(VehicleConfig c, float moist, float water, float startX, float secs) {
    Terrain t;
    makePit(t, moist, water);
    Vehicle v;
    v.configure(c);
    v.reset({startX, t.heightAt(startX, 0) - v.rideAxleY + v.wheels[0].radius + 0.02f, 0}, PI / 2);
    VehicleInput in;
    for (int i = 0; i < 240; i++) v.step(t, in, Vehicle::DT);
    in.throttle = 1;
    float maxX = v.pos.x;
    for (int i = 0; i < int(secs * 240); i++) {
        v.step(t, in, Vehicle::DT);
        if (i % 4 == 0) t.relax(v.pos, 5, 4 * Vehicle::DT);
        maxX = std::max(maxX, v.pos.x);
        if (!std::isfinite(v.pos.x)) return -1e9f;
    }
    return maxX;
}

// Drive a lane of the real mud bog pit at full throttle, holding the lane. Returns the best
// distance into the pit (m). rock = back off and re-hit the throttle when stuck, like a player.
static float runBog(const VehicleConfig& c, int lane, float secs, bool rock) {
    static World w;
    w.generate(1976);
    ModeRunner m;
    m.id = ModeId::MudBog;
    Vehicle v;
    v.configure(c);
    float yaw;
    Vec3 p = m.spawnFor(w, lane, yaw);
    p.y = w.terrain.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.2f;
    v.reset(p, yaw);
    float laneZ = w.laneZ(lane), best = 0, stuckT = 0, rockT = 0;
    VehicleInput in;
    for (int i = 0; i < int(secs * 240); i++) {
        if (i % 4 == 0) {
            Vec3 f = v.forward();
            float ang = std::atan2(f.z, f.x);
            in.steer = clampf(-(laneZ - v.pos.z) * 0.35f + ang * 1.6f, -1.f, 1.f);
            in.throttle = 1; in.brake = 0;
            if (rock) {
                stuckT = v.speed() < 0.6f ? stuckT + 4 * Vehicle::DT : 0.f;
                if (stuckT > 2.f) rockT += 4 * Vehicle::DT;
                if (rockT > 0 && std::fmod(rockT, 2.4f) < 0.8f) { in.throttle = 0; in.brake = 1; }
                if (v.speed() > 2.f) rockT = 0;
            }
            w.terrain.relax(v.pos, 5, 4 * Vehicle::DT);
        }
        v.step(w.terrain, in, Vehicle::DT);
        best = std::max(best, v.pos.x - w.pitX0);
        if (v.pos.x >= w.pitX1 || !std::isfinite(v.pos.x)) break;
    }
    return best;
}

int main(int argc, char** argv) {
    cadSetRoot("cad/parts");
    if (argc > 2 && std::string(argv[1]) == "--bog") {   // tuning probe: mb_tests --bog "<cfg>" [secs] [lane]
        float secs = argc > 3 ? float(std::atof(argv[3])) : 120.f;
        int lane = argc > 4 ? std::atoi(argv[4]) : 1;
        VehicleConfig c = VehicleConfig::parse(argv[2]);
        std::printf("%s: throttle %.1f m, rocking %.1f m (of 80, %g s)\n", argv[2], runBog(c, lane, secs, false), runBog(c, lane, secs, true), secs);
        return 0;
    }

    test("terramechanics formulas", [] {
        float k = MudModel::kEq(0.6f), n = MudModel::n(0.6f);
        float z = bekkerSinkage(56e3f, k, n);
        CHECK(std::fabs(bekkerPressure(k, n, z) - 56e3f) < 10.f, "Bekker inverse mismatch");
        CHECK(z > 0.05f && z < 0.2f, "moisture 0.6 sinkage %.3f out of calibrated band", z);
        CHECK(bekkerSinkage(56e3f, MudModel::kEq(1.f), MudModel::n(1.f)) > 0.4f, "soup should swallow a tire");
        float f1 = janosiHanamoto(0.1f, 4000, 3000, 0.2f, 0.1f, 0.3f, 0.02f);
        float f2 = janosiHanamoto(0.1f, 4000, 3000, 0.2f, 0.8f, 0.3f, 0.02f);
        float fmax = 0.1f * 3000 + 4000 * std::tan(0.2f);
        CHECK(f2 > f1 && f2 < fmax, "Janosi-Hanamoto must rise with slip toward (Ac+W tan phi)");
        CHECK(std::fabs(magicFormula(0.f, 10, 1.65f, 1, 0.97f)) < 1e-6f, "MF(0) != 0");
        CHECK(std::fabs(minnaertFrequency(0.001f) - 3290.f) < 120.f, "Minnaert 1mm bubble ~3.3 kHz, got %.0f", minnaertFrequency(0.001f));
        CHECK(rankineBulldozing(0.3f, 0.2f, 17000, 2000, 0.1f) > 0, "bulldozing");
    });

    test("cad kernel: every part builds", [] {
        const char* parts[] = {"wheels/wheel.mbcad", "vehicles/comanche.mbcad", "vehicles/bogcar76.mbcad", "vehicles/silverado.mbcad",
                               "vehicles/utv.mbcad", "props/barrel.mbcad", "props/tpost.mbcad", "props/schoolbus.mbcad",
                               "props/dozer450.mbcad", "props/spectator.mbcad", "vegetation/poplar.mbcad", "vegetation/teasel.mbcad"};
        for (auto p : parts) {
            CadResult r = cadBuild(p);
            CHECK(r.error.empty() && r.mesh.idx.size() > 12, "%s: %s", p, r.error.c_str());
            CHECK(r.scad.find("rotate([90,0,0])") != std::string::npos, "%s: SCAD export missing", p);
        }
        std::string err;
        CHECK(std::fabs(cadEval("2*(3+4)-max(1,5)/5", {}, &err) - 13.f) < 1e-5f && err.empty(), "expr eval: %s", err.c_str());
        CadResult bad = cadBuildSource("box size=1,1\n", {}, "bad");
        CHECK(!bad.error.empty(), "malformed size should error");
        CadResult w35 = cadBuild("wheels/wheel.mbcad", {{"tire_r", 0.4445f}});
        Vec3 mx = w35.mesh.boundsMax();
        CHECK(std::fabs(mx.y - 0.4445f) < 0.04f, "35in tire radius %.3f", mx.y);
    });

    test("netcode: snapshot round-trip + interpolation", [] {
        NetSnapshot a;
        a.id = 7; a.seq = 42; a.time = 1234.5; a.pos = {1.5f, 2.f, -3.f}; a.vel = {10, 0, 0};
        a.rot = Quat::axisAngle({0, 1, 0}, 0.7f); a.rpm = 4200; a.gear = -1; a.wheelOmega[2] = 55.f; a.score = 99;
        uint8_t buf[NetSnapshot::WIRE_SIZE];
        CHECK(a.write(buf) == NetSnapshot::WIRE_SIZE, "size");
        NetSnapshot b;
        CHECK(b.read(buf, sizeof buf), "read");
        CHECK(b.id == 7 && b.seq == 42 && b.gear == -1 && std::fabs(b.rpm - 4200) < 1, "fields");
        CHECK(length(b.pos - a.pos) < 1e-5f && std::fabs(b.wheelOmega[2] - 55.f) < 0.05f, "pos/omega");
        RemoteVehicle rv;
        NetSnapshot s0 = a, s1 = a;
        s0.time = 10; s0.seq = 1; s0.pos = {0, 0, 0};
        s1.time = 10.1; s1.seq = 2; s1.pos = {1, 0, 0};
        rv.push(s0); rv.push(s1);
        NetSnapshot o;
        rv.sample(10.05, o);
        CHECK(std::fabs(o.pos.x - 0.5f) < 0.05f, "midpoint interp %.3f", o.pos.x);
        rv.sample(10.6, o);
        CHECK(o.pos.x < 1.f + 10.f * kMaxExtrapolate + 1e-3f, "extrapolation bounded");
        CHECK(std::fabs(clockOffset(1, 11, 11, 3) - 9.0) < 1e-9, "NTP offset");
    });

    test("world generation", [] {
        static World w;
        w.generate(1976);
        float hPit = 0, hOut = w.terrain.heightAt(0, w.pitZ + 30);
        for (int k = 0; k < 40; k++) hPit += w.terrain.heightAt(-30.f + 1.5f * float(k), w.laneZ(1)) / 40.f;  // floor is rough now
        CHECK(hPit < hOut - 0.2f, "pit should be excavated (%.2f vs %.2f)", hPit, hOut);
        TerrainSample s = w.terrain.sample(0, w.pitZ);
        CHECK(s.moisture > 0.8f && s.mudDepth() > 0.5f, "pit slurry: moist %.2f depth %.2f", s.moisture, s.mudDepth());
        CHECK(w.terrain.sample(w.pondCenter.x, w.pondCenter.z).waterDepth > 0.3f, "pond has water");
        CHECK(w.barrels.size() > 30 && w.posts.size() > 60 && w.props.size() > 300, "props: %zu barrels %zu posts %zu props",
              w.barrels.size(), w.posts.size(), w.props.size());
        CHECK(w.circuit.size() >= 10, "circuit");
    });

    test("physics: drives straight and stays upright on firm ground", [] {
        VehicleConfig c;
        Terrain t;
        makePit(t, 0.2f, 0);
        Vehicle v;
        v.configure(c);
        v.reset({-90, -v.rideAxleY + v.wheels[0].radius, 0}, PI / 2);
        VehicleInput in;
        in.throttle = 1;
        for (int i = 0; i < 240 * 5; i++) v.step(t, in, Vehicle::DT);
        CHECK(v.speed() > 12.f, "0-5s speed %.1f m/s", v.speed());
        CHECK(v.up().y > 0.95f, "upright");
        in.throttle = 0; in.brake = 1;
        float minSpeed = 1e9f;   // holding brake at a stop auto-selects reverse, so track the minimum
        for (int i = 0; i < 240 * 4; i++) { v.step(t, in, Vehicle::DT); minSpeed = std::min(minSpeed, std::fabs(v.forwardSpeed())); }
        CHECK(minSpeed < 0.5f, "brakes stop it (min %.1f)", minSpeed);
        CHECK(v.gear == -1 && v.forwardSpeed() < -0.5f, "brake held at a stop rolls into reverse");
    });

    test("physics: stock rig gets stuck in slop, digging itself in", [] {
        VehicleConfig s;
        s.tread = Tread::AllTerrain; s.lockRear = false; s.fourWD = false; s.tireIn = 31; s.liftIn = 0; s.widthIn = 10.5f; s.psi = 30;
        Terrain t;
        makePit(t, 1.f, 0);
        Vehicle v;
        v.configure(s);
        v.reset({10, t.heightAt(10, 0) - v.rideAxleY + v.wheels[0].radius + 0.02f, 0}, PI / 2);
        VehicleInput in;
        for (int i = 0; i < 240; i++) v.step(t, in, Vehicle::DT);
        float h0 = t.heightAt(v.wheels[2].contactPos.x, v.wheels[2].contactPos.z);
        in.throttle = 1;
        for (int i = 0; i < 240 * 8; i++) v.step(t, in, Vehicle::DT);
        float h1 = t.heightAt(v.wheels[2].contactPos.x, v.wheels[2].contactPos.z);
        CHECK(v.pos.x - 10 < 3.f, "should be stuck, moved %.1f m", v.pos.x - 10);
        CHECK(h1 < h0 - 0.15f, "spinning tires should dig a hole (%.2f -> %.2f)", h0, h1);
        CHECK(v.up().y > 0.7f, "no rollover");
    });

    test("physics: builds rank stock < default < built in a water-topped pit", [] {
        VehicleConfig stock, def, built;
        stock.tread = Tread::AllTerrain; stock.lockRear = false; stock.fourWD = false; stock.tireIn = 31; stock.liftIn = 0; stock.widthIn = 10.5f; stock.psi = 30;
        built.tread = Tread::Paddle; built.lockFront = true; built.tireIn = 40; built.engine = "v8_454"; built.psi = 10;
        float a = runPit(stock, 0.9f, 0.25f, -25, 14), b = runPit(def, 0.9f, 0.25f, -25, 14), c = runPit(built, 0.9f, 0.25f, -25, 14);
        std::printf("    stock %.1f m, default %.1f m, built %.1f m\n", a, b, c);
        CHECK(a < b && b < c, "ordering");
        CHECK(c > 60.f, "built rig should clear the pit");
    });

    test("modes: mud bog scoring", [] {
        static World w;
        w.generate(7);
        Vehicle v;
        v.configure(VehicleConfig{});
        ModeRunner m;
        m.start(ModeId::MudBog, w, v, 1, 0.0);
        CHECK(std::fabs(v.pos.z - w.laneZ(1)) < 0.01f, "spawned in lane");
        m.update(0.01f, 1.0, w, v, 0, 0, 0);
        CHECK(m.phase == Phase::Running, "countdown elapsed");
        v.pos.x = w.pitX0 + 20.f;                       // parked mid-pit: no clock, the run keeps going
        for (int i = 0; i < 600; i++) m.update(0.1f, 1.1 + i * 0.1, w, v, 0, 0, 0);
        CHECK(m.phase == Phase::Running, "standing still in the pit must not end the run");
        ModeRunner towed = m;
        towed.requestTow(w, v);
        CHECK(towed.phase == Phase::Finished && towed.results[0].board == "bog_distance", "dozer tow ends the run on distance");
        v.pos.x = w.pitX1 + 0.5f;
        m.update(0.01f, 61.1, w, v, 0, 0, 0);
        CHECK(m.phase == Phase::Finished && m.resultCount == 2 && m.results[0].board == "bog_time", "full pass result");
    });

    test("audio: engine + mud voices are finite and bounded", [] {
        AudioEngine a;
        a.init(48000);
        a.setListener({0, 2, -7}, {0, 0, 1}, {});
        std::vector<float> buf(4096 * 2);
        double sumsq = 0;
        float peak = 0;
        bool finite = true;
        int n = 0;
        for (auto& e : engineCatalog()) {
            VehicleSound s;
            s.engine = &e;
            s.local = true;
            for (float rpm = e.idleRpm; rpm < e.redlineRpm; rpm += 900) {
                s.rpm = rpm; s.throttle = 1; s.load = 1; s.mudSlap = 0.8f; s.lugRate = 30; s.waterSpeed = 5; s.boost = 0.7f;
                a.setVehicle(1, s);
                a.render(buf.data(), 4096);
                for (float x : buf) { finite &= std::isfinite(x); peak = std::max(peak, std::fabs(x)); sumsq += x * x; n++; }
            }
        }
        a.trigger(SfxType::Splash, {0, 0, 0}, 1.f);
        a.trigger(SfxType::Barrel, {2, 0, 0}, 1.f);
        a.render(buf.data(), 4096);
        float rms = float(std::sqrt(sumsq / n));
        CHECK(finite, "NaN/inf in audio output");
        CHECK(peak <= 1.f, "limiter: peak %.3f", peak);
        CHECK(rms > 0.01f, "audible output, rms %.4f", rms);
    });

    test("collisions: trucks can't drive through each other", [] {
        Terrain t;
        makePit(t, 0.2f, 0);
        Vehicle a, b;
        a.configure(VehicleConfig{});
        b.configure(VehicleConfig::parse("body=silverado;engine=duramax"));
        a.reset({-80, -a.rideAxleY + a.wheels[0].radius, 0}, PI / 2);
        b.reset({-60, -b.rideAxleY + b.wheels[0].radius, 0}, -PI / 2);
        VehicleInput in;
        in.throttle = 1;
        float minGap = 1e9f, maxJ = 0;
        for (int i = 0; i < 240 * 6; i++) {
            a.step(t, in, Vehicle::DT);
            b.step(t, in, Vehicle::DT);
            maxJ = std::max(maxJ, collideVehicles(a, b, true));
            minGap = std::min(minGap, std::fabs(a.pos.x - b.pos.x));
        }
        CHECK(maxJ > 100.f, "impact registered (%.0f N*s)", maxJ);
        CHECK(minGap > 4.9f, "bodies stayed apart (closest %.2f m between centers)", minGap);
        CHECK(a.pos.x < b.pos.x, "no tunneling");
    });

    test("mud bog: the starter truck can't rip through in one go", [] {
        VehicleConfig starter;                       // garage defaults
        float a = runBog(starter, 1, 45, false), b = runBog(starter, 2, 45, true);
        VehicleConfig built = VehicleConfig::parse("body=comanche;engine=ls53_sc;tire=40;tread=paddle;lockF=1;lockR=1;psi=9");
        float c = runBog(built, 1, 45, true);
        std::printf("    starter %.1f m (throttle) / %.1f m (rocking), built %.1f m of 80\n", a, b, c);
        CHECK(a < 70.f && b < 79.f, "starter should bog down before a full pass");
        CHECK(a > 6.f, "starter should at least get into the pit");
        CHECK(c > a, "a built rig should get further than the starter");
    });

    test("ai: rivals race the circuit and attack the bog", [] {
        static World w;
        w.generate(1976);
        ModeRunner m;
        m.id = ModeId::Circuit;
        auto rivals = makeRivals(2, 3);
        for (auto& r : rivals) {
            float yaw;
            Vec3 p = m.spawnFor(w, r.lane, yaw);
            p.y = w.terrain.heightAt(p.x, p.z) - r.v.rideAxleY + r.v.wheels[0].radius + 0.2f;
            r.v.reset(p, yaw);
        }
        for (int f = 0; f < 60 * 70; f++) {
            for (auto& r : rivals) {
                aiThink(r, ModeId::Circuit, w, 1.f / 60, f / 60.f, true);
                for (int s = 0; s < 4; s++) { r.v.step(w.terrain, r.input, Vehicle::DT); w.collideStatic(r.v, Vehicle::DT); }
                aiProgress(r, ModeId::Circuit, w, 1.f / 60, f / 60.f, 0);
            }
            collideVehicles(rivals[0].v, rivals[1].v, true);
        }
        for (auto& r : rivals) {
            std::printf("    %s: lap %d cp %d (%.1f)\n", r.name.c_str(), r.lap, r.nextCp, r.score);
            CHECK(r.score >= 0.5f, "%s should make real progress around the circuit", r.name.c_str());
        }
        AiDriver bog = makeRivals(1, 9)[0];
        bog.cfg = VehicleConfig::parse("body=comanche;engine=ls53_sc;tire=40;tread=paddle;lockF=1;lockR=1;psi=9");
        bog.v.configure(bog.cfg);
        m.id = ModeId::MudBog;
        float yaw;
        Vec3 p = m.spawnFor(w, 2, yaw);
        bog.lane = 2;
        p.y = w.terrain.heightAt(p.x, p.z) - bog.v.rideAxleY + bog.v.wheels[0].radius + 0.2f;
        bog.v.reset(p, yaw);
        for (int f = 0; f < 60 * 40 && !bog.finished && !bog.gaveUp; f++) {
            aiThink(bog, ModeId::MudBog, w, 1.f / 60, f / 60.f, true);
            for (int s = 0; s < 4; s++) bog.v.step(w.terrain, bog.input, Vehicle::DT);
            aiProgress(bog, ModeId::MudBog, w, 1.f / 60, f / 60.f, 0);
        }
        std::printf("    bog AI: %.1f m %s\n", bog.distance, bog.finished ? "(full pass)" : "");
        CHECK(bog.distance > 30.f, "built AI should get deep into the pit");
    });

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
