#include "world.h"
#include <cmath>
#include "../core/noise.h"

namespace mb {

Vec3 FencePost::top() const {
    const float h = 1.15f;
    return base + Vec3{leanDir.x * std::sin(lean), std::cos(lean), leanDir.z * std::sin(lean)} * h;
}

namespace {
float distToSegment(Vec2 p, Vec2 a, Vec2 b, float* tOut = nullptr) {
    Vec2 ab = b - a;
    float t = clampf(dot(p - a, ab) / std::max(1e-6f, dot(ab, ab)), 0.f, 1.f);
    if (tOut) *tOut = t;
    return length(p - (a + ab * t));
}
float gauss(float d2, float r) { return std::exp(-d2 / (r * r)); }
}  // namespace

void World::generate(uint32_t seed) {
    Rng rng(seed * 7919u + 13);
    Terrain& T = terrain;
    props.clear(); barrels.clear(); posts.clear(); fenceSegs.clear(); colliders.clear();

    circuit = {{-60, 0, 8},  {-20, 0, 10}, {25, 0, 12}, {62, 0, 18}, {80, 0, 42}, {66, 0, 70}, {35, 0, 80},
               {5, 0, 62},   {0, 0, 32},   {-30, 0, 40}, {-50, 0, 52}, {-74, 0, 38}, {-76, 0, 18}};

    auto natural = [&](float x, float z) { return (fbm(x * 0.011f + 3.1f, z * 0.011f - 7.4f, 4, seed) - 0.5f) * 1.8f; };
    const float hPit = natural(0, pitZ), hBowl = natural(bowlCenter.x, bowlCenter.z);
    const float hPond = natural(pondCenter.x, pondCenter.z);
    Vec2 pools[3] = {{-12, pitZ + 4}, {9, pitZ - 5}, {24, pitZ + 2}};

    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            const int k = T.idx(i, j);
            const float x = T.originX() + i * Terrain::CELL, z = T.originZ() + j * Terrain::CELL;
            float h = natural(x, z);
            float n1 = fbm(x * 0.15f, z * 0.15f, 3, seed + 5), n2 = fbm(x * 0.6f, z * 0.6f, 2, seed + 9);
            h += (n2 - 0.5f) * 0.05f;
            float moist = 0.22f + 0.18f * smoothstep(0.55f, 0.8f, n1);
            float grass = 0.78f + 0.2f * n1;
            float base = h - 0.07f;
            float water = Terrain::NO_WATER;

            // Staging + pit apron graded flat.
            float dzP = std::fabs(z - pitZ);
            float flatW = (1.f - smoothstep(24.f, 34.f, dzP)) * (1.f - smoothstep(64.f, 76.f, std::fabs(x)));
            h = lerpf(h, hPit + (n2 - 0.5f) * 0.04f, flatW);
            base = lerpf(base, hPit - 0.07f, flatW);

            // Excavated drag pit with entry/exit ramps and soupy slurry over hardpan.
            float tx = smoothstep(pitX0 - 7.f, pitX0, x) * (1.f - smoothstep(pitX1, pitX1 + 7.f, x));
            float tz = 1.f - smoothstep(pitHalfW - 1.5f, pitHalfW + 0.5f, dzP);
            float tp = tx * tz;
            if (tp > 0) {
                float floor = hPit - 0.42f + (n1 - 0.5f) * 0.16f;
                for (auto& pl : pools) floor -= 0.38f * gauss(sq(x - pl.x) + sq(z - pl.y), 4.f);
                h = lerpf(h, floor, tp);
                base = lerpf(base, hPit - 1.3f + (n2 - 0.5f) * 0.1f, tp);
                moist = lerpf(moist, 0.86f + 0.14f * n1, tp);
                grass = lerpf(grass, 0.f, std::min(1.f, tp * 2));
                if (tp > 0.6f) water = hPit - 0.53f;
            }
            // Spoil berms along both pit sides.
            if (x > pitX0 - 4 && x < pitX1 + 4) {
                float b = dzP - (pitHalfW + 3.2f);
                float berm = 0.85f * std::exp(-b * b / 3.f) * (0.8f + 0.4f * n1);
                h += berm;
                base += berm * 0.9f;
                grass = std::max(grass * 0.5f, grass - berm);
            }

            // Pond / water crossing on the circuit (photo 4 standing water).
            float dPond = length(Vec2{x - pondCenter.x, z - pondCenter.z});
            if (dPond < pondRadius + 4) {
                float t = 1.f - smoothstep(pondRadius - 2, pondRadius + 4, dPond);
                float floor = hPond - 0.95f * std::max(0.f, 1.f - sq(dPond / pondRadius));
                h = lerpf(h, floor, t);
                base = std::min(base, floor - 0.45f);
                moist = std::max(moist, lerpf(0.5f, 1.f, t));
                grass *= 1.f - t;
                if (dPond < pondRadius + 1) water = hPond - 0.18f;
            }

            // Freestyle bowl + King of the Pit puddle.
            float dB = length(Vec2{x - bowlCenter.x, z - bowlCenter.z});
            if (dB < bowlRadius + 7) {
                float t = 1.f - smoothstep(bowlRadius, bowlRadius + 3, dB);
                float floor = hBowl - 0.5f * std::max(0.f, 1.f - sq(dB / bowlRadius)) -
                              0.35f * std::max(0.f, 1.f - sq(dB / kingRadius)) + (n1 - 0.5f) * 0.12f;
                h = lerpf(h, floor, t);
                base = lerpf(base, hBowl - 1.05f, t);
                moist = lerpf(moist, 0.78f + 0.2f * n1, t);
                grass *= 1.f - t;
                float rim = dB - (bowlRadius + 3.5f);
                h += 0.7f * std::exp(-rim * rim / 2.5f);
                if (dB < kingRadius + 2) water = hBowl - 0.66f;
            }

            // Spoil mound with brush pile and the bus (photos 1-2).
            float dM2 = sq(x - busPos.x - 6) + sq(z - busPos.z - 4);
            float mound = 3.2f * gauss(dM2, 11.f);
            h += mound;
            base += mound;
            if (mound > 0.3f) grass = std::min(grass, 0.6f + 0.2f * n2);

            // Mowed two-track circuit.
            Vec2 p{x, z};
            float best = 1e9f, along = 0;
            int seg = -1;
            for (size_t s = 0; s < circuit.size(); s++) {
                const Vec3 &a = circuit[s], &b = circuit[(s + 1) % circuit.size()];
                float t;
                float d = distToSegment(p, {a.x, a.z}, {b.x, b.z}, &t);
                if (d < best) { best = d; seg = int(s); along = t * length(Vec2{b.x - a.x, b.z - a.z}); }
            }
            if (best < 3.2f && dPond > pondRadius + 1) {
                float t = 1.f - smoothstep(2.4f, 3.2f, best);
                grass = lerpf(grass, 0.32f, t);
                moist = std::max(moist, 0.32f * t);
                float rut = std::exp(-sq(best - 0.78f) / 0.04f);
                h -= 0.07f * rut * t;
                grass -= 0.25f * rut * t;
                if (seg == 3) h += 0.32f * std::sin(along * 2 * PI / 6.5f) * t;  // whoops
            }

            T.h[k] = h;
            T.base[k] = std::min(base, h - 0.02f);
            T.moist[k] = clampf(moist, 0.f, 1.f);
            T.grass[k] = uint8_t(clampf(grass, 0.f, 1.f) * 255.f);
            T.water[k] = water;
            T.comp[k] = moist > 0.6f ? 0.01f : 0.08f;
            T.track[k] = 0;
        }

    auto ground = [&](float x, float z) { return T.heightAt(x, z); };
    auto addProp = [&](const std::string& part, float x, float z, float yaw, float scale = 1, CadParams pr = {}) {
        props.push_back({part, pr, {x, ground(x, z), z}, yaw, scale});
    };

    // ---- Orange safety fence on T-posts along the pit (some already flattened, photos 5-6) ----
    auto fenceLine = [&](Vec3 a, Vec3 b, float spacing) {
        int n = std::max(1, int(length(b - a) / spacing));
        int first = int(posts.size());
        for (int s = 0; s <= n; s++) {
            Vec3 p = lerp(a, b, float(s) / n);
            FencePost fp;
            fp.base = {p.x, ground(p.x, p.z), p.z};
            if (rng.uniform() < 0.12f) {
                fp.lean = rng.range(0.4f, 1.3f);
                float ang = rng.range(0, 2 * PI);
                fp.leanDir = {std::cos(ang), 0, std::sin(ang)};
            }
            posts.push_back(fp);
            if (s > 0) fenceSegs.push_back({first + s - 1, first + s});
            if (rng.uniform() < 0.35f)
                addProp("vegetation/teasel.mbcad", p.x + rng.range(-0.8f, 0.8f), p.z + rng.range(0.3f, 1.2f), rng.range(0, 6.28f),
                        rng.range(0.8f, 1.2f));
        }
    };
    float fz = pitHalfW + 6.5f;
    fenceLine({pitX0 - 8, 0, pitZ + fz}, {pitX1 + 8, 0, pitZ + fz}, 3.f);
    fenceLine({pitX0 - 8, 0, pitZ - fz}, {pitX1 + 8, 0, pitZ - fz}, 3.f);
    {   // Bowl ring fence with an entrance gap facing west.
        const int n = 26;
        int first = int(posts.size()), made = 0;
        for (int s = 0; s < n; s++) {
            float a = 2 * PI * s / n;
            if (std::fabs(std::remainder(a - PI, 2 * PI)) < 0.35f) continue;
            float r = bowlRadius + 5.f;
            FencePost fp;
            fp.base = {bowlCenter.x + r * std::cos(a), 0, bowlCenter.z + r * std::sin(a)};
            fp.base.y = ground(fp.base.x, fp.base.z);
            posts.push_back(fp);
            if (made > 0) fenceSegs.push_back({first + made - 1, first + made});
            made++;
        }
    }

    // ---- Barrels ----
    auto addBarrel = [&](float x, float z, int variant) {
        Barrel b;
        b.pos = {x, ground(x, z), z};
        b.yaw = rng.range(0, 2 * PI);
        b.variant = variant;
        barrels.push_back(b);
    };
    for (int l = 0; l <= lanes; l++) {
        float zl = pitZ - pitHalfW + l * (2 * pitHalfW / lanes);
        addBarrel(pitX0 - 1.5f, zl, 0);
        addBarrel(pitX1 + 1.5f, zl, 0);
    }
    for (float x = pitX0 + 5; x < pitX1; x += 10) {
        addBarrel(x, pitZ + pitHalfW + 1.0f, (int(x) / 10) & 1);
        addBarrel(x + 5, pitZ - pitHalfW - 1.0f, ((int(x) / 10) + 1) & 1);
    }
    for (int s = 0; s < 10; s++) {
        float a = 2 * PI * s / 10 + 0.3f;
        addBarrel(bowlCenter.x + (bowlRadius + 1.5f) * std::cos(a), bowlCenter.z + (bowlRadius + 1.5f) * std::sin(a), s & 1);
    }
    for (size_t s = 0; s < circuit.size(); s++) {
        const Vec3& c = circuit[s];
        if (length(Vec2{c.x - pondCenter.x, c.z - pondCenter.z}) < pondRadius + 2) continue;
        addBarrel(c.x + 3.6f, c.z + 1.0f, s & 1);
        addBarrel(c.x - 3.6f, c.z - 1.0f, (s + 1) & 1);
    }
    barrelsInitial = barrels;

    // ---- Spectators along the north fence (photo 3) ----
    for (int s = 0; s < 46; s++) {
        float x = rng.range(-34, 34), z = pitZ + fz + rng.range(1.6f, 5.5f);
        CadParams pr{{"shirt_r", rng.range(0.15f, 0.8f)}, {"shirt_g", rng.range(0.15f, 0.7f)},
                     {"shirt_b", rng.range(0.2f, 0.8f)}, {"vest", rng.uniform() < 0.4f ? 1.f : 0.f}};
        addProp("props/spectator.mbcad", x, z, PI + rng.range(-0.5f, 0.5f), rng.range(0.92f, 1.08f), pr);
    }

    // ---- Parked rigs, dozer, bus, brush ----
    auto boxCol = [&](Vec3 c, Vec3 half, float yaw) { colliders.push_back({{c.x, ground(c.x, c.z) + half.y, c.z}, half, yaw}); };
    addProp("vehicles/silverado.mbcad", -54, pitZ + fz + 9, 0.35f, 1, {{"hood_open", 1.f}});
    boxCol({-54, 0, pitZ + fz + 9}, {1.0f, 0.9f, 3.0f}, 0.35f);
    addProp("vehicles/utv.mbcad", -46, pitZ + fz + 7.5f, -0.5f);
    boxCol({-46, 0, pitZ + fz + 7.5f}, {0.8f, 0.8f, 1.7f}, -0.5f);
    addProp("vehicles/bogcar76.mbcad", -22, pitZ + fz - 2.2f, PI * 0.5f + 0.15f);
    boxCol({-22, 0, pitZ + fz - 2.2f}, {1.0f, 0.7f, 2.9f}, PI * 0.5f + 0.15f);
    addProp("props/dozer450.mbcad", dozerPos.x, dozerPos.z, -2.3f);
    boxCol(dozerPos, {1.2f, 1.3f, 1.9f}, -2.3f);
    addProp("props/schoolbus.mbcad", busPos.x, busPos.z, 2.45f);
    boxCol(busPos, {1.3f, 1.6f, 4.8f}, 2.45f);
    addProp("vegetation/brushpile.mbcad", busPos.x + 6, busPos.z + 4, 0.4f, 1.6f);
    addProp("vegetation/brushpile.mbcad", busPos.x + 2, busPos.z + 8, 2.0f, 1.3f);
    for (int s = 0; s < 14; s++)
        addProp("vegetation/sumac.mbcad", busPos.x + rng.range(-6, 14), busPos.z + rng.range(-4, 12), rng.range(0, 6.28f),
                rng.range(0.8f, 1.5f), {{"red", rng.uniform() < 0.5f ? 1.f : 0.25f}});

    // ---- Tree line (poplar/aspen in October, a few spruce) ----
    auto clearOfCourse = [&](float x, float z, float margin) {
        if (std::fabs(z - pitZ) < pitHalfW + 12 && std::fabs(x) < 60) return false;
        if (length(Vec2{x - bowlCenter.x, z - bowlCenter.z}) < bowlRadius + 8 + margin) return false;
        if (length(Vec2{x - pondCenter.x, z - pondCenter.z}) < pondRadius + 3) return false;
        for (size_t s = 0; s < circuit.size(); s++) {
            const Vec3 &a = circuit[s], &b = circuit[(s + 1) % circuit.size()];
            if (distToSegment({x, z}, {a.x, a.z}, {b.x, b.z}) < 4.f + margin) return false;
        }
        return true;
    };
    for (int s = 0; s < 520; s++) {
        float x = rng.range(-94, 94), z = rng.range(-94, 94);
        float edge = std::max(std::fabs(x), std::fabs(z));
        bool cluster = length(Vec2{x - busPos.x, z - busPos.z - 14}) < 16;
        if (edge < 82 && !cluster) continue;
        if (!clearOfCourse(x, z, 2)) continue;
        bool spruce = rng.uniform() < (cluster ? 0.35f : 0.12f);
        float sc = rng.range(0.75f, 1.25f);
        if (spruce) addProp("vegetation/spruce.mbcad", x, z, rng.range(0, 6.28f), sc);
        else addProp("vegetation/poplar.mbcad", x, z, rng.range(0, 6.28f), sc, {{"yellow", rng.range(0.55f, 1.f)}, {"h", rng.range(7, 12)}});
        colliders.push_back({{x, ground(x, z) + 2, z}, {0.25f, 2, 0.25f}, 0});
    }
    for (int s = 0; s < 420; s++) {
        float x = rng.range(-90, 90), z = rng.range(-90, 90);
        if (!clearOfCourse(x, z, 1)) continue;
        addProp("vegetation/goldenrod.mbcad", x, z, rng.range(0, 6.28f), rng.range(0.7f, 1.3f));
    }
    // Barbed-wire perimeter posts (photo 1-2 foreground).
    for (float x = -90; x <= 90; x += 4.5f) addProp("props/fencepost_wood.mbcad", x, -91.5f, rng.range(-0.2f, 0.2f));
}

void World::resetDynamics() {
    barrels = barrelsInitial;
    for (auto& b : barrels) b.pos.y = terrain.heightAt(b.pos.x, b.pos.z);
}

void World::stepDynamics(float dt, std::vector<Vehicle*>& vehicles, WorldEvents& ev) {
    const float r = 0.29f;
    for (auto& b : barrels) {
        for (Vehicle* v : vehicles) {
            Vec3 d = b.pos - v->pos;
            if (sq(d.x) + sq(d.z) > 36.f) continue;
            Vec3 l = v->rot.unrotate(d + Vec3{0, 0.45f, 0});
            float hx = v->body->dims[0] * 0.5f + r, hz = v->body->dims[2] * 0.5f + r;
            if (std::fabs(l.x) < hx && std::fabs(l.z) < hz && l.y > -1.6f && l.y < 1.6f) {
                Vec3 vp = v->vel + cross(v->angVel, b.pos - v->pos);
                Vec3 rel = vp - b.vel;
                float rs = length(rel);
                if (rs < 0.4f) continue;
                Vec3 dir = normalize(Vec3{d.x, 0, d.z});
                b.vel = vp * 1.1f + dir * (1.f + rs * 0.2f) + Vec3{0, 1.2f + rs * 0.12f, 0};
                if (!b.knocked) { ev.barrelsKnocked++; b.knocked = true; }
                b.tiltVel = 4.f + rs * 0.5f;
                v->vel *= 0.997f;
                ev.barrelHits.push_back(b.pos);
                ev.barrelStrength.push_back(clampf(rs / 10.f, 0.2f, 1.5f));
            }
        }
        if (!b.knocked && length(b.vel) < 1e-3f) continue;
        b.vel.y -= 9.81f * dt;
        b.pos += b.vel * dt;
        float g = terrain.heightAt(b.pos.x, b.pos.z);
        if (b.pos.y <= g) {
            b.pos.y = g;
            if (b.vel.y < -1.5f) b.vel.y *= -0.3f;
            else b.vel.y = 0;
            TerrainSample ts = terrain.sample(b.pos.x, b.pos.z);
            float fr = 1.6f + 4.f * ts.moisture;
            b.vel.x *= std::exp(-fr * dt);
            b.vel.z *= std::exp(-fr * dt);
            if (length(b.vel) < 0.05f) b.vel = {};
        }
        b.tilt = std::min(PI * 0.5f, b.tilt + b.tiltVel * dt);
        if (b.tilt >= PI * 0.5f) b.tiltVel = 0;
        b.spin += length(Vec3{b.vel.x, 0, b.vel.z}) / r * dt * (b.tilt > 1.f ? 1.f : 0.f);
    }
    for (auto& p : posts) {
        for (Vehicle* v : vehicles) {
            Vec3 d = p.base - v->pos;
            if (sq(d.x) + sq(d.z) > 25.f) continue;
            Vec3 l = v->rot.unrotate(d + Vec3{0, 0.6f, 0});
            float hx = v->body->dims[0] * 0.5f + 0.05f, hz = v->body->dims[2] * 0.5f + 0.05f;
            if (std::fabs(l.x) < hx && std::fabs(l.z) < hz && l.y > -1.5f && l.y < 1.5f) {
                float spd = length(Vec3{v->vel.x, 0, v->vel.z});
                float target = std::min(1.45f, spd * 0.16f);
                if (target > p.lean + 0.05f) {
                    p.lean = target;
                    p.leanDir = normalize(Vec3{v->vel.x, 0, v->vel.z});
                    ev.fenceHits.push_back(p.base);
                    v->vel *= 0.985f;
                }
            }
        }
    }
}

void World::collideStatic(Vehicle& v, float dt) {
    (void)dt;
    const float w = v.body->dims[0] * 0.5f, l = v.body->dims[2] * 0.5f, hh = v.body->dims[1] * 0.5f;
    Vec3 pts[10];
    int n = 0;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            pts[n++] = v.pos + v.rot.rotate({sx * w, 0.1f, sz * l});
            pts[n++] = v.pos + v.rot.rotate({sx * w, hh, sz * l * 0.5f});
        }
    pts[n++] = v.pos + v.rot.rotate({0, 0.2f, l});
    pts[n++] = v.pos + v.rot.rotate({0, 0.2f, -l});
    for (const BoxCollider& c : colliders) {
        if (sq(c.center.x - v.pos.x) + sq(c.center.z - v.pos.z) > 100.f) continue;
        Quat q = Quat::axisAngle({0, 1, 0}, c.yaw);
        for (int k = 0; k < n; k++) {
            Vec3 lp = q.unrotate(pts[k] - c.center);
            if (std::fabs(lp.x) >= c.half.x || std::fabs(lp.y) >= c.half.y || std::fabs(lp.z) >= c.half.z) continue;
            float px = c.half.x - std::fabs(lp.x), pz = c.half.z - std::fabs(lp.z);
            Vec3 nLocal = px < pz ? Vec3{signf(lp.x), 0, 0} : Vec3{0, 0, signf(lp.z)};
            float pen = std::min(px, pz);
            Vec3 nW = q.rotate(nLocal);
            v.pos += nW * pen;
            float vn = dot(v.vel, nW);
            if (vn < 0) {
                v.vel -= nW * (vn * 1.3f);
                if (-vn * v.mass * 0.2f > v.events.impact) { v.events.impact = -vn * v.mass * 0.2f; v.events.impactPos = pts[k]; }
            }
        }
    }
}

}  // namespace mb
