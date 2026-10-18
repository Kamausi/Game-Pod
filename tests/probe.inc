// Vehicle-dynamics probe: standard manoeuvres on synthetic terrain, printed as metrics.
// Run with: mb_tests --probe [cfg]
static void fieldTerrain(Terrain& t, float slopeDeg = 0, bool sideSlope = false, float grass = 0.85f) {
    float sl = std::tan(slopeDeg * DEG);
    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            int k = t.idx(i, j);
            Vec3 w = t.cellToWorld(i, j);
            t.h[k] = (sideSlope ? w.z : w.x) * sl;
            t.base[k] = t.h[k] - 0.05f; t.moist[k] = 0.25f; t.grass[k] = uint8_t(grass * 255); t.comp[k] = 0.08f;
            t.water[k] = Terrain::NO_WATER;
        }
}
static void settle(Vehicle& v, Terrain& t, float secs = 1.5f) {
    VehicleInput in;
    in.handbrake = true;
    for (int i = 0; i < int(secs * 240); i++) v.step(t, in, Vehicle::DT);
}
static float rollDeg(const Vehicle& v) { return std::asin(clampf(dot(v.left(), Vec3{0, 1, 0}), -1, 1)) / DEG; }
static float pitchDeg(const Vehicle& v) { return std::asin(clampf(dot(v.forward(), Vec3{0, 1, 0}), -1, 1)) / DEG; }

static void runProbe(const VehicleConfig& c) {
    static Terrain t;
    std::printf("== probe: %s\n", c.serialize().c_str());
    auto fresh = [&](Vehicle& v, Vec3 p, float yaw) {
        v.configure(c);
        p.y = t.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.05f;
        v.reset(p, yaw);
        v.placeOnGround(t);
        settle(v, t);
    };
    // 1. Acceleration and top speed on grass
    {
        fieldTerrain(t);
        Vehicle v; fresh(v, {-90, 0, 0}, PI / 2);
        VehicleInput in; in.throttle = 1;
        float t60 = -1, top = 0;
        for (int i = 0; i < 240 * 25; i++) {
            v.step(t, in, Vehicle::DT);
            if (t60 < 0 && v.speed() > 26.8f) t60 = i * Vehicle::DT;
            top = std::max(top, v.speed());
            if (v.pos.x > 85) break;
        }
        std::printf("  accel: 0-60 mph %.1f s, best %.1f mph in 175 m, mass %.0f kg\n", t60, top * 2.237f, v.mass);
    }
    // 2. Braking from 20 m/s
    {
        fieldTerrain(t);
        Vehicle v; fresh(v, {-90, 0, 0}, PI / 2);
        v.vel = v.forward() * 20.f;
        for (auto& w : v.wheels) w.omega = 20.f / w.radius;
        VehicleInput in; in.brake = 1;
        float x0 = v.pos.x, maxPitch = 0;
        int i = 0;
        for (; i < 240 * 10 && v.forwardSpeed() > 0.3f; i++) { v.step(t, in, Vehicle::DT); maxPitch = std::min(maxPitch, pitchDeg(v)); }
        float d = v.pos.x - x0;
        std::printf("  brake: 20 m/s -> 0 in %.1f m (%.2f g), nose dive %.1f deg\n", d, 400.f / (2 * d * 9.81f), -maxPitch);
    }
    // 3. Steady cornering: constant-radius-ish at increasing speed with half lock
    for (float sp : {8.f, 14.f, 20.f}) {
        fieldTerrain(t);
        Vehicle v; fresh(v, {0, 0, 0}, PI / 2);
        v.vel = v.forward() * sp;
        for (auto& w : v.wheels) w.omega = sp / w.radius;
        VehicleInput in; in.steer = 1.f;
        float latMax = 0, rollMax = 0, yawRate = 0;
        bool flipped = false;
        for (int i = 0; i < 240 * 4; i++) {
            // hold speed with throttle
            in.throttle = v.speed() < sp ? 0.6f : 0.f;
            v.step(t, in, Vehicle::DT);
            if (i > 240 * 2) {
                Vec3 vh{v.vel.x, 0, v.vel.z};
                float wy = v.angVel.y;
                latMax = std::max(latMax, std::fabs(wy * length(vh)) / 9.81f);
                rollMax = std::max(rollMax, std::fabs(rollDeg(v)));
                yawRate = wy;
            }
            if (v.up().y < 0.3f) flipped = true;
        }
        std::printf("  corner %4.0f mph full lock: lat %.2f g, roll %.1f deg, yaw %.2f rad/s, speed %.1f%s\n", sp * 2.237f, latMax,
                    rollMax, yawRate, v.speed() * 2.237f, flipped ? "  ** ROLLED **" : "");
    }
    // 4. Lane change (sinusoid steer) at 20 m/s
    {
        fieldTerrain(t);
        Vehicle v; fresh(v, {-80, 0, 0}, PI / 2);
        v.vel = v.forward() * 20.f;
        for (auto& w : v.wheels) w.omega = 20.f / w.radius;
        VehicleInput in;
        float rollMax = 0; bool flipped = false;
        for (int i = 0; i < 240 * 5; i++) {
            float tt = i * Vehicle::DT;
            in.steer = tt < 2.f ? std::sin(tt * PI) : 0.f;
            in.throttle = 0.4f;
            v.step(t, in, Vehicle::DT);
            rollMax = std::max(rollMax, std::fabs(rollDeg(v)));
            if (v.up().y < 0.3f) flipped = true;
        }
        Vec3 f = v.forward();
        std::printf("  lane change 45 mph: max roll %.1f deg, heading after %.0f deg, yaw rate left %.2f%s\n", rollMax,
                    std::atan2(f.z, f.x) / DEG, v.angVel.y, flipped ? "  ** ROLLED **" : "");
    }
    // 5. Drop test (1.5 m) - suspension absorption and settling
    {
        fieldTerrain(t);
        Vehicle v; fresh(v, {0, 0, 0}, PI / 2);
        float rest = v.pos.y;
        v.pos.y += 1.5f;
        v.vel = {};
        VehicleInput in;
        float minY = 1e9f, maxBelly = 0; int bounces = 0; float prevVy = 0, lastTurn = 1e9f;
        float settleT = -1;
        for (int i = 0; i < 240 * 5; i++) {
            v.step(t, in, Vehicle::DT);
            minY = std::min(minY, v.pos.y);
            maxBelly = std::max(maxBelly, v.bellyLoad);
            if (prevVy < 0 && v.vel.y >= 0 && i > 60) {   // a trough: count it if the swing was > 2 cm
                if (lastTurn - v.pos.y > 0.02f) bounces++;
            }
            if (prevVy > 0 && v.vel.y <= 0) lastTurn = v.pos.y;
            prevVy = v.vel.y;
            if (settleT < 0 && i > 240 && std::fabs(v.pos.y - rest) < 0.01f && std::fabs(v.vel.y) < 0.05f) settleT = i * Vehicle::DT;
        }
        std::printf("  drop 1.5 m: bottomed %.2f m below ride height, belly load peak %.2f, rebounds %d, settles in %.1f s\n",
                    rest - minY, maxBelly, bounces, settleT);
    }
    // 6. Bump/pitch: whoops (moderate and harsh) at 10 m/s
    for (float amp : {0.08f, 0.18f}) {
        fieldTerrain(t);
        for (int j = 0; j < Terrain::N; j++)
            for (int i = 0; i < Terrain::N; i++) {
                Vec3 w = t.cellToWorld(i, j);
                if (w.x > -40 && w.x < 40) { int k = t.idx(i, j); t.h[k] = amp * std::sin(w.x * 2 * PI / 5.f); t.base[k] = t.h[k] - 0.05f; }
            }
        Vehicle v; fresh(v, {-60, 0, 0}, PI / 2);
        v.vel = v.forward() * 10.f;
        for (auto& w : v.wheels) w.omega = 10.f / w.radius;
        VehicleInput in;
        float pmin = 0, pmax = 0, air = 0, flipX = 0; bool flipped = false;
        for (int i = 0; i < 240 * 8; i++) {
            in.throttle = v.speed() < 10.f ? 0.7f : 0.f;
            v.step(t, in, Vehicle::DT);
            if (v.pos.x > -35 && v.pos.x < 35) { pmin = std::min(pmin, pitchDeg(v)); pmax = std::max(pmax, pitchDeg(v)); air = std::max(air, v.airTime); }
            if (v.up().y < 0.3f && !flipped) { flipped = true; flipX = v.pos.x; }
        }
        std::printf("  whoops %2.0f cm @22 mph: pitch %.1f..%.1f deg, longest air %.2f s, exit speed %.1f mph", amp * 200.f, pmin, pmax, air,
                    v.speed() * 2.237f);
        if (flipped) std::printf("  ** FLIPPED at x=%.1f **", flipX);
        std::printf("\n");
    }
    // 7. Hill climb from standstill
    for (float deg : {20.f, 30.f, 35.f}) {
        fieldTerrain(t, deg);
        Vehicle v; fresh(v, {-30, 0, 0}, PI / 2);
        VehicleInput in; in.throttle = 1;
        for (int i = 0; i < 240 * 6; i++) v.step(t, in, Vehicle::DT);
        std::printf("  climb %.0f deg grass: %.1f m up the slope in 6 s, avg slip %.2f\n", deg, v.pos.x + 30, v.avgSlip);
    }
    // 8. Parked on a 15 deg side slope - lateral creep
    {
        fieldTerrain(t, 15, true);
        Vehicle v; fresh(v, {0, 0, 0}, PI / 2);
        Vec3 p0 = v.pos;
        VehicleInput in; in.handbrake = true;
        for (int i = 0; i < 240 * 10; i++) v.step(t, in, Vehicle::DT);
        std::printf("  parked on 15 deg side slope: drifted %.2f m in 10 s\n", length(Vec3{v.pos.x - p0.x, 0, v.pos.z - p0.z}));
    }
    // 9. Parked facing down a 15 deg slope, brake held
    {
        fieldTerrain(t, 15);
        Vehicle v; fresh(v, {0, 0, 0}, -PI / 2);
        Vec3 p0 = v.pos;
        VehicleInput in; in.handbrake = true;
        for (int i = 0; i < 240 * 6; i++) v.step(t, in, Vehicle::DT);
        std::printf("  handbrake held facing down a 15 deg slope: slid %.2f m\n", length(Vec3{v.pos.x - p0.x, 0, v.pos.z - p0.z}));
    }
}
static void debugClimb(const VehicleConfig& c, float deg) {
    static Terrain t;
    fieldTerrain(t, deg);
    Vehicle v; v.configure(c);
    Vec3 p{-30, 0, 0}; p.y = t.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.05f;
    v.reset(p, PI / 2);
    v.placeOnGround(t);
    VehicleInput in;
    in.handbrake = true;
    for (int i = 0; i < 240 * 3; i++) {
        if (i == 120) { in.throttle = 1; in.handbrake = false; }
        v.step(t, in, Vehicle::DT);
        if (i % 30 == 0)
            std::printf("t %.2f x %.2f vf %.2f gear %d rpm %.0f thr %.2f slip %.2f om %.1f/%.1f load %.0f/%.0f fx %.0f/%.0f contact %d\n", i * Vehicle::DT, v.pos.x,
                        v.forwardSpeed(), v.gear, v.engineRpm, v.throttleApplied, v.clutchSlip, v.wheels[0].omega, v.wheels[2].omega,
                        v.wheels[0].load, v.wheels[2].load, v.wheels[0].fx, v.wheels[2].fx, v.wheelsInContact);
    }
}
static void debugWhoops(const VehicleConfig& c) {
    static Terrain t;
    fieldTerrain(t);
    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            Vec3 w = t.cellToWorld(i, j);
            if (w.x > -40 && w.x < 40) { int k = t.idx(i, j); t.h[k] = 0.18f * std::sin(w.x * 2 * PI / 5.f); t.base[k] = t.h[k] - 0.05f; }
        }
    Vehicle v; v.configure(c);
    Vec3 p{-50, 0, 0}; p.y = t.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.05f;
    v.reset(p, PI / 2); settle(v, t);
    v.vel = v.forward() * 10.f;
    for (auto& w : v.wheels) w.omega = 10.f / w.radius;
    VehicleInput in;
    for (int i = 0; i < 240 * 4; i++) {
        in.throttle = v.speed() < 10.f ? 0.7f : 0.f;
        v.step(t, in, Vehicle::DT);
        if (i % 12 == 0)
            std::printf("x %6.2f vy %5.2f pitch %6.1f wz %5.2f comp %.2f %.2f %.2f %.2f load %5.0f %5.0f belly %.2f imp %.0f\n", v.pos.x, v.vel.y,
                        pitchDeg(v), dot(v.angVel, v.left()), v.wheels[0].compression, v.wheels[1].compression, v.wheels[2].compression,
                        v.wheels[3].compression, v.wheels[0].load, v.wheels[2].load, v.bellyLoad, v.events.impact);
    }
}
static void debugBog(const VehicleConfig& c) {
    static World w;
    w.generate(1976);
    ModeRunner m; m.id = ModeId::MudBog;
    Vehicle v; v.configure(c);
    float yaw; Vec3 p = m.spawnFor(w, 1, yaw);
    p.y = w.terrain.heightAt(p.x, p.z) - v.rideAxleY + v.wheels[0].radius + 0.2f;
    v.reset(p, yaw);
    VehicleInput in;
    float laneZ = w.laneZ(1);
    for (int i = 0; i < 240 * 30; i++) {
        Vec3 f = v.forward();
        in.steer = clampf(-(laneZ - v.pos.z) * 0.35f + std::atan2(f.z, f.x) * 1.6f, -1.f, 1.f);
        in.throttle = 1;
        v.step(w.terrain, in, Vehicle::DT);
        if (i % 4 == 0) w.terrain.relax(v.pos, 5, 4 * Vehicle::DT);
        if (i % 240 == 0)
            std::printf("t %4.0f x %6.1f v %.1f pen %.2f %.2f load %5.0f %5.0f contact %d belly %.2f slip %.2f mudW %.2f sub %.2f\n", i / 240.f,
                        v.pos.x - w.pitX0, v.speed(), v.wheels[0].tirePen, v.wheels[2].tirePen, v.wheels[0].load, v.wheels[2].load,
                        v.wheelsInContact, v.bellyLoad, v.avgSlip, v.wheels[2].mudW, v.submerged);
    }
}
static void debugLane(const VehicleConfig& c) {
    static Terrain t;
    fieldTerrain(t);
    Vehicle v; v.configure(c);
    Vec3 p{-80, 0, 0}; v.reset(p, PI / 2); v.placeOnGround(t); settle(v, t);
    v.vel = v.forward() * 20.f;
    for (auto& w : v.wheels) w.omega = 20.f / w.radius;
    VehicleInput in;
    for (int i = 0; i < 240 * 4; i++) {
        float tt = i * Vehicle::DT;
        in.steer = tt < 2.f ? std::sin(tt * PI) : 0.f;
        in.throttle = 0.4f;
        v.step(t, in, Vehicle::DT);
        if (i % 24 == 0) {
            Vec3 f = v.forward();
            float beta = std::atan2(dot(v.vel, v.left()), dot(v.vel, f)) / DEG;
            std::printf("t %.1f steer %5.2f applied %5.2f yawRate %5.2f heading %6.1f slip-angle %5.1f roll %5.1f fy %5.0f %5.0f %5.0f %5.0f load %5.0f %5.0f %5.0f %5.0f\n", tt, in.steer, v.steerApplied,
                        v.angVel.y, std::atan2(f.z, f.x) / DEG, beta, rollDeg(v), v.wheels[0].fy, v.wheels[1].fy, v.wheels[2].fy, v.wheels[3].fy,
                        v.wheels[0].load, v.wheels[1].load, v.wheels[2].load, v.wheels[3].load);
        }
    }
}
