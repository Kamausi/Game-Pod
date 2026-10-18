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
#include "game/recovery.h"
#include "game/challenges.h"

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
static float runBog(const VehicleConfig& c, int lane, float secs, bool rock, float zOff = 0) {
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
    p.z += zOff;
    v.reset(p, yaw);
    float laneZ = w.laneZ(lane) + zOff, best = 0, stuckT = 0, rockT = 0;
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

#include "probe.inc"

int main(int argc, char** argv) {
    cadSetRoot("cad/parts");
    if (argc > 1 && std::string(argv[1]) == "--whoops") { debugWhoops(argc > 2 ? VehicleConfig::parse(argv[2]) : VehicleConfig{}); return 0; }
    if (argc > 1 && std::string(argv[1]) == "--bogdbg") { debugBog(argc > 2 ? VehicleConfig::parse(argv[2]) : VehicleConfig{}); return 0; }
    if (argc > 2 && std::string(argv[1]) == "--map") {   // top-down layout dump: mb_tests --map out.ppm
        static World w;
        w.generate(1976);
        const int M = 1024;
        const float S = w.terrain.size();
        std::vector<uint8_t> img(size_t(M) * M * 3);
        for (int j = 0; j < M; j++)
            for (int i = 0; i < M; i++) {
                int oi = i * w.overviewN / M, oj = j * w.overviewN / M;
                for (int c = 0; c < 3; c++) img[(size_t(j) * M + i) * 3 + c] = w.overview[(size_t(oj) * w.overviewN + oi) * 4 + c];
            }
        auto dot = [&](float x, float z, int r, uint8_t R, uint8_t G, uint8_t B) {
            int ci = int((x - w.terrain.originX()) / S * M), cj = int((z - w.terrain.originZ()) / S * M);
            for (int dj = -r; dj <= r; dj++) for (int di = -r; di <= r; di++) {
                int ii = ci + di, jj = cj + dj;
                if (ii < 0 || jj < 0 || ii >= M || jj >= M) continue;
                uint8_t* px = &img[(size_t(jj) * M + ii) * 3]; px[0] = R; px[1] = G; px[2] = B;
            }
        };
        for (auto& p : w.props) {
            if (p.part.find("vegetation/") != std::string::npos) { if (p.part.find("poplar") != std::string::npos || p.part.find("spruce") != std::string::npos) dot(p.pos.x, p.pos.z, 1, 20, 60, 20); continue; }
            if (p.part.find("wheels/") != std::string::npos) continue;
            dot(p.pos.x, p.pos.z, 2, 255, 40, 200);
        }
        for (auto& b : w.colliders) if (b.half.x > 0.5f) dot(b.center.x, b.center.z, 1, 255, 255, 255);
        FILE* f = std::fopen(argv[2], "wb");
        std::fprintf(f, "P6 %d %d 255\n", M, M);
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--lane") { debugLane(VehicleConfig{}); return 0; }
    if (argc > 1 && std::string(argv[1]) == "--climb") { debugClimb(VehicleConfig{}, argc > 2 ? float(std::atof(argv[2])) : 20.f); return 0; }
    if (argc > 1 && std::string(argv[1]) == "--probe") {
        runProbe(argc > 2 ? VehicleConfig::parse(argv[2]) : VehicleConfig{});
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "--bog") {   // tuning probe: mb_tests --bog "<cfg>" [secs] [lane]
        float secs = argc > 3 ? float(std::atof(argv[3])) : 120.f;
        int lane = argc > 4 ? std::atoi(argv[4]) : 1;
        float off = argc > 5 ? float(std::atof(argv[5])) : 0.f;
        VehicleConfig c = VehicleConfig::parse(argv[2]);
        std::printf("%s: throttle %.1f m, rocking %.1f m (of 80, %g s)\n", argv[2], runBog(c, lane, secs, false, off), runBog(c, lane, secs, true, off), secs);
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
                               "props/dozer450.mbcad", "props/spectator.mbcad", "vegetation/poplar.mbcad", "vegetation/teasel.mbcad",
                               "props/tractionboard.mbcad", "props/winchline.mbcad",
                               "vehicles/squarebody.mbcad",
                               "vehicles/fullsize_suv.mbcad",
                               "vehicles/atv.mbcad",
                               "vehicles/megatruck.mbcad",
                               "props/trailer_flatbed.mbcad",
                               "props/trailer_enclosed.mbcad",
                               "props/rv_motorhome.mbcad",
                               "props/camper_trailer.mbcad",
                               "props/tractor_loader.mbcad",
                               "props/dually_hauler.mbcad",
                               "props/pickup_parked.mbcad",
                               "props/sedan_parked.mbcad",
                               "venue/announcer_tower.mbcad",
                               "venue/banner.mbcad",
                               "venue/bleachers.mbcad",
                               "venue/canopy.mbcad",
                               "venue/cone.mbcad",
                               "venue/dome_tent.mbcad",
                               "venue/dumpster.mbcad",
                               "venue/fire_ring.mbcad",
                               "venue/flag_pole.mbcad",
                               "venue/folding_table.mbcad",
                               "venue/food_truck.mbcad",
                               "venue/gate_arch.mbcad",
                               "venue/generator.mbcad",
                               "venue/handwash.mbcad",
                               "venue/hay_bale.mbcad",
                               "venue/light_tower.mbcad",
                               "venue/metal_building.mbcad",
                               "venue/office_trailer.mbcad",
                               "venue/pavilion.mbcad",
                               "venue/picnic_table.mbcad",
                               "venue/pit_clutter.mbcad",
                               "venue/portajohn.mbcad",
                               "venue/results_board.mbcad",
                               "venue/rv_hookup.mbcad",
                               "venue/sign.mbcad",
                               "venue/stage.mbcad",
                               "venue/ticket_booth.mbcad",
                               "venue/tire_stack.mbcad",
                               "venue/trash_barrel.mbcad",
                               "venue/washbay.mbcad",
                               "venue/water_tank.mbcad"};
        for (auto p : parts) {
            CadResult r = cadBuild(p);
            CHECK(r.error.empty() && r.mesh.idx.size() > 12, "%s: %s", p, r.error.c_str());
            CHECK(r.scad.find("rotate([90,0,0])") != std::string::npos, "%s: SCAD export missing", p);
        }
        std::string err;
        CHECK(std::fabs(cadEval("2*(3+4)-max(1,5)/5", {}, &err) - 13.f) < 1e-5f && err.empty(), "expr eval: %s", err.c_str());
        CadResult bad = cadBuildSource("box size=1,1\n", {}, "bad");
        CHECK(!bad.error.empty(), "malformed size should error");
        CadResult one = cadBuildSource("box size=1,1,1\n", {}, "one");
        CadResult loop = cadBuildSource("for i = 0, 4\n  for j = 0, 1\n    box size=0.5,0.5,0.5 pos=i*2,j*2,0\n  end\nend\n", {}, "loop");
        CHECK(loop.error.empty() && loop.mesh.verts.size() == one.mesh.verts.size()*10, "for loops: %s (%zu verts)", loop.error.c_str(), loop.mesh.verts.size());
        CHECK(std::fabs(loop.mesh.boundsMax().x - 8.25f) < 1e-4f, "loop var reaches the inclusive end");
        CHECK(!cadBuildSource("for i = 0, 3\nbox size=1,1,1\n", {}, "open").error.empty(), "unterminated for should error");
        CHECK(!cadBuildSource("end\n", {}, "stray").error.empty(), "stray end should error");
        CHECK(!cadBuildSource("box size=1,1,1 pos=0,0,3.18.0\n", {}, "badnum").error.empty(), "malformed number should error");
        CadResult txt = cadBuildSource("text str=MUD_7 size=0.5 t=0.1\n", {}, "text");
        CHECK(txt.error.empty() && !txt.mesh.verts.empty(), "text primitive: %s", txt.error.c_str());
        CHECK(std::fabs(txt.mesh.boundsMax().x + txt.mesh.boundsMin().x) < 0.06f && txt.mesh.boundsMax().y > 0.3f, "text is centred, cap height ~0.7");
        CHECK(!cadBuildSource("text str=A?B\n", {}, "badtext").error.empty(), "unknown glyph should error");
        CadResult flat = cadBuildSource("param on = 0\nbox size=1*on,1,1\ncyl r=0.2 h=0.5*on\nsphere r=0.3 scale=1,on,1\nbox size=1,1,1\n", {}, "flat");
        CHECK(flat.error.empty() && flat.mesh.verts.size() == one.mesh.verts.size(), "zero-size features emit nothing (%zu verts)", flat.mesh.verts.size());
        CHECK(std::fabs(cadEval("1.5e2+2e-1", {}, &err) - 150.2f) < 1e-3f && err.empty(), "exponent literals: %s", err.c_str());
        CadResult hexLoop = cadBuildSource("for i = 0, 1\n  color #ff0000\n  box size=1,1,1 pos=i*2,0,0\nend\n", {}, "hexloop");
        CHECK(hexLoop.error.empty() && !hexLoop.mesh.verts.empty() && hexLoop.mesh.verts[0].r > 0.99f, "indented hex color: %s", hexLoop.error.c_str());
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

    test("physics: handling sanity (climb, whoops, lane change, parked)", [] {
        static Terrain t;
        VehicleConfig c;
        auto spawn = [&](Vehicle& v, Vec3 p, float yaw) { v.configure(c); v.reset(p, yaw); v.placeOnGround(t); settle(v, t); };
        fieldTerrain(t, 20);
        { Vehicle v; spawn(v, {-30, 0, 0}, PI / 2); VehicleInput in; in.throttle = 1;
          for (int i = 0; i < 240 * 6; i++) v.step(t, in, Vehicle::DT);
          CHECK(v.pos.x + 30 > 15.f, "should climb a 20 deg grass slope (%.1f m)", v.pos.x + 30); }
        fieldTerrain(t, 15, true);
        { Vehicle v; spawn(v, {0, 0, 0}, PI / 2); Vec3 p0 = v.pos; VehicleInput in; in.handbrake = true;
          for (int i = 0; i < 240 * 10; i++) v.step(t, in, Vehicle::DT);
          CHECK(length(Vec3{v.pos.x - p0.x, 0, v.pos.z - p0.z}) < 1.f, "parked on a side slope shouldn't creep away"); }
        fieldTerrain(t);
        { Vehicle v; spawn(v, {-80, 0, 0}, PI / 2); v.vel = v.forward() * 20.f; for (auto& w : v.wheels) w.omega = 20.f / w.radius;
          VehicleInput in; in.throttle = 0.4f;
          for (int i = 0; i < 240 * 5; i++) { in.steer = i < 480 ? std::sin(i * Vehicle::DT * PI) : 0.f; v.step(t, in, Vehicle::DT); }
          Vec3 f = v.forward();
          CHECK(std::fabs(std::atan2(f.z, f.x)) < 20.f * DEG && v.up().y > 0.9f, "lane change at 45 mph should recover, heading %.0f deg",
                std::atan2(f.z, f.x) / DEG); }
        for (float amp : {0.08f, 0.18f}) {
            for (int j = 0; j < Terrain::N; j++)
                for (int i = 0; i < Terrain::N; i++) {
                    Vec3 w = t.cellToWorld(i, j); int k = t.idx(i, j);
                    t.h[k] = (w.x > -40 && w.x < 40) ? amp * std::sin(w.x * 2 * PI / 5.f) : 0.f; t.base[k] = t.h[k] - 0.05f;
                }
            Vehicle v; spawn(v, {-60, 0, 0}, PI / 2); v.vel = v.forward() * 10.f; for (auto& w : v.wheels) w.omega = 10.f / w.radius;
            VehicleInput in; float pitch = 0; bool flipped = false;
            for (int i = 0; i < 240 * 8; i++) {
                in.throttle = v.speed() < 10.f ? 0.7f : 0.f;
                v.step(t, in, Vehicle::DT);
                pitch = std::max(pitch, std::fabs(pitchDeg(v)));
                flipped = flipped || v.up().y < 0.3f;
            }
            CHECK(!flipped, "whoops (%.0f cm) at 22 mph must not flip the truck", amp * 200.f);
            if (amp < 0.1f) CHECK(pitch < 12.f, "moderate whoops pitch %.1f deg", pitch);
        }
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
            std::printf("    %s: lap %d cp %d (%.1f)%s\n", r.name.c_str(), r.lap, r.nextCp, r.score, r.v.frontShaftBroken ? " [front shaft broken]" : "");
            CHECK(!r.v.frontShaftBroken && !r.v.engineBroken, "%s shouldn't break parts just racing the circuit", r.name.c_str());
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


    test("field guide: spec sheet (crawl ratio, regear, diff clearance, class)", [] {
        VehicleConfig c;   // comanche, 35s on 4.56s
        SpecSheet s = specSheet(c);
        std::printf("    %s\n", s.json().c_str());
        CHECK(std::fabs(s.crawlRatio - 2.48f * 2.72f * 4.56f) < 0.05f, "crawl ratio %.1f", s.crawlRatio);
        CHECK(s.diffClearIn > 9.f && s.diffClearIn < 12.f, "35in tire diff clearance %.1f in", s.diffClearIn);
        VehicleConfig big = VehicleConfig::parse("body=comanche;tire=44;tread=paddle;psi=8");
        SpecSheet b = specSheet(big);
        CHECK(b.regearTarget > s.regearTarget && std::string(b.cls) == "Pro", "44s want deeper gears (%.2f) and run Pro", b.regearTarget);
        CHECK(b.groundKpa < s.groundKpa, "aired-down big tires float: %.0f vs %.0f kPa", b.groundKpa, s.groundKpa);
        CHECK(b.effRatio < s.effRatio, "same axle on taller tires is taller gearing");
    });

    test("field guide: hydrolock - dead until dry, cranking underwater bends a rod", [] {
        static Terrain t;
        makePit(t, 0.9f, 3.f);
        VehicleConfig c;
        Vehicle v;
        v.configure(c);
        v.reset({30, t.heightAt(30, 0) - v.rideAxleY + v.wheels[0].radius + 0.02f, 0}, PI / 2);
        VehicleInput in;
        bool flooded = false;
        for (int i = 0; i < 240 * 3; i++) { v.step(t, in, Vehicle::DT); flooded = flooded || v.flooded; }
        CHECK(flooded, "submerged intake floods the engine");
        in.throttle = 1;
        for (int i = 0; i < 240 * 3; i++) v.step(t, in, Vehicle::DT);
        CHECK(v.engineBroken, "cranking it while swamped bends a rod");
        // On dry ground: wait for it to drain, then it restarts.
        fieldTerrain(t);
        Vehicle d;
        d.configure(c);
        d.reset({0, 0, 0}, PI / 2);
        d.placeOnGround(t);
        settle(d, t);
        d.flooded = true;
        VehicleInput w;
        for (int i = 0; i < 240 * 1; i++) d.step(t, w, Vehicle::DT);
        CHECK(d.flooded, "still flooded right after");
        for (int i = 0; i < 240 * 3; i++) d.step(t, w, Vehicle::DT);
        w.throttle = 1;
        for (int i = 0; i < 240 * 3; i++) d.step(t, w, Vehicle::DT);
        CHECK(!d.flooded && !d.engineBroken && d.speed() > 2.f, "drained engine restarts and drives (%.1f m/s)", d.speed());
    });

    test("field guide: part-time 4WD windup breaks the front shaft on grippy ground", [] {
        static Terrain t;
        fieldTerrain(t, 0, false, 0.f);
        auto circle = [&](int tc) {
            VehicleConfig c;
            Vehicle v;
            v.configure(c);
            v.reset({0, 0, 0}, PI / 2);
            v.placeOnGround(t);
            settle(v, t);
            v.setTransfer(tc);
            VehicleInput in;
            in.throttle = 0.35f; in.steer = 1.f;
            float maxW = 0;
            for (int i = 0; i < 240 * 25 && !v.frontShaftBroken; i++) { v.step(t, in, Vehicle::DT); maxW = std::max(maxW, v.windup); }
            std::printf("    tcase %d: windup peak %.2f %s\n", tc, maxW, v.frontShaftBroken ? "(front U-joint snapped)" : "");
            return v.frontShaftBroken;
        };
        CHECK(circle(1), "tight circles in 4H on dirt should snap the front driveline");
        CHECK(!circle(0), "2H never binds");
    });

    test("field guide: UTV CVT belt cooks in high range, survives in low", [] {
        auto run = [](bool low) {
            static Terrain t;
            makePit(t, 0.95f, 0);
            VehicleConfig c = VehicleConfig::parse("body=utv;engine=twin_1000t;tire=30;width=10;awd=1");
            Vehicle v;
            v.configure(c);
            v.reset({8, t.heightAt(8, 0) - v.rideAxleY + v.wheels[0].radius + 0.02f, 0}, PI / 2);
            v.setTransfer(low ? 2 : 1);
            VehicleInput in;
            in.throttle = 1;
            float peak = 0;
            for (int i = 0; i < 240 * 40; i++) { v.step(t, in, Vehicle::DT); peak = std::max(peak, v.beltTemp); if (i % 4 == 0) t.relax(v.pos, 5, 4 * Vehicle::DT); }
            std::printf("    %s range: belt peak %.0f C%s, moved %.1f m\n", low ? "low" : "high", peak, v.beltBurnt ? " BURNT" : "", v.pos.x - 8);
            return peak;
        };
        float hi = run(false), lo = run(true);
        CHECK(hi > 130.f, "slogging in high range should overheat the belt (%.0f C)", hi);
        CHECK(lo < 130.f, "low range keeps the belt alive (%.0f C)", lo);
    });

    test("field guide: aired down to 5 psi, hard cornering pops a bead unless beadlocked", [] {
        auto run = [](bool bead) {
            static Terrain t;
            fieldTerrain(t, 0, false, 0.f);
            VehicleConfig c;
            c.psi = 5; c.beadlock = bead;
            Vehicle v;
            v.configure(c);
            v.reset({-60, 0, 0}, PI / 2);
            v.placeOnGround(t);
            settle(v, t);
            VehicleInput in;
            bool popped = false;
            for (int i = 0; i < 240 * 20 && !popped; i++) {
                in.throttle = 0.8f;
                in.steer = std::sin(i * Vehicle::DT * 2.5f);
                v.step(t, in, Vehicle::DT);
                for (auto& w : v.wheels) popped = popped || w.debeaded;
            }
            return popped;
        };
        CHECK(run(false), "5 psi without beadlocks should roll a bead off in a slalom");
        CHECK(!run(true), "beadlocks hold the bead");
    });

    test("field guide: winch and traction boards get a stuck rig out", [] {
        static World w;
        auto stuck = [&](Vehicle& v, VehicleConfig c) {
            makePit(w.terrain, 1.f, 0);
            v.configure(c);
            v.reset({10, w.terrain.heightAt(10, 0) - v.rideAxleY + v.wheels[0].radius + 0.02f, 0}, PI / 2);
            VehicleInput in;
            for (int i = 0; i < 240; i++) v.step(w.terrain, in, Vehicle::DT);
            in.throttle = 1;
            for (int i = 0; i < 240 * 6; i++) v.step(w.terrain, in, Vehicle::DT);
        };
        VehicleConfig s;
        s.tread = Tread::AllTerrain; s.lockRear = false; s.fourWD = false; s.tireIn = 31; s.liftIn = 0; s.widthIn = 10.5f; s.psi = 30; s.winch = true;
        // Winch to a tree 25 m ahead.
        {
            Vehicle v;
            Recovery r;
            stuck(v, s);
            float x0 = v.pos.x;
            w.props.clear();
            PropInstance tree;
            tree.part = "vegetation/poplar.mbcad";
            tree.pos = {v.pos.x + 25.f, 0, 0.5f};
            w.props.push_back(tree);
            std::string msg = r.toggleWinch(w, v);
            CHECK(r.winch.active, "winch hooks a tree ahead: %s", msg.c_str());
            VehicleInput in;
            in.throttle = 0.2f;
            for (int i = 0; i < 240 * 40 && r.winch.active; i++) { r.stepWinch(v, Vehicle::DT); v.step(w.terrain, in, Vehicle::DT); }
            std::printf("    winch: %.1f m -> %.1f m (%s)\n", x0, v.pos.x, r.winch.broken ? "snapped" : "ok");
            CHECK(v.pos.x - x0 > 8.f && !r.winch.broken, "winch drags the rig toward the anchor (%.1f m)", v.pos.x - x0);
        }
        // The guide's sequence: stop spinning, shovel out the belly, boards under the drive tires, ease on.
        {
            Vehicle v;
            Recovery r;
            v.pads = &r.pads;
            stuck(v, s);
            VehicleInput in;
            in.handbrake = true;
            float x0 = v.pos.x, belly0 = v.bellyLoad;
            for (int i = 0; i < 240; i++) v.step(w.terrain, in, Vehicle::DT);
            r.startShovel(v);
            for (int i = 0; i < 240 * 6; i++) { v.step(w.terrain, in, Vehicle::DT); if (i % 24 == 0) r.update(w, v, 24 * Vehicle::DT); }
            for (int i = 0; i < 240; i++) v.step(w.terrain, in, Vehicle::DT);
            v.gear = 1;
            std::string msg = r.placeBoards(w, v);
            CHECK(r.pads.size() == 2, "boards placed: %s", msg.c_str());
            in.handbrake = false; in.throttle = 0.45f;
            float onPad = 0;
            for (int i = 0; i < 240 * 6; i++) { v.step(w.terrain, in, Vehicle::DT); onPad = std::max(onPad, float(v.onPad)); }
            std::printf("    shovel + boards: belly %.2f -> moved %.1f m\n", belly0, v.pos.x - x0);
            CHECK(onPad > 0, "tires climbed onto the boards");
            CHECK(v.pos.x - x0 > 1.5f, "dig + boards gets the stuck rig moving (%.1f m)", v.pos.x - x0);
        }
    });


    test("venue: grounds layout (roads, paddock, wash bay, bounty trench, campground)", [] {
        static World w;
        w.generate(1976);
        CHECK(w.terrain.sample(w.gatePos.x, w.gatePos.z + 2).soil == uint8_t(Soil::Gravel), "gravel at the gate");
        CHECK(w.terrain.sample(0, -70).soil == uint8_t(Soil::Gravel), "entrance road is gravel");
        CHECK(w.terrain.sample(-10, -58).soil == uint8_t(Soil::Gravel), "return road is gravel");
        CHECK(w.terrain.sample(w.washCenter.x, w.washCenter.z).soil == uint8_t(Soil::Gravel), "wash bay apron");
        Vec3 bc = w.bountyCenter();
        TerrainSample b = w.terrain.sample(bc.x, bc.z);
        CHECK(b.waterDepth > 0.1f && b.mudDepth() > 1.2f && b.soil == uint8_t(Soil::Clay), "bounty trench: water %.2f soft %.2f", b.waterDepth, b.mudDepth());
        int trees = 0, buildings = 0, people = 0;
        for (auto& p : w.props) {
            bool tree = p.part.find("poplar") != std::string::npos || p.part.find("spruce") != std::string::npos;
            if (tree && (w.builtUp(p.pos.x, p.pos.z))) trees++;
            if (p.part.find("venue/") != std::string::npos) buildings++;
            if (p.part.find("spectator") != std::string::npos) people++;
        }
        std::printf("    %d venue props, %d people\n", buildings, people);
        CHECK(trees == 0, "%d trees growing in built-up grounds", trees);
        CHECK(buildings > 150 && people > 120, "grounds populated");
        // Spawns and the camp road stay clear of colliders.
        auto blocked = [&](float x, float z) {
            for (auto& c : w.colliders) {
                float dx = x - c.center.x, dz = z - c.center.z, cs = std::cos(c.yaw), sn = std::sin(c.yaw);
                float lx = dx * cs - dz * sn, lz = dx * sn + dz * cs;
                if (std::fabs(lx) < c.half.x + 1.f && std::fabs(lz) < c.half.z + 1.f) return true;
            }
            return false;
        };
        CHECK(!blocked(w.garageSpot.x, w.garageSpot.z), "garage spot clear");
        for (float t = 0; t <= 1.f; t += 0.05f) {
            Vec3 a = w.returnRoad[2], c = w.returnRoad[3];
            Vec3 p = lerp(a, c, t);
            CHECK(!blocked(p.x, p.z), "return road blocked at %.0f,%.0f", p.x, p.z);
        }
        CHECK(!blocked(w.bountyX0 - 8, w.bountyZ), "bounty start clear");
        for (auto& fp : w.posts) CHECK(length(Vec3{fp.base.x - (w.bountyX0 - 8), 0, fp.base.z - w.bountyZ}) > 3.f, "fence post at the bounty start");
    });

    test("vehicles: every body drives and stays upright (squarebody, SUV, ATV, mega truck)", [] {
        static Terrain t;
        fieldTerrain(t);
        for (const char* body : {"squarebody", "fullsize_suv", "atv", "megatruck", "comanche", "silverado", "utv", "bogcar76"}) {
            VehicleConfig c = VehicleConfig::parse(std::string("body=") + body + ";engine=" + findBody(body).defaultEngine +
                                                   ";tire=" + std::to_string(int(findBody(body).defaultTireIn)) + (std::string(body) == "megatruck" ? ";width=22" : ""));
            Vehicle v;
            v.configure(c);
            v.reset({-60, 0, 0}, PI / 2);
            v.placeOnGround(t);
            settle(v, t);
            float h0 = v.pos.y;
            VehicleInput in;
            in.throttle = 1;
            for (int i = 0; i < 240 * 5; i++) v.step(t, in, Vehicle::DT);
            std::printf("    %-13s %.0f kg  5 s: %.1f m/s  ride %.2f m\n", body, v.mass, v.speed(), h0);
            CHECK(v.speed() > 6.f && v.up().y > 0.9f && std::isfinite(v.pos.x), "%s should drive off upright", body);
        }
        // The mega truck floats over the slurry pit that swallows a stock rig.
        VehicleConfig mega = VehicleConfig::parse("body=megatruck;engine=ls53_sc;tire=54;width=22;tread=paddle;psi=8;bead=1");
        VehicleConfig atv = VehicleConfig::parse("body=atv;engine=twin_1000t;tire=30;width=10;tread=paddle;psi=7;bead=1");
        float m = runPit(mega, 0.9f, 0.25f, -25, 14), a = runPit(atv, 0.9f, 0.25f, -25, 14);
        std::printf("    pit: mega truck %.1f m, mud ATV %.1f m\n", m, a);
        CHECK(m > 60.f, "a mega truck clears the slurry pit");
        CHECK(a > 0.f, "the ATV gets into the pit");
    });

    test("modes: bounty hole (pot grows on failure, claim pays out)", [] {
        static World w;
        w.generate(1976);
        ModeRunner m;
        Vehicle v;
        v.configure(VehicleConfig{});
        m.start(ModeId::Bounty, w, v, 0, 0);
        CHECK(v.pos.x < w.bountyX0 && std::fabs(v.pos.z - w.bountyZ) < 1.f, "spawned at the bounty start");
        m.update(0.1f, 1.0, w, v, 0, 0, 0);
        CHECK(m.phase == Phase::Running, "running");
        int pot0 = m.bountyPot;
        v.pos.x = w.bountyX0 + 10.f;
        m.update(0.1f, 1.1, w, v, 0, 0, 0);
        CHECK(std::fabs(m.distance - 10.f) < 0.5f, "distance into the hole %.1f", m.distance);
        m.requestTow(w, v);
        CHECK(m.phase == Phase::Finished && m.bountyPot == pot0 + 250 && m.results[0].board == "bounty_distance", "tow grows the pot");
        CHECK(m.payout == int(10.f * 3.281f * 2.f), "failed attempt pays per foot (%d)", m.payout);
        m.start(ModeId::Bounty, w, v, 0, 0);
        m.update(0.1f, 1.0, w, v, 0, 0, 0);
        v.pos.x = w.bountyX1 + 0.5f;
        m.update(0.1f, 1.1, w, v, 0, 0, 0);
        CHECK(m.claimed && m.results[0].board == "bounty_time" && m.bountyPot == 1500, "claimed, pot resets");
        CHECK(m.payout == 1750, "claimed bounty pays the pot (%d)", m.payout);
    });


    test("open pits: back-country challenges complete once and pay out", [] {
        static World w;
        w.generate(1976);
        Challenges ch;
        Vehicle v;
        v.configure(VehicleConfig{});
        auto at = [&](float x, float z) { v.reset({x, w.terrain.heightAt(x, z) + 1.f, z}, 0); v.wheelsInContact = 4; v.vel = {}; return ch.update(w, v, 0.1f); };
        CHECK(at(0, -80) == -1, "nothing in the village");
        CHECK(at(w.hillCenter.x, w.hillCenter.z) == 0 && ch.lastReward == 300, "hill top");
        CHECK(at(w.hillCenter.x, w.hillCenter.z) == -1, "only once");
        Vec3 s = w.swampCenter;
        at(s.x - w.swampRadius - 3, s.z);
        at(s.x - w.swampRadius + 3, s.z);
        at(s.x, s.z);
        CHECK(at(s.x + w.swampRadius + 3, s.z) == 1, "swamp crossing");
        float cx = -60.f, cz = w.creekZ(cx);
        at(cx, cz - w.creekHalfW - 2);
        CHECK(at(cx, cz + 1.f) == 2 || at(cx, cz + 0.5f) == 2, "creek ford");
        v.reset({w.flatsCenter.x, 0, w.flatsCenter.z}, 0);
        v.vel = {16.f, 0, 0};
        int r = -1;
        for (int i = 0; i < 40 && r < 0; i++) r = ch.update(w, v, 0.1f);
        CHECK(r == 3, "flats speed run");
        size_t n = w.trail.size();
        for (unsigned q = 0; q < 3; q++) at(w.trail[q * n / 4].x, w.trail[q * n / 4].z);
        CHECK(at(w.trail[3 * n / 4].x, w.trail[3 * n / 4].z) == 4, "trail loop");
        int serial = ch.rewardSerial;
        CHECK(serial == 5, "five rewards (%d)", serial);
    });


    test("weather: rain soaks the map, gravel drains, soaked ground gives way", [] {
        static World w;
        w.generate(1976);
        TerrainSample field0 = w.terrain.sample(-30, 60), road0 = w.terrain.sample(0, -70);
        w.terrain.rainWet = 1.f;
        TerrainSample field1 = w.terrain.sample(-30, 60), road1 = w.terrain.sample(0, -70);
        std::printf("    field moisture %.2f -> %.2f, gravel road %.2f -> %.2f\n", field0.moisture, field1.moisture, road0.moisture, road1.moisture);
        CHECK(field1.moisture > field0.moisture + 0.3f, "rain soaks the fields");
        CHECK(road1.moisture - road0.moisture < 0.15f, "gravel drains");
        // A stock rig on a soaked field sinks deeper than on a dry one.
        auto sink = [&](float wet) {
            w.terrain.rainWet = wet;
            static World w2; w2.generate(1976); w2.terrain.rainWet = wet;
            Vehicle v; v.configure(VehicleConfig{});
            v.reset({-30, w2.terrain.heightAt(-30, 60) + 1.f, 60}, 0); v.placeOnGround(w2.terrain);
            VehicleInput in; in.handbrake = true;
            for (int i = 0; i < 240 * 3; i++) v.step(w2.terrain, in, Vehicle::DT);
            return w2.terrain.heightAt(v.wheels[0].contactPos.x, v.wheels[0].contactPos.z);
        };
        float dry = sink(0.f), wet = sink(1.f);
        std::printf("    parked rut: dry %.3f m, soaked %.3f m\n", dry, wet);
        CHECK(wet < dry, "soaked ground gives way more under a parked truck");
        // The pit and the bounty trench fill up.
        float pit0 = w.terrain.sample(0, w.laneZ(1)).waterDepth, b0 = w.terrain.sample(w.bountyCenter().x, w.bountyZ).waterDepth;
        w.setRainRise(0.18f);
        float pit1 = w.terrain.sample(0, w.laneZ(1)).waterDepth, b1 = w.terrain.sample(w.bountyCenter().x, w.bountyZ).waterDepth;
        std::printf("    pit water %.2f -> %.2f m, bounty %.2f -> %.2f m (%zu pool cells)\n", pit0, pit1, b0, b1, w.rainPools.size());
        CHECK(pit1 > pit0 + 0.15f && b1 > b0 + 0.15f, "rain fills the pools");
        w.setRainRise(0.f);
        CHECK(std::fabs(w.terrain.sample(0, w.laneZ(1)).waterDepth - pit0) < 0.01f, "and they drain back");
        w.terrain.rainWet = 0;
    });


    test("audio: rain bed and delayed thunder are audible, finite and bounded", [] {
        auto rms = [](AudioEngine& a, float secs, float* peak) {
            std::vector<float> buf(2048 * 2);
            double sq = 0; long n = 0;
            for (int b = 0; b < int(secs * 48000 / 2048); b++) {
                a.render(buf.data(), 2048);
                for (float x : buf) { if (!std::isfinite(x)) return -1.0; *peak = std::max(*peak, std::fabs(x)); sq += x * x; n++; }
            }
            return std::sqrt(sq / double(n));
        };
        AudioEngine a;
        a.init(48000);
        a.setListener({0, 2, -7}, {0, 0, 1}, {});
        a.setAmbient({500, 0, 500}, 0.f);
        float peak = 0;
        double quiet = rms(a, 1.f, &peak);
        a.setRain(1.f);
        double wet = rms(a, 1.f, &peak);
        a.thunder(343.f);   // arrives after ~1 s
        double before = rms(a, 0.8f, &peak), boom = rms(a, 1.5f, &peak);
        std::printf("    rms: dry %.4f, rain %.4f, rain+thunder %.4f -> %.4f, peak %.2f\n", quiet, wet, before, boom, peak);
        CHECK(wet > quiet * 2 && wet > 0.005, "rain is audible");
        CHECK(boom > before * 1.3, "thunder rolls in after the sound's travel time");
        CHECK(peak <= 1.f && quiet >= 0 && boom >= 0, "finite and limited");
    });


    test("burnout: line lock holds the fronts, the rears smoke, points add up", [] {
        static World w;
        w.generate(1976);
        for (const char* cfgs : {"body=comanche;engine=v8_350;awd=0", "body=squarebody;engine=v8_454;awd=1"}) {
            ModeRunner m;
            Vehicle v;
            v.configure(VehicleConfig::parse(cfgs));
            m.start(ModeId::Burnout, w, v, 0, 0);
            v.setTransfer(0);   // 2H
            m.update(0.01f, 1.0, w, v, 0, 0, 0);
            VehicleInput in;
            in.throttle = 1; in.brake = 1;
            Vec3 p0 = v.pos;
            float maxSlip = 0;
            for (int f = 0; f < 60 * 10 && m.phase == Phase::Running; f++) {
                for (int s = 0; s < 4; s++) v.step(w.terrain, in, Vehicle::DT);
                m.update(1.f / 60, 2.0 + f / 60.0, w, v, 0, 0, 0);
                maxSlip = std::max(maxSlip, std::fabs(v.wheels[2].omega * v.wheels[2].radius));
            }
            float moved = length(Vec3{v.pos.x - p0.x, 0, v.pos.z - p0.z});
            std::printf("    %s: rear tread %.0f m/s, moved %.1f m, %.0f pts, front omega %.1f\n", cfgs, maxSlip, moved, m.score, v.wheels[0].omega);
            CHECK(v.lineLock || m.phase != Phase::Running, "line lock engaged");
            CHECK(maxSlip > 12.f && moved < 3.f, "rears spin while the truck stays put");
            CHECK(m.score > 200.f, "smoke points");
        }
    });

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
