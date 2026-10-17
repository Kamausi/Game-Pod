// Native test suite: formulas, CAD kernel, netcode, world generation, vehicle physics
// behaviors (getting stuck, build differentiation), mode scoring and audio sanity.
#include <cmath>
#include <cstdio>
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

int main() {
    cadSetRoot("cad/parts");

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
        float hPit = w.terrain.heightAt(0, w.pitZ), hOut = w.terrain.heightAt(0, w.pitZ + 30);
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
        v.pos.x = w.pitX1 + 0.5f;
        m.update(0.01f, 1.1, w, v, 0, 0, 0);
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

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
