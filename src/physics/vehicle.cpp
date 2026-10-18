#include "vehicle.h"
#include <cmath>
#include "../core/noise.h"
#include "terramechanics.h"

namespace mb {

namespace {
constexpr float G = 9.81f;
constexpr float IN = 0.0254f;
Rng g_rng(0xC0FFEEu);
}  // namespace
Tuning g_tuning;
namespace {

struct TreadCoeffs {
    float firm, lateral, lateralMud, grouser, dig, packRate, clearRate;
};
TreadCoeffs treadCoeffs(Tread t) {
    switch (t) {
        case Tread::AllTerrain: return {1.0f, 1.0f, 1.0f, 1.0f, 0.7f, 0.60f, 0.8f};
        case Tread::MudTerrain: return {0.93f, 0.92f, 0.9f, 1.6f, 1.0f, 0.35f, 1.4f};
        case Tread::Paddle: return {0.55f, 0.55f, 0.5f, 2.6f, 1.7f, 0.15f, 2.2f};
    }
    return {1, 1, 1, 1, 1, 0.5f, 1};
}

struct BellyPoint {
    Vec3 p;
    float weight;
};
}  // namespace

static std::vector<BellyPoint> g_bellyScratch;

void Vehicle::configure(const VehicleConfig& c) {
    cfg = c;
    body = &findBody(c.body);
    engine = &findEngine(c.engine);
    psi = c.psi;
    diffF = c.diffF;
    diffR = c.diffR;
    lockFront = diffF == Diff::Spool || (diffF == Diff::Locker && c.lockFront);
    lockRear = diffR == Diff::Spool || (diffR == Diff::Locker && c.lockRear);
    fourWD = c.fourWD;
    lowRange = false;
    finalDrive = c.axleRatio;

    const float r = c.tireIn * IN * 0.5f;
    const float wdt = c.widthIn * IN;
    const float tireMass = 22.f + (c.tireIn - 30.f) * 3.2f + (c.tread == Tread::Paddle ? 6.f : 0.f) + (c.widthIn - 10.5f) * 1.5f;
    mass = body->mass + engine->mass + 4 * tireMass;
    const float w = body->dims[0], h = body->dims[1], l = body->dims[2];
    inertia = {mass / 12.f * (h * h + l * l), mass / 12.f * (w * w + l * l), mass / 12.f * (w * w + h * h)};

    travel = body->travel + c.liftIn * IN * 0.35f;
    rideAxleY = body->axleY - c.liftIn * IN;
    const float sag = travel * 0.38f;
    // Unsprung corner: tire + wheel + brake + half a solid axle (independent arms on the UTV).
    const float unsprung = tireMass + (body->id == "utv" ? 14.f : 42.f);
    sprungMass = mass - 4.f * unsprung;
    springK = (sprungMass * G * 0.25f) / sag;
    damperC = 2.f * 0.42f * std::sqrt(springK * sprungMass * 0.25f);
    const float L = body->wheelbase, fb = body->frontBias;
    arbFront = 0.45f * springK;
    arbRear = 0.2f * springK;
    const float zf = L * (1.f - fb), zr = -L * fb;
    const float tx = body->track * 0.5f;
    const Vec3 mounts[4] = {{tx, 0, zf}, {-tx, 0, zf}, {tx, 0, zr}, {-tx, 0, zr}};
    for (int i = 0; i < 4; i++) {
        WheelState& ws = wheels[i];
        ws = WheelState{};
        ws.front = i < 2;
        ws.left = (i % 2) == 0;
        ws.radius = r;
        ws.width = wdt;
        ws.inertia = 0.75f * tireMass * r * r + 0.6f;  // tire+wheel+brake+axle shaft
        // Mount = top of travel. Wheel center = mount - susLen; static susLen = travel - sag puts
        // the wheel center at rideAxleY.
        ws.mount = {mounts[i].x, rideAxleY + (travel - sag), mounts[i].z};
        ws.susLen = travel - sag;
        ws.compression = ws.prevCompression = sag;
        // Per-axle rates from the static weight split, so the truck sits level at ride height.
        const float cornerW = sprungMass * G * 0.5f * (ws.front ? fb : 1.f - fb);
        ws.unsprung = unsprung;
        ws.nomLoad = cornerW + unsprung * G;
        ws.springK = cornerW / sag;
        const float crit = 2.f * std::sqrt(ws.springK * cornerW / G);
        ws.bumpC = 0.28f * crit;      // soft on compression so bumps pass through
        ws.reboundC = 0.8f * crit;    // firm on rebound so it doesn't pogo
    }
}

void Vehicle::reset(Vec3 p, float yaw) {
    pos = p;
    rot = Quat::axisAngle({0, 1, 0}, yaw);
    vel = angVel = {};
    for (auto& w : wheels) {
        w.omega = 0;
        w.pack = 0;
        w.susVel = 0;
        w.tirePen = w.prevTirePen = 0;
    }
    lastAccel = {};
    gear = 1;
    engineRpm = engine ? engine->idleRpm : 800;
    stallTimer = 0;
    fuelCut = false;
    airTime = 0;
    events.clear();
}

void Vehicle::repair() {
    flooded = engineBroken = beltBurnt = frontShaftBroken = false;
    floodDry = floodCrank = windup = brakeWet = 0;
    beltTemp = 60.f;
    stallTimer = 0;
    engineTemp = 85.f;
    radiatorClog = 0;
    for (auto& w : wheels) { w.debeaded = false; w.beadStress = 0; w.cake = 0; w.pack = 0; }
}

void Vehicle::setTransfer(int m) {
    fourWD = m >= 1;
    lowRange = m == 2;
}

void Vehicle::placeOnGround(const Terrain& t) {
    Vec3 f = forward();
    float yaw = std::atan2(f.x, f.z);
    Quat qy = Quat::axisAngle({0, 1, 0}, yaw);
    float h[4];
    for (int i = 0; i < 4; i++) {
        Vec3 m = pos + qy.rotate(Vec3{wheels[i].mount.x, 0, wheels[i].mount.z});
        h[i] = t.heightAt(m.x, m.z);
    }
    float pitch = std::atan2(0.5f * (h[0] + h[1]) - 0.5f * (h[2] + h[3]), body->wheelbase);
    float roll = std::atan2(0.5f * (h[0] + h[2]) - 0.5f * (h[1] + h[3]), body->track);
    rot = qy * Quat::axisAngle({1, 0, 0}, -pitch) * Quat::axisAngle({0, 0, 1}, roll);
    float avg = 0.25f * (h[0] + h[1] + h[2] + h[3]);
    pos.y = avg + (wheels[0].radius - rideAxleY) / std::max(0.5f, std::cos(pitch) * std::cos(roll)) + 0.03f;
}

float Vehicle::derate() const {
    float d = 1.f;
    if (engineTemp > 118.f) d *= lerpf(1.f, 0.45f, clampf((engineTemp - 118.f) / 14.f, 0, 1));
    return d;
}

void Vehicle::stepDrivetrain(float dt, const VehicleInput& in, float& totalTorque, float& reflI) {
    (void)in;
    const EngineSpec& e = *engine;
    totalTorque = 0;
    reflI = 0;
    const float lowMul = lowRange ? gearbox.lowRange : 1.f;
    float Gr = 0;
    if (gear > 0) Gr = gearbox.ratios[gear - 1] * finalDrive * lowMul;
    else if (gear < 0) Gr = -gearbox.reverse * finalDrive * lowMul;

    float sd = 0;
    int nDriven = 0;
    for (int i = 0; i < 4; i++)
        if (!wheels[i].front || (fourWD && !frontShaftBroken)) { sd += wheels[i].omega; nDriven++; }
    sd /= std::max(1, nDriven);

    const float thr = throttleApplied;
    const float rpmToW = 2.f * PI / 60.f;

    if (flooded || engineBroken) {   // hydrolocked or broken: no running engine
        engineRpm = approach(engineRpm, 0.f, 6.f, dt);
        clutchSlip = 1;
        return;
    }
    if (stallTimer > 0) {
        stallTimer -= dt;
        engineRpm = approach(engineRpm, 0.f, 4.f, dt);
        if (stallTimer <= 0) engineRpm = e.idleRpm;  // restarted
        clutchSlip = 1;
        return;
    }

    // Boost spool (turbo lags, roots blower is instant).
    float boostTarget = thr * clampf(engineRpm / e.redlineRpm * 1.4f, 0, 1);
    boost = approach(boost, boostTarget, e.supercharged ? 12.f : (thr > boost ? 1.4f : 3.f), dt);
    float boostMul = e.turbo ? lerpf(0.62f, 1.f, boost) : 1.f;

    // Rev limiter with hysteresis (fuel cut) - the classic mud-bog "bwap-bwap-bwap".
    if (engineRpm > e.redlineRpm && !fuelCut) { fuelCut = true; events.limiterHits++; if (g_rng.uniform() < 0.35f) events.backfires++; }
    if (fuelCut && engineRpm < e.redlineRpm - 260.f) fuelCut = false;

    float Te = fuelCut ? 0.f : thr * e.torqueAt(engineRpm) * derate() * boostMul * g_tuning.engineTorque;
    if (shiftTimer > 0) Te *= 0.25f;
    const float Tfric = (0.05f + 0.13f * engineRpm / e.redlineRpm) * e.peakTorque() * (1.f - 0.85f * thr);

    if (gear == 0) {
        float w = engineRpm * rpmToW;
        w += (Te - Tfric) / e.inertia * dt;
        engineRpm = std::max(e.idleRpm * (1.f - 0.15f * (1 - thr)), w / rpmToW);
        if (thr < 0.02f) engineRpm = approach(engineRpm, e.idleRpm, 3.f, dt);
        clutchSlip = 1;
        return;
    }

    // Turbine speed is signed: wheels dragged backwards against the selected gear (rolling back
    // on a climb) put the converter at stall - full torque multiplication, not lock-up.
    float rpmWheel = Gr * sd / rpmToW;
    float freeTarget = e.idleRpm + thr * (e.stallRpm - e.idleRpm);
    float target = std::max(rpmWheel, freeTarget);
    engineRpm = approach(engineRpm, target, rpmWheel >= freeTarget ? 80.f : 9.f, dt);
    float SR = clampf(rpmWheel / std::max(engineRpm, 1.f), 0.f, 1.f);
    clutchSlip = 1.f - SR;
    float TR = lerpf(2.1f, 1.0f, smoothstep(0.0f, 0.85f, SR));
    float coupling = SR * SR;
    const float eff = 0.86f;
    float dir = Gr > 0 ? 1.f : -1.f;
    totalTorque = std::fabs(Gr) * (dir * eff * Te * TR - signf(sd) * Tfric * coupling);
    if (isCvt()) {
        // Belt CVT: slow, heavy going in HIGH range slips and cooks the belt (field guide 7/11);
        // low range keeps the sheaves in a happy ratio.
        float work = clutchSlip * thr * (Te / std::max(e.peakTorque(), 1.f));
        float heat = work * (lowRange ? 4.f : 22.f);
        float cool = (beltTemp - 50.f) * 0.035f;
        beltTemp = clampf(beltTemp + (heat - cool) * dt, 20.f, 200.f);
        if (beltTemp > 165.f && !beltBurnt) { beltBurnt = true; events.beltBurnt = true; }
        if (beltBurnt) totalTorque = 0;
        else if (beltTemp > 130.f) totalTorque *= 0.6f;   // glazed belt slips
    }
    reflI = e.inertia * Gr * Gr * coupling / std::max(1, nDriven);
}

void Vehicle::step(Terrain& terrain, const VehicleInput& in, float dt) {
    const EngineSpec& e = *engine;
    const TreadCoeffs tc = treadCoeffs(cfg.tread);
    const float vf = forwardSpeed();

    // ---- input / gear selection ----
    float spd = speed();
    // Speed-sensitive lock: full lock for crawling, ~6 deg at highway speed (a real truck needs 2-3).
    float maxSteer = std::max(0.09f, 0.62f / (1.f + std::pow(spd / 8.f, 1.3f)));
    steerApplied = approach(steerApplied, in.steer * maxSteer, 7.f, dt);

    float drivePedal = in.throttle, brakePedal = in.brake;
    if (!manual) {
        if (gear >= 1 && vf < 0.6f && in.brake > 0.15f && in.throttle < 0.05f) { gear = -1; events.shifts++; }
        else if (gear == -1 && vf > -0.6f && in.throttle > 0.15f && in.brake < 0.05f) { gear = 1; events.shifts++; }
        else if (gear == 0 && (in.throttle > 0.1f || in.brake > 0.1f) && stallTimer <= 0) gear = in.brake > in.throttle ? -1 : 1;
        if (gear == -1) std::swap(drivePedal, brakePedal);
        if (gear >= 1 && shiftTimer <= 0 && clutchSlip < 0.12f) {
            int nG = int(gearbox.ratios.size());
            float upAt = lerpf(0.5f, 0.92f, drivePedal) * e.redlineRpm;
            if (gear < nG && engineRpm > upAt) { gear++; shiftTimer = 0.28f; events.shifts++; }
            else if (gear > 1) {
                float lowerRpm = engineRpm * gearbox.ratios[gear - 2] / gearbox.ratios[gear - 1];
                if (lowerRpm < lerpf(0.4f, 0.72f, drivePedal) * e.redlineRpm) { gear--; shiftTimer = 0.22f; events.shifts++; }
            }
        }
    } else {
        if (in.shiftUp && gear < int(gearbox.ratios.size())) { gear++; shiftTimer = 0.2f; events.shifts++; }
        if (in.shiftDown && gear > -1) { gear--; shiftTimer = 0.2f; events.shifts++; }
    }
    if (shiftTimer > 0) shiftTimer -= dt;
    float prevThr = throttleApplied;
    throttleApplied = approach(throttleApplied, drivePedal, 14.f, dt);
    if (prevThr > 0.7f && throttleApplied < 0.15f && engineRpm > e.redlineRpm * 0.55f && g_rng.uniform() < 0.25f) events.backfires++;

    if (in.airDelta != 0) psi = clampf(psi + in.airDelta * (in.airDelta > 0 ? 1.6f : 3.2f) * dt, 5.f, 35.f);

    float driveTorque, reflI;
    stepDrivetrain(dt, in, driveTorque, reflI);
    const bool fwd4 = fourWD && !frontShaftBroken;   // a broken front shaft leaves rear drive only
    int nDriven = fwd4 ? 4 : 2;
    float omega0[4];
    for (int i = 0; i < 4; i++) omega0[i] = wheels[i].omega;
    float mudSum = 0, scrubSum = 0;

    // ---- forces ----
    Vec3 F{0, -G * sprungMass, 0}, T{};
    const Vec3 U = up();
    const float cosT = std::max(0.25f, U.y);
    wheelsInContact = 0;
    onPad = false;
    float slipSum = 0;
    int slipN = 0;

    for (int i = 0; i < 4; i++) {
        WheelState& w = wheels[i];
        const Vec3 mountW = pos + rot.rotate(w.mount);
        const Vec3 probe = mountW - U * w.susLen;   // current wheel centre
        // Footprint sampling: the tire is a cylinder, not a point. Sample along the rolling
        // direction (accounting for wheel curvature) and across the tread; the highest support
        // wins. Lets tires straddle dug holes and climb rut walls.
        const float rs = w.radius;
        const float wsa = w.front ? steerApplied : 0.f;
        Vec3 fwH = rot.rotate({std::sin(wsa), 0, std::cos(wsa)});
        fwH = normalize(Vec3{fwH.x, 0, fwH.z});
        const Vec3 sdH{fwH.z, 0, -fwH.x};
        float gy = -1e9f;
        Vec3 support = probe;
        const float offs[5] = {-0.7f, -0.35f, 0.f, 0.35f, 0.7f};
        for (float o : offs) {
            float dx = o * rs;
            Vec3 sp = probe + fwH * dx;
            float hh = terrain.heightAt(sp.x, sp.z) - (rs - std::sqrt(rs * rs - dx * dx));
            if (hh > gy) { gy = hh; support = sp; }
        }
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            Vec3 sp = probe + sdH * (sgn * 0.38f * w.width);
            float hh = terrain.heightAt(sp.x, sp.z);
            if (hh > gy) { gy = hh; support = sp; }
        }
        // Traction boards under the footprint: a firm deck to climb onto.
        SupportPad* pad = nullptr;
        if (pads) {
            for (SupportPad& sp : *pads) {
                if (sp.wear >= 1.f) continue;
                Vec3 d = probe - sp.center;
                float a = d.x * sp.dir.x + d.z * sp.dir.z, b2 = d.x * sp.dir.z - d.z * sp.dir.x;
                float deck = sp.topAt(clampf(a, -sp.halfL, sp.halfL));
                if (std::fabs(a) < sp.halfL + 0.3f * rs && std::fabs(b2) < sp.halfW + 0.3f * w.width && probe.y - rs < deck + 0.25f) {
                    pad = &sp;
                    if (deck > gy) { gy = deck; support = {probe.x, deck, probe.z}; }
                }
            }
        }
        // ---- quarter car ----
        // Tire: spring between wheel centre and ground, stiffer with pressure; bottoms on the rim.
        const float dist = (mountW.y - gy) / cosT;           // mount to ground along the strut
        float pen = w.radius + w.susLen - dist;               // tire deflection
        const float kTire = 1.6e5f * std::pow(psi / 25.f, 0.85f) * (w.debeaded ? 0.2f : 1.f);
        const float sidewallH = std::max(0.08f, w.radius - 0.2f);
        // Ground that appears inside the wheel (rut walls, hole edges, berms) can't be absorbed by
        // the rubber: the wheel climbs it, pushing up the strut instead of exploding the tire force.
        const float maxPen = 0.6f * sidewallH;
        if (pen > maxPen) {
            w.susLen = std::max(0.f, w.susLen - (pen - maxPen));
            w.susVel = std::min(w.susVel, 0.f);
            pen = w.radius + w.susLen - dist;
        }
        float Ftire = 0;
        if (pen > 0) {
            float penVel = clampf((pen - w.prevTirePen) / dt, -8.f, 8.f);
            // Slop is a huge damper: a tire sitting in it doesn't hop like one on hardpan.
            TerrainSample gs = terrain.sample(support.x, support.z);
            float soft = smoothstep(0.02f, 0.15f, gs.mudDepth()) * smoothstep(0.35f, 0.7f, gs.moisture);
            float zeta = 0.08f + 0.9f * soft + (gs.waterDepth > 0.05f ? 0.3f : 0.f);
            Ftire = kTire * pen + 2.f * zeta * std::sqrt(kTire * w.unsprung) * penVel;
            float rimAt = 0.55f * sidewallH;
            if (pen > rimAt) Ftire += 12.f * kTire * (pen - rimAt) + 1500.f * std::max(0.f, penVel);
            Ftire = clampf(Ftire, 0.f, mass * G * 2.5f);
        }
        w.prevTirePen = std::max(0.f, pen);
        w.tirePen = std::max(0.f, pen);
        w.contact = pen > 0;
        // Suspension: spring + bump/rebound damper + progressive bump stop between body and wheel.
        float compression = travel - w.susLen;
        float compVel = -w.susVel;
        float Fs = w.springK * g_tuning.spring * compression +
                   (compVel > 0 ? w.bumpC : w.reboundC) * g_tuning.damper * compVel;
        // Hydraulic bump stop over the last 25% of travel: a progressive spring plus heavy damping,
        // so big hits are soaked up instead of being stored and fired back as a launch.
        const float stop = travel * 0.75f;
        if (compression > stop) {
            float x = (compression - stop) / (travel - stop);
            Fs += w.springK * travel * (1.5f * x + 6.f * x * x * x) +
                  (compVel > 0 ? w.bumpC * 7.f * x : w.reboundC * 0.8f * x) * compVel;
            if (compVel > 1.5f) {
                float imp = (compVel - 1.5f) * mass * 0.25f;
                if (imp > events.impact) { events.impact = imp; events.impactPos = probe; }
            }
        }
        // Anti-roll bar: resists left/right compression difference on the axle.
        {
            const WheelState& o = wheels[size_t(i ^ 1)];
            float diff = compression - (travel - o.susLen);
            Fs += (w.front ? arbFront : arbRear) * diff * 0.5f;
        }
        Fs = clampf(Fs, -mass * G * 0.5f, mass * G * 3.f);
        // Wheel moves along the strut relative to the chassis (semi-implicit Euler).
        {
            float aRel = (Fs - Ftire) / w.unsprung + dot(lastAccel, U);
            w.susVel += aRel * dt;
            w.susLen += w.susVel * dt;
            if (w.susLen < 0.f) { w.susLen = 0.f; w.susVel = std::max(w.susVel, 0.f); }
            if (w.susLen > travel) { w.susLen = travel; w.susVel = std::min(w.susVel, 0.f); }
        }
        w.worldCenter = mountW - U * w.susLen;
        compression = travel - w.susLen;
        w.prevCompression = w.compression;
        w.compression = compression;
        w.load = Ftire;
        {
            Vec3 Fsv = U * Fs;   // what the chassis feels through the strut
            // Mud-caked wheel out of balance: m r w^2 shake at wheel speed.
            if (w.cake > 0.02f) Fsv += U * (6.f * w.cake * 0.12f * w.omega * w.omega * std::sin(w.spin) * 0.5f);
            F += Fsv;
            T += cross(w.worldCenter - pos, Fsv);
        }

        // Steering (simple Ackermann: inside wheel turns more).
        float sa = 0;
        if (w.front) {
            float ack = 1.f + 0.12f * ((steerApplied > 0) == w.left ? 1.f : -1.f);
            sa = steerApplied * ack;
        }
        w.steer = sa;
        Vec3 fw = rot.rotate({std::sin(sa), 0, std::cos(sa)});

        if (!w.contact) {
            w.fx = w.fy = 0;
            w.mudW = 0;
            w.waterDepth = 0;
            // free spin
            float Iw = w.inertia + ((!w.front || fwd4) ? reflI : 0.f);
            float Td = (!w.front || fwd4) ? driveTorque / nDriven : 0.f;
            w.driveTorque = Td;
            w.omega += Td / Iw * dt;
            w.omega *= std::exp(-0.4f * dt);
            float Tb = brakePedal * (w.front ? 3400.f : 2400.f) * (1.f - 0.55f * brakeWet) + ((in.handbrake && !w.front) ? 3200.f : 0.f);
            float d = Tb / Iw * dt;
            w.omega = std::fabs(w.omega) <= d ? 0.f : w.omega - signf(w.omega) * d;
            w.spin += w.omega * dt;
            w.slipSpeed = 0;
            continue;
        }
        wheelsInContact++;

        Vec3 cp{support.x, gy, support.z};
        TerrainSample ts = terrain.sample(cp.x, cp.z);
        Vec3 n = terrain.normalAt(cp.x, cp.z);
        Vec3 f = normalize(fw - n * dot(fw, n));
        Vec3 lf = cross(n, f);
        Vec3 r = cp - pos;
        Vec3 vc = vel + cross(angVel, r);
        float vLong = dot(vc, f), vLat = dot(vc, lf);
        w.contactPos = cp;
        w.contactNormal = n;
        w.fwdDir = f;

        const float m = ts.moisture;
        const float mud = ts.mudDepth();
        const float mudW = pad ? 0.f : smoothstep(0.02f, 0.18f, mud) * smoothstep(0.3f, 0.65f, m);
        if (pad) onPad = true;
        w.mudW = mudW;
        w.moisture = m;
        w.waterDepth = ts.waterDepth;

        // Contact patch from tire deflection (air pressure matters).
        const float rr = w.radius;
        const float sidewall = std::max(0.08f, rr - 0.2f);
        const float defl = std::min(w.tirePen, 0.5f * sidewall);
        float dirSign = std::fabs(vLong) > 0.3f ? signf(vLong) : signf(w.omega + 1e-6f);
        float hAhead = terrain.heightAt(cp.x + f.x * rr * 0.75f * dirSign, cp.z + f.z * rr * 0.75f * dirSign);
        float wall = clampf(hAhead - gy, 0.f, rr);
        w.rutWall = wall;
        float Lc = 2.f * std::sqrt(std::max(1e-4f, 2.f * rr * defl - defl * defl)) +
                   std::sqrt(std::max(0.f, 2.f * rr * wall - wall * wall)) * mudW;
        float b = w.width * (1.f + clampf((20.f - psi) / 60.f, -0.1f, 0.25f));
        float A = b * Lc;
        float p = w.load / std::max(A, 1e-3f);

        // Terrain deformation (Bekker bearing + viscous sinkage rate).
        if (mud > 0.003f && w.load > 50.f)
            terrain.pressPatch({w.worldCenter.x, gy, w.worldCenter.z}, f, Lc * 0.5f, b * 0.5f, p, dt);

        // Shear / friction parameters
        const SoilState soil = soilState(m, ts.soil, ts.disturb);
        w.soil = ts.soil;
        const float kEq = soil.kEq, nExp = soil.n;
        const float coh = soil.coh * (1.f - 0.7f * w.pack);
        const float phi = soil.phi;
        const float tanEff = std::tan(phi) * (1.f - 0.5f * w.pack) * (1.f + 0.35f * (tc.grouser - 1.f));
        const float fmaxM = A * coh * tc.grouser + w.load * tanEff;
        const float Kshear = soil.K;
        const float wetness = clampf((m - 0.3f) / 0.7f, 0.f, 1.f);
        const float muFirm = pad ? 0.95f : g_tuning.firmGrip * lerpf(0.8f, 0.62f, ts.grass) * (1.f - 0.38f * wetness) * tc.firm *
                             (1.f - 0.3f * clampf(ts.waterDepth / 0.1f, 0.f, 1.f));
        const float psiLat = 1.f - 0.25f * clampf(1.f - psi / 25.f, 0.f, 1.f);
        // Load sensitivity: friction coefficient falls as a tire is loaded up, so weight transfer
        // in a turn costs grip and roll-stiffness balance shapes understeer/oversteer.
        const float muLoad = clampf(1.f - 0.22f * (w.load / std::max(w.nomLoad, 1.f) - 1.f), 0.65f, 1.15f);

        auto tireForces = [&](float omega, float& Fx, float& Fy) {
            float wr = omega * rr;
            float slip = wr - vLong;
            float kappa = clampf(slip / std::max(std::fabs(vLong), 2.5f), -6.f, 6.f);
            float alpha = std::atan2(vLat, std::max(std::fabs(vLong), 2.0f));
            float iMud = clampf(slip / std::max(std::max(std::fabs(wr), std::fabs(vLong)), 0.5f), -1.f, 1.f);
            float fxF = muFirm * muLoad * w.load * magicFormula(kappa, 10.f, 1.65f, 1.f, 0.97f);
            float fyF = -muFirm * muLoad * w.load * magicFormula(alpha, 9.f, 1.35f, 1.f, -0.5f) * tc.lateral * psiLat;
            float fxM = janosiHanamoto(A, w.load, coh * tc.grouser, std::atan(tanEff), iMud, Lc, Kshear);
            float fyM = -signf(vLat) * fmaxM * 0.8f * tc.lateralMud *
                        (1.f - std::exp(-std::fabs(std::tan(alpha)) * Lc / (1.5f * Kshear)));
            Fx = lerpf(fxF, fxM, mudW);
            Fy = lerpf(fyF, fyM, mudW);
            if (w.debeaded) { Fx *= 0.6f; Fy *= 0.4f; }
            // Static friction at crawl speeds: a parked tire grips sideways instead of creeping.
            float crawl = 1.f - clampf(std::fabs(vLong) / 1.5f, 0.f, 1.f);
            Fy -= crawl * vLat * w.load / G * 10.f;
            float fmax = lerpf(muFirm * muLoad * w.load * 1.05f, fmaxM, mudW);
            float mag = std::sqrt(Fx * Fx + Fy * Fy);
            if (mag > fmax && mag > 0) { Fx *= fmax / mag; Fy *= fmax / mag; }
        };

        // Paddle-wheel propulsion: in soup and standing water the lugs throw slurry backwards.
        // Thrust ~ 0.5 rho Cd A (v_slip)|v_slip| on the buried part of the tread, strongest for paddles.
        const float paddleK = cfg.tread == Tread::Paddle ? 0.25f : 0.f;   // only scoops make real thrust
        const float buriedP = std::min(std::max(wall + 0.5f * w.tirePen, 0.f) * mudW + std::min(ts.waterDepth, rr), 1.2f * rr);
        const float paddleA = w.width * buriedP * paddleK * g_tuning.digRate;
        auto paddleThrust = [&](float omega) {
            float vs = clampf(omega * rr - vLong, -12.f, 12.f);
            return 0.5f * MudModel::slurryDensity * 1.1f * paddleA * vs * std::fabs(vs);
        };
        float Fx, Fy;
        tireForces(w.omega, Fx, Fy);
        Fx += paddleThrust(w.omega);
        const float eps = 0.05f;
        float Fx2, Fy2;
        tireForces(w.omega + eps, Fx2, Fy2);
        Fx2 += paddleThrust(w.omega + eps);
        float dFdw = std::max(0.f, (Fx2 - Fx) / eps);

        // Resistances: compaction (Bekker), bulldozing (Rankine), suction, rolling.
        float zc = std::min(wall, rr) * 0.6f;
        float Rc = compactionResistance(b, kEq, nExp, zc);
        float Rb = rankineBulldozing(b, wall, MudModel::unitWeight, coh, phi);
        float Rs = g_tuning.suction * soil.sticky * A * 3500.f * clampf(wall / (rr * 0.5f), 0.f, 1.f);
        float Rroll = w.load * (0.015f + 0.01f * clampf((15.f - psi) / 10.f, 0.f, 1.f));
        // Slurry / water drag on the buried part of the tire: 0.5 rho Cd A v^2.
        float buried = std::min(wall + 0.5f * defl, 2.f * rr) * mudW;
        float Rd = 0.5f * MudModel::slurryDensity * 0.45f * b * buried * vLong * vLong +
                   0.5f * 1000.f * 0.3f * b * std::min(ts.waterDepth, rr) * vLong * vLong;
        float R = (Rc + Rb + Rs) * mudW + Rroll + Rd;

        float FxNet;
        if (std::fabs(vLong) > 0.25f) FxNet = Fx - signf(vLong) * R;
        else {
            FxNet = Fx - clampf(Fx, -R, R);
            FxNet -= vLong * w.load / G * 6.f;  // static hold when traction < resistance
        }

        Vec3 Ft = f * FxNet + lf * Fy;
        F += Ft;
        T += cross(r, Ft);
        (void)0;
        w.fx = Fx;
        w.fy = Fy;

        // Wheel spin, semi-implicit in tire slip stiffness.
        bool driven = !w.front || fwd4;
        float Iw = w.inertia + (driven ? reflI : 0.f);
        float Td = driven ? driveTorque / nDriven : 0.f;
        w.driveTorque = Td;
        // Churning slurry resists a spinning tire (paddle-wheel drag ~ omega^2).
        float churnK = 0.45f * g_tuning.churn * (cfg.tread == Tread::Paddle ? 1.6f : 1.f) * mudW * std::min(wall + defl + 0.1f, 2.f * rr);
        float Tchurn = churnK * w.omega * std::fabs(w.omega);
        w.omega += dt * (Td - Fx * rr - Tchurn) / (Iw + dt * (rr * dFdw + 2.f * churnK * std::fabs(w.omega)));
        float Tb = brakePedal * (w.front ? 3400.f : 2400.f) * (1.f - 0.55f * brakeWet) + ((in.handbrake && !w.front) ? 3200.f : 0.f);
        // Brake traction control: brake a wheel spinning away from its axle partner so the open
        // diff sends torque across (field guide 5). Useless once the axle is locked.
        if (cfg.btc && driven && drivePedal > 0.2f && !locked(w.front)) {
            float over = (std::fabs(w.omega) - std::fabs(omega0[i ^ 1])) * rr;
            if (over > 1.2f) Tb += std::min(1800.f, 700.f * (over - 1.2f));
        }
        w.brakeT = Tb;
        float dB = Tb / Iw * dt;
        w.omega = std::fabs(w.omega) <= dB ? 0.f : w.omega - signf(w.omega) * dB;
        w.spin += w.omega * dt;

        float wr = w.omega * rr;
        w.slipSpeed = wr - vLong;
        w.slipRatio = w.slipSpeed / std::max(std::max(std::fabs(wr), std::fabs(vLong)), 0.5f);
        w.slipAngle = std::atan2(vLat, std::max(std::fabs(vLong), 0.5f));
        if (driven) { slipSum += std::fabs(w.slipRatio); slipN++; }

        // Slip excavation: spinning tires dig (and throw) mud.
        float ss = std::fabs(w.slipSpeed);
        if (pad && ss > 3.f) pad->wear = std::min(1.f, pad->wear + dt * 0.12f * (ss - 3.f));
        if (mud > 0.01f && ss > 1.0f) {
            float loadFrac = clampf(w.load / (mass * G * 0.25f), 0.f, 1.3f);  // unloaded tires can't dig
            // Lugs can only carry so much slop per revolution: saturate with slip speed.
            float depth = 0.006f * g_tuning.digRate * tc.dig * (0.35f + 0.65f * m) * std::min(ss - 1.f, 8.f) * loadFrac * dt;
            Vec3 dc{w.worldCenter.x, gy, w.worldCenter.z};
            w.ejectVolume += terrain.digPatch(dc, f, std::max(Lc * 0.5f, rs * 0.45f), b * 0.55f, depth);
        }
        // Vegetation wear and tire tracks.
        if (ts.grass > 0.02f && (ss > 2.f || std::fabs(vLat) > 2.f))
            terrain.scuff(cp, b * 0.6f, 0.25f * dt * std::max(ss, std::fabs(vLat)), 0.f);
        if (std::fabs(vLong) > 0.3f && (m > 0.35f || ts.grass < 0.6f)) terrain.scuff(cp, b * 0.45f, 0.f, dt * 1.2f, std::atan2(f.z, f.x));

        // Tread packing: sticky clay loads the lugs; wheel speed flings it out.
        float ag = w.omega * w.omega * rr / G;
        w.pack += dt * soil.sticky * mudW * tc.packRate * clampf(std::fabs(wr) / 3.f, 0.f, 1.f) * 0.6f;
        w.pack -= dt * std::max(0.f, ag - 14.f) / 18.f * tc.clearRate;
        if (ts.waterDepth > 0.12f) w.pack -= dt * 0.35f;
        w.pack = clampf(w.pack, 0.f, 1.f);
        // Mud caked inside the wheel (shaken out only by water or a wash) throws it out of balance.
        w.cake = clampf(w.cake + dt * 0.006f * mudW * clampf(ss, 0.f, 4.f) - (ts.waterDepth > 0.25f ? dt * 0.08f : 0.f), 0.f, 1.f);
        // Soaked brakes after a crossing; they dry with use.
        if (ts.waterDepth > 0.3f * rr) brakeWet = 1.f;
        // Bead retention: very low pressure without beadlocks + big side load rolls the tire off the rim.
        if (!cfg.beadlock && psi < 8.f && !w.debeaded) {
            float side = std::fabs(Fy) / std::max(w.load, 100.f);
            w.beadStress += dt * std::max(0.f, side - 0.4f) * 3.f * (8.f - psi) / 3.f;
            if (w.beadStress > 1.f) { w.debeaded = true; events.beadPopped = true; }
        }
        mudSum += mudW;
        if (fwd4) scrubSum += (1.f - mudW) * (1.f - clampf(std::fabs(w.slipRatio) * 2.f, 0.f, 1.f));
        dirt = clampf(dirt + dt * 0.004f * ss * mudW, 0.f, 1.f);
    }
    avgSlip = slipN ? slipSum / slipN : 0.f;

    // ---- differential / transfer-case constraints ----
    if (fwd4) {
        WheelState &a = wheels[0], &b2 = wheels[1], &c = wheels[2], &d = wheels[3];
        float If = a.inertia + b2.inertia, Ir = c.inertia + d.inertia;
        float sF = 0.5f * (a.omega + b2.omega), sR = 0.5f * (c.omega + d.omega);
        float s = (If * sF + Ir * sR) / (If + Ir);
        a.omega += s - sF; b2.omega += s - sF;
        c.omega += s - sR; d.omega += s - sR;
    }
    auto lockAxle = [](WheelState& l, WheelState& r) {
        float avg = (l.omega * l.inertia + r.omega * r.inertia) / (l.inertia + r.inertia);
        l.omega = r.omega = avg;
    };
    if (lockRear) lockAxle(wheels[2], wheels[3]);
    if (lockFront && fwd4) lockAxle(wheels[0], wheels[1]);
    // Limited-slip: clutches resist part of the speed difference across the axle.
    auto lsd = [](WheelState& l, WheelState& r) {
        float avg = (l.omega * l.inertia + r.omega * r.inertia) / (l.inertia + r.inertia);
        l.omega += (avg - l.omega) * 0.12f;
        r.omega += (avg - r.omega) * 0.12f;
    };
    if (diffR == Diff::Limited && !lockRear) lsd(wheels[2], wheels[3]);
    if (diffF == Diff::Limited && !lockFront && fwd4) lsd(wheels[0], wheels[1]);
    // Part-time 4WD windup: no centre diff, so turning on high-grip ground fights the driveline.
    {
        float bind = fwd4 ? clampf(std::fabs(steerApplied) / 0.62f, 0.f, 1.f) * clampf(spd / 2.f, 0.f, 1.f) * (scrubSum / 4.f) : 0.f;
        windup = clampf(windup + dt * (bind * 0.35f - 0.1f), 0.f, 1.2f);
        if (windup >= 1.f && !frontShaftBroken) { frontShaftBroken = true; windup = 0; events.shaftBroke = true; }
    }
    brakeWet = std::max(0.f, brakeWet - dt * (0.04f + 0.3f * brakePedal * clampf(spd / 5.f, 0.f, 1.f)));

    // ---- chassis / belly contacts (high-centering) ----
    const float bw = body->dims[0], bl = body->dims[2];
    g_bellyScratch.clear();
    g_bellyScratch.push_back({{bw * 0.38f, body->bellyY, bl * 0.38f}, 1});
    g_bellyScratch.push_back({{-bw * 0.38f, body->bellyY, bl * 0.38f}, 1});
    g_bellyScratch.push_back({{bw * 0.38f, body->bellyY, -bl * 0.38f}, 1});
    g_bellyScratch.push_back({{-bw * 0.38f, body->bellyY, -bl * 0.38f}, 1});
    g_bellyScratch.push_back({{0, body->bellyY, 0}, 1.5f});
    g_bellyScratch.push_back({{0, body->bellyY + 0.25f, bl * 0.5f}, 0.8f});
    g_bellyScratch.push_back({{0, body->bellyY + 0.25f, -bl * 0.5f}, 0.8f});
    std::vector<Vec3> worldPts;
    if (body->id != "utv") {  // solid-axle differential pumpkins hang below the wheel centers
        for (int ax = 0; ax < 2; ax++) {
            Vec3 c = (wheels[ax * 2].worldCenter + wheels[ax * 2 + 1].worldCenter) * 0.5f - U * 0.17f;
            worldPts.push_back(c);
        }
    }
    for (auto& bp : g_bellyScratch) worldPts.push_back(pos + rot.rotate(bp.p));

    float bellyN = 0, scrape = 0;
    for (const Vec3& pw : worldPts) {
        float th = terrain.heightAt(pw.x, pw.z);
        float pen = th - pw.y;
        if (pen <= 0) continue;
        TerrainSample ts = terrain.sample(pw.x, pw.z);
        float soft = smoothstep(0.03f, 0.25f, ts.mudDepth()) * smoothstep(0.4f, 0.75f, ts.moisture);
        Vec3 n = terrain.normalAt(pw.x, pw.z);
        Vec3 vp = vel + cross(angVel, pw - pos);
        float vn = dot(vp, n);
        float k = lerpf(1.6e5f, 3.0e4f, soft), c = lerpf(9000.f, 5000.f, soft);
        float Fn = std::max(0.f, k * pen - c * vn);
        float suction = 0;
        if (soft > 0.2f && vn > 0.f) suction = -g_tuning.suction * std::min(9000.f * soft * (0.5f + MudModel::stickiness(ts.moisture)), 6000.f * vn);
        Vec3 vt = vp - n * vn;
        float vtl = length(vt);
        Vec3 Fc = n * (Fn + suction);
        if (vtl > 1e-3f) {
            float mu = lerpf(0.5f, 0.85f, soft);
            float plow = soft * std::min(pen, 0.4f) * 9000.f * std::min(vtl, 3.f);
            float ft = std::min(mu * Fn + plow, mass * 0.04f * vtl / dt);
            Fc -= vt * (ft / vtl);
        }
        F += Fc;
        T += cross(pw - pos, Fc);
        bellyN += Fn;
        scrape = std::max(scrape, clampf(vtl / 4.f, 0.f, 1.f) * clampf(Fn / (mass * G * 0.2f), 0.f, 1.f));
        float imp = -vn * mass * 0.15f;
        if (imp > events.impact && Fn > mass * G * 1.5f) { events.impact = imp; events.impactPos = pw; }
    }
    bellyLoad = clampf(bellyN / (mass * G), 0.f, 2.f);
    // A high-centered chassis is held by the slop: extra angular damping.
    if (bellyLoad > 0.2f) angVel *= std::exp(-1.5f * bellyLoad * dt);
    events.bellyScrape = std::max(events.bellyScrape, scrape);

    // External pull (winch line).
    if (dot(extForce, extForce) > 1.f) {
        F += extForce;
        T += cross(extPoint - pos, extForce);
    }
    // ---- aero, water, buoyancy ----
    F += vel * (-0.5f * 1.2f * body->cdA * spd);
    TerrainSample here = terrain.sample(pos.x, pos.z);
    float waterSurf = here.waterDepth > 0 ? here.height + here.waterDepth : -1e9f;
    float bellyWorld = pos.y + body->bellyY;
    float prevSub = submerged;
    submerged = clampf(waterSurf - bellyWorld, 0.f, body->dims[1]);
    if (submerged > 0) {
        Vec3 vh{vel.x, 0, vel.z};
        float vhl = length(vh);
        float Aw = bw * submerged;
        F += vh * (-0.5f * 1000.f * 0.85f * Aw * vhl);
        F.y += 1000.f * G * bw * bl * submerged * 0.22f;
        angVel *= std::exp(-0.6f * dt);
        if (prevSub <= 0.02f && spd > 2.f) { events.splash = std::max(events.splash, spd * 0.15f); events.splashPos = pos; }
        events.splash = std::max(events.splash, submerged * vhl * 0.08f);
        events.splashPos = pos + forward() * (bl * 0.5f);
        radiatorClog = std::max(0.f, radiatorClog - 0.15f * dt * clampf(submerged / 0.4f, 0, 1));
        dirt = std::max(0.f, dirt - 0.01f * dt * submerged);
    }

    // Radiator clogging when the nose plows mud.
    Vec3 nose = pos + forward() * (bl * 0.5f) + U * (body->bellyY + 0.3f);
    TerrainSample tn = terrain.sample(nose.x, nose.z);
    if (tn.height > nose.y - 0.1f && tn.mudDepth() > 0.05f && spd > 1.f)
        radiatorClog = std::min(1.f, radiatorClog + 0.035f * spd * dt * tn.moisture);
    radiatorClog = std::max(0.f, radiatorClog - 0.002f * dt);

    // Engine thermals: heat in from load, out through the (possibly clogged) radiator.
    float load = throttleApplied * (engineRpm / e.redlineRpm);
    float heatIn = 4.2f * load + 0.35f;
    float airflow = 0.6f + 0.4f * clampf(spd / 12.f, 0.f, 1.f);
    float cool = (engineTemp - 70.f) * 0.11f * airflow * (1.f - 0.82f * radiatorClog);
    engineTemp = clampf(engineTemp + (heatIn - cool) * dt, 20.f, 140.f);

    // Hydrolock: water (or slop) over the air intake kills the engine.
    float intakeY = cfg.snorkel ? body->dims[1] * 0.5f + 0.45f : body->intakeY;
    Vec3 intake = pos + rot.rotate({0, intakeY, body->intakeZ});
    TerrainSample ti = terrain.sample(intake.x, intake.z);
    // Only liquid kills it: standing water, or soupy slurry. A nose buried in dry dirt doesn't.
    bool liquid = ti.waterDepth > 0.02f || (ti.moisture > 0.8f && ti.mudDepth() > 0.15f);
    float slop = ti.waterDepth > 0 ? ti.height + ti.waterDepth : ti.height;
    bool intakeWet = liquid && slop > intake.y;
    if (!flooded && !engineBroken && intakeWet && engineRpm > 200.f) {
        flooded = true;
        floodDry = floodCrank = 0;
        events.hydrolocked = true;
    }
    if (flooded) {
        // Water in a cylinder: cranking it while still swamped bends a rod (field guide 8). Get it
        // out of the water first, wait, then it will clear and restart.
        if (intakeWet) {
            floodDry = 0;
            floodCrank = in.throttle > 0.5f ? floodCrank + dt : std::max(0.f, floodCrank - dt);
            if (floodCrank > 1.2f) { flooded = false; engineBroken = true; events.rodBent = true; }
        } else {
            floodDry += dt;
            if (floodDry > 3.f && in.throttle > 0.5f) { flooded = false; engineRpm = e.idleRpm; events.restarted = true; }
        }
    }

    // ---- integrate ----
    lastAccel = (F + Vec3{0, G * sprungMass, 0}) * (1.f / sprungMass);
    vel += F * (dt / sprungMass);
    pos += vel * dt;
    Vec3 tb = rot.unrotate(T);
    Vec3 wb = rot.unrotate(angVel);
    Vec3 Iw{inertia.x * wb.x, inertia.y * wb.y, inertia.z * wb.z};
    Vec3 gyro = cross(wb, Iw);
    Vec3 dwb{(tb.x - gyro.x) / inertia.x, (tb.y - gyro.y) / inertia.y, (tb.z - gyro.z) / inertia.z};
    angVel += rot.rotate(dwb) * dt;
    angVel *= std::exp(-0.08f * dt);
    // Sim-cade air assist: with all four wheels off the ground, gently bleed pitch/roll rate (not
    // yaw) so a hard launch lands on its wheels more often than its roof.
    if (wheelsInContact == 0) {
        Vec3 a = rot.unrotate(angVel);
        float k = std::exp(-0.9f * dt);
        a.x *= k; a.z *= k;
        angVel = rot.rotate(a);
    }
    rot = rot.integrated(angVel, dt);

    // Safety: never fall through the world.
    float minY = terrain.baseAt(pos.x, pos.z) - 2.f;
    if (pos.y < minY) { pos.y = minY + 2.5f; vel = {}; }

    airTime = wheelsInContact == 0 ? airTime + dt : 0.f;
}

}  // namespace mb
