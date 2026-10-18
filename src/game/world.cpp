#include "world.h"
#include "../physics/terramechanics.h"
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

    // Back-country trail: a wobbly loop well outside the venue's tree line.
    trail.clear();
    for (int k = 0; k < 72; k++) {
        float a = 2 * PI * float(k) / 72.f;
        float R = 148.f + 12.f * std::sin(3 * a + 0.7f) + 7.f * std::sin(7 * a + 2.f);
        trail.push_back({R * std::cos(a), 0, R * std::sin(a)});
    }
    {
        Vec3 dir = normalize(Vec3{-hillCenter.x, 0, -hillCenter.z});
        chuteTop = hillCenter + dir * 5.f;
        chuteBottom = hillCenter + dir * (hillRadius + 4.f);
    }
    entranceRoad = {{0, 0, -124}, {0, 0, -91.5f}, {0, 0, -58}};
    returnRoad = {{68, 0, -24}, {56, 0, -40}, {44, 0, -58}, {-56, 0, -58}, {-62, 0, -64}};
    campRoad = {{82, 0, 0}, {100, 0, 2}, {114, 0, 2}, {114, 0, 34}};
    auto polyDist = [](const std::vector<Vec3>& P, float x, float z, float* along = nullptr) {
        float best = 1e9f, acc = 0, bestAlong = 0;
        for (size_t s = 0; s + 1 < P.size(); s++) {
            float t;
            float d = distToSegment({x, z}, {P[s].x, P[s].z}, {P[s + 1].x, P[s + 1].z}, &t);
            float L = length(Vec2{P[s + 1].x - P[s].x, P[s + 1].z - P[s].z});
            if (d < best) { best = d; bestAlong = acc + t * L; }
            acc += L;
        }
        if (along) *along = bestAlong;
        return best;
    };
    auto natural = [&](float x, float z) { return (fbm(x * 0.011f + 3.1f, z * 0.011f - 7.4f, 4, seed) - 0.5f) * 1.8f; };
    const float hPit = natural(0, pitZ), hBowl = natural(bowlCenter.x, bowlCenter.z);
    const float hPond = natural(pondCenter.x, pondCenter.z);
    // Sinkholes staggered down every lane (own RNG so the rest of the layout is unchanged):
    // deep, water-filled pockets of soft slop that catch a front axle and stop a stock rig.
    struct Hole { float x, z, r, depth; };
    std::vector<Hole> holes;
    {
        Rng hr(seed * 31u + 7u);
        // Same hole pattern in every lane, so a drag race is fair and difficulty is consistent.
        struct Slot { float x, dz, r, d; };
        std::vector<Slot> slots;
        for (float x = pitX0 + 9.f + hr.range(0.f, 3.f); x < pitX1 - 6.f; x += hr.range(6.5f, 8.5f))
            slots.push_back({x, hr.range(-0.6f, 0.6f), hr.range(1.9f, 2.3f), hr.range(0.6f, 0.7f)});
        for (int l = 0; l < lanes; l++)
            for (const Slot& sl : slots) holes.push_back({sl.x, laneZ(l) + sl.dz, sl.r, sl.d});
        // Spoil mounds (negative depth = hump), also mirrored into every lane.
        std::vector<Slot> mounds;
        for (int k = 0; k < 4; k++) mounds.push_back({hr.range(pitX0 + 8, pitX1 - 8), hr.range(-2.2f, 2.2f), hr.range(1.0f, 1.6f), -hr.range(0.18f, 0.3f)});
        for (int l = 0; l < lanes; l++)
            for (const Slot& m : mounds) holes.push_back({m.x, laneZ(l) + m.dz, m.r, m.d});
    }

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
            uint8_t soil = uint8_t(Soil::Loam);
            T.track[k] = 0;

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
                for (int l = 0; l < lanes; l++)   // one old wallow per lane, same spot in each
                    floor -= 0.3f * gauss(sq(x - (pitX0 + 0.42f * (pitX1 - pitX0))) + sq(z - laneZ(l)), 3.f);
                // Whoops: humps across the lanes that unload the tires on every crest.
                float along = (x - pitX0) / (pitX1 - pitX0);
                floor += 0.22f * std::sin(x * (2 * PI / 6.2f) + n1 * 2.5f) * smoothstep(0.06f, 0.2f, along) * (0.6f + 0.6f * n2);
                // The middle third is the soup: the floor falls away under deeper water and softer slop.
                float soup = smoothstep(0.28f, 0.45f, along) * (1.f - smoothstep(0.78f, 0.92f, along));
                floor -= 0.32f * soup;
                // Old ruts from earlier runs pull the tires into the lane's grooves.
                float soft = 0;
                for (int l = 0; l < lanes; l++) {
                    float dz = z - laneZ(l);
                    floor -= 0.13f * (std::exp(-sq(dz - 0.8f) / 0.05f) + std::exp(-sq(dz + 0.8f) / 0.05f));
                }
                for (auto& hl : holes) {
                    float g = gauss(sq(x - hl.x) + sq(z - hl.z), hl.r);
                    floor -= hl.depth * g;
                    if (hl.depth > 0) soft = std::max(soft, g);
                }
                h = lerpf(h, floor, tp);
                base = lerpf(base, hPit - 1.3f - 0.5f * std::max(soft, soup) + (n2 - 0.5f) * 0.1f, tp);
                moist = lerpf(moist, 0.86f + 0.14f * n1, tp);
                if (tp > 0.3f) soil = uint8_t(Soil::Clay);
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
                if (t > 0.3f) soil = uint8_t(Soil::Silt);
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
                if (t > 0.3f) soil = uint8_t(Soil::Clay);
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

            // ---------------- back-country ----------------
            float edge = std::max(std::fabs(x), std::fabs(z));
            float outer = smoothstep(98.f, 130.f, edge);
            if (outer > 0) {
                // Rolling farm country beyond the venue.
                float roll = (fbm(x * 0.006f + 11.f, z * 0.006f - 3.f, 4, seed + 21) - 0.5f) * 8.f;
                h += roll * outer;
                base += roll * outer;
                // The hill (with a muddy climb chute facing the venue).
                float dH = length(Vec2{x - hillCenter.x, z - hillCenter.z});
                if (dH < hillRadius) {
                    float hh = hillHeight * std::pow(std::cos(dH / hillRadius * PI * 0.5f), 1.5f);
                    h += hh; base += hh;
                    float t;
                    float dc = distToSegment({x, z}, {chuteTop.x, chuteTop.z}, {chuteBottom.x, chuteBottom.z}, &t);
                    float ch = std::exp(-dc * dc / 6.f);
                    h -= 0.55f * ch;
                    base -= 1.0f * ch;
                    moist = std::max(moist, 0.62f + 0.3f * ch * (0.6f + 0.4f * n1));
                    grass *= 1.f - ch;
                    if (ch > 0.25f) soil = uint8_t(Soil::Clay);
                }
                // Mud flats to the west: a big shallow slop field with puddles.
                float ex = (x - flatsCenter.x) / flatsHalfX, ez = (z - flatsCenter.z) / flatsHalfZ;
                float e = ex * ex + ez * ez;
                if (e < 1.2f) {
                    float t = 1.f - smoothstep(0.65f, 1.15f, e);
                    float flatH = natural(flatsCenter.x, flatsCenter.z) - 0.2f + (n1 - 0.5f) * 0.25f + (n2 - 0.5f) * 0.08f;
                    h = lerpf(h, flatH, t);
                    base = lerpf(base, flatH - 0.55f, t);
                    moist = lerpf(moist, 0.72f + 0.25f * n1, t);
                    grass *= 1.f - t;
                    if (t > 0.3f) soil = uint8_t(Soil::Sand);
                    if (t > 0.6f && n2 > 0.6f) water = flatH - (n1 - 0.5f) * 0.25f - 0.06f;
                }
                // Creek across the south: muddy banks, deep pools, shallow fords where the trail crosses.
                float dz = std::fabs(z - creekZ(x));
                if (dz < creekHalfW + 4.f) {
                    float bank = h;
                    float ford = 0;
                    for (const Vec3& tp : trail)
                        if (std::fabs(tp.x - x) < 9.f && std::fabs(creekZ(tp.x) - tp.z) < 12.f) ford = 1;
                    float depth = lerpf(1.25f, 0.55f, ford) * (0.8f + 0.4f * n1);
                    if (dz < creekHalfW) {
                        float bed = bank - depth * (1.f - sq(dz / creekHalfW));
                        h = bed;
                        base = std::min(base, bed - 0.35f);
                        grass = 0;
                        water = bank - 0.45f;
                    }
                    moist = std::max(moist, lerpf(1.f, 0.5f, clampf((dz - creekHalfW) / 4.f, 0.f, 1.f)));
                    if (dz < creekHalfW + 2.f) soil = uint8_t(Soil::Silt);
                    grass *= clampf((dz - creekHalfW) / 4.f, 0.f, 1.f);
                }
                // Peat swamp: black organic muck over a very deep soft layer, pools of tea-dark water.
                float dS = length(Vec2{x - swampCenter.x, z - swampCenter.z});
                if (dS < swampRadius + 6.f) {
                    float t = 1.f - smoothstep(swampRadius - 4.f, swampRadius + 5.f, dS);
                    float sh = natural(swampCenter.x, swampCenter.z) - 0.35f + (n1 - 0.5f) * 0.3f;
                    h = lerpf(h, sh, t);
                    base = lerpf(base, sh - 2.2f, t);
                    moist = lerpf(moist, 0.93f + 0.07f * n2, t);
                    grass = lerpf(grass, 0.15f * n2, t);
                    if (t > 0.4f) soil = uint8_t(Soil::Peat);
                    if (t > 0.7f && n2 > 0.5f) water = sh + 0.12f - (n1 - 0.5f) * 0.3f;
                }
                // The trail: two ruts, worn grass, mud holes every so often.
                float rr = length(Vec2{x, z});
                if (rr > 115.f && rr < 182.f) {
                    float a = std::atan2(z, x);
                    int n = int(trail.size()), k0 = int(std::floor((a < 0 ? a + 2 * PI : a) / (2 * PI) * n));
                    float bestD = 1e9f;
                    int bestK = 0;
                    for (int dk = -2; dk <= 2; dk++) {
                        int ka = ((k0 + dk) % n + n) % n, kb = (ka + 1) % n;
                        float d = distToSegment({x, z}, {trail[ka].x, trail[ka].z}, {trail[kb].x, trail[kb].z});
                        if (d < bestD) { bestD = d; bestK = ka; }
                    }
                    if (bestD < 4.f) {
                        float tw = 1.f - smoothstep(2.0f, 3.6f, bestD);
                        float rut = std::exp(-sq(bestD - 0.8f) / 0.04f);
                        h -= 0.08f * rut * tw;
                        grass = lerpf(grass, 0.25f, tw);
                        grass -= 0.2f * rut * tw;
                        moist = std::max(moist, 0.38f * tw);
                        if (bestK % 6 == 3) {   // mud hole
                            const Vec3& hp = trail[size_t(bestK)];
                            float g = gauss(sq(x - hp.x) + sq(z - hp.z), 3.2f);
                            h -= 0.42f * g; base -= 0.6f * g;
                            moist = std::max(moist, 0.95f * g);
                            if (g > 0.35f) water = std::max(water, h + 0.42f * g - 0.18f);
                        }
                    }
                }
            }

            // ---------------- venue grounds ----------------
            float comp = moist > 0.6f ? 0.01f : 0.08f;
            auto inRect = [&](Vec3 lo, Vec3 hi, float m) {   // 1 inside, fading over m metres
                float dx = std::max(lo.x - x, x - hi.x), dz = std::max(lo.z - z, z - hi.z);
                return 1.f - smoothstep(0.f, m, std::max(dx, dz));
            };
            auto gravel = [&](float t) {
                if (t <= 0) return;
                grass = lerpf(grass, 0.f, t);
                moist = lerpf(moist, 0.16f + 0.06f * n2, t);
                comp = lerpf(comp, 0.4f, t);
                if (t > 0.45f) soil = uint8_t(Soil::Gravel);
            };
            auto worn = [&](float t, float g) {   // trampled grass / packed dirt
                grass = lerpf(grass, g * (0.7f + 0.6f * n2), t);
                comp = lerpf(comp, 0.25f, t);
            };
            {
                float along;
                float dE = polyDist(entranceRoad, x, z);
                gravel(1.f - smoothstep(2.6f, 3.6f, dE));
                float dR = polyDist(returnRoad, x, z, &along);
                if (dR < 4.5f) {
                    gravel(1.f - smoothstep(2.6f, 3.8f, dR));
                    // Mud dragged off the course: wet, dark tracks for the first stretch past the exit.
                    float carry = (1.f - smoothstep(10.f, 45.f, along)) * (1.f - smoothstep(1.5f, 3.f, dR));
                    moist = std::max(moist, (0.45f + 0.25f * n1) * carry);
                    T.track[k] = uint8_t(std::max(float(T.track[k]), 160.f * carry * (0.6f + 0.4f * n2)));
                }
                gravel(1.f - smoothstep(2.0f, 3.0f, polyDist(campRoad, x, z)));
                // Gate apron, wash bay apron, staging/tech apron edges.
                gravel(inRect({-9, 0, -96}, {9, 0, -86}, 2.f));
                gravel(inRect({washCenter.x - washHalfX - 2, 0, washCenter.z - washHalfZ - 2},
                              {washCenter.x + washHalfX + 2, 0, washCenter.z + washHalfZ + 2}, 1.5f));
                // Paddock: packed dirt with gravel lanes between the trailer rows.
                float pad = inRect(paddockMin, paddockMax, 3.f);
                worn(pad, 0.32f);
                if (pad > 0.5f && (std::fabs(z + 71.f) < 2.5f || std::fabs(z + 81.f) < 2.5f)) gravel(pad);
                // Visitor parking: mowed field with worn wheel tracks along the rows.
                float park = inRect(parkingMin, parkingMax, 2.f);
                worn(park, 0.55f);
                if (park > 0.5f && std::fabs(std::fmod(z + 200.f, 8.f) - 4.f) < 1.4f) worn(park, 0.2f);
                // Village: trampled grass, gravel around the food trucks.
                float vil = inRect(villageMin, villageMax, 2.f);
                worn(vil, 0.38f);
                gravel(inRect({6, 0, -86}, {13, 0, -64}, 1.f));
                // Shutdown area past the finish: churned, mud-tracked dirt.
                float sd = inRect(shutdownMin, shutdownMax, 3.f);
                if (sd > 0) {
                    worn(sd, 0.1f);
                    moist = std::max(moist, 0.5f * sd * (0.5f + 0.5f * n1));
                    T.track[k] = uint8_t(std::max(float(T.track[k]), 120.f * sd * n2));
                }
                // Footpaths: village to the bleachers, parking to the village, the campground path.
                float fp = std::min({distToSegment({x, z}, {18, -64}, {18, -55}), distToSegment({x, z}, {-12, -72}, {5, -72}),
                                     distToSegment({x, z}, {-6, -55}, {6, -55}), distToSegment({x, z}, {30, -55}, {82, -56})});
                worn(1.f - smoothstep(0.6f, 1.4f, fp), 0.15f);
                // Spectator bank along the south fence is worn by foot traffic.
                if (std::fabs(x) < 40.f && z > -10.f && z < -3.f) worn(0.7f, 0.3f);
            }
            // Burnout pad: graded dead flat, hard standing.
            {
                float bp = inRect({burnoutCenter.x - burnoutHalfX, 0, burnoutCenter.z - burnoutHalfZ},
                                  {burnoutCenter.x + burnoutHalfX, 0, burnoutCenter.z + burnoutHalfZ}, 3.f);
                if (bp > 0) {
                    float hb = natural(burnoutCenter.x, burnoutCenter.z);
                    h = lerpf(h, hb, bp);
                    base = lerpf(base, hb - 0.02f, bp);
                    gravel(bp);
                    moist = lerpf(moist, 0.1f, bp);
                    comp = lerpf(comp, 0.6f, bp);
                }
            }
            // Bounty hole: graded approach, a 20 m bottomless clay trench, spoil and a viewing bank.
            {
                float bc = 0.5f * (bountyX0 + bountyX1);
                float site = inRect({bountyX0 - 14, 0, bountyZ - 18}, {bountyX1 + 4, 0, bountyZ + 10}, 6.f);
                if (site > 0) {
                    float hb = natural(bc, bountyZ);
                    h = lerpf(h, hb + (n2 - 0.5f) * 0.05f, site);
                    base = lerpf(base, hb - 0.08f, site);
                    float dz = std::fabs(z - bountyZ);
                    float tx = smoothstep(bountyX0, bountyX0 + 5, x) * (1.f - smoothstep(bountyX1 - 5, bountyX1, x));
                    float tz = 1.f - smoothstep(bountyHalfW - 2.f, bountyHalfW, dz);
                    float t = tx * tz * site;
                    if (t > 0) {
                        float along = (x - bountyX0) / (bountyX1 - bountyX0);
                        float floor = hb - 0.55f - 0.65f * std::sin(along * PI) - 0.25f * gauss(sq(x - bc - 3) + sq(dz), 4.f) + (n1 - 0.5f) * 0.2f;
                        h = lerpf(h, floor, t);
                        base = lerpf(base, hb - 2.6f + (n2 - 0.5f) * 0.3f, t);
                        moist = lerpf(moist, 0.92f + 0.08f * n1, t);
                        grass *= 1.f - std::min(1.f, 2.f * t);
                        if (t > 0.3f) soil = uint8_t(Soil::Clay);
                        if (t > 0.55f) water = hb - 0.42f;
                        comp = 0.01f;
                    }
                    // Spoil berms either side; the north one is the crowd's viewing bank.
                    float bx = smoothstep(bountyX0 - 2, bountyX0 + 3, x) * (1.f - smoothstep(bountyX1 - 3, bountyX1 + 2, x));
                    float berm = 0.9f * std::exp(-sq(dz - bountyHalfW - 2.5f) / 2.5f) * bx;
                    h += berm; base += berm * 0.9f;
                    if (z < bountyZ) worn(std::exp(-sq(dz - bountyHalfW - 7.f) / 8.f) * bx, 0.25f);
                    worn((1.f - tz) * tx * site * std::exp(-sq(dz - bountyHalfW - 1.f)), 0.1f);
                    // Approach lane from the venue: graded dirt.
                    if (x < bountyX0 && dz < 4.f) worn(site * (1.f - smoothstep(2.5f, 4.f, dz)), 0.2f);
                }
            }
            // Campground: graded field, worn sites.
            {
                float cg = inRect(campMin, campMax, 6.f);
                if (cg > 0) {
                    float hc = natural(0.5f * (campMin.x + campMax.x), 0.5f * (campMin.z + campMax.z));
                    h = lerpf(h, hc + (h - hc) * 0.25f, cg);
                    base = lerpf(base, h - 0.08f, cg);
                    moist = lerpf(moist, 0.24f, cg);
                    worn(cg * 0.6f, 0.5f);
                }
            }

            T.h[k] = h;
            T.base[k] = std::min(base, h - 0.02f);
            T.moist[k] = clampf(moist, 0.f, 1.f);
            T.grass[k] = uint8_t(clampf(grass, 0.f, 1.f) * 255.f);
            T.water[k] = water;
            T.comp[k] = comp;
            T.soil[k] = soil;
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

    // People share a small wardrobe so identical figures batch into one instanced mesh.
    auto outfit = [](Rng& r, bool vestAllowed) {
        static const float shirts[][3] = {{0.75f, 0.12f, 0.1f}, {0.12f, 0.25f, 0.55f}, {0.15f, 0.42f, 0.2f}, {0.85f, 0.85f, 0.82f},
                                          {0.2f, 0.2f, 0.22f}, {0.55f, 0.35f, 0.2f}, {0.85f, 0.6f, 0.15f}, {0.45f, 0.25f, 0.5f},
                                          {0.35f, 0.55f, 0.7f}, {0.6f, 0.15f, 0.35f}};
        static const float hats[][3] = {{0.12f, 0.14f, 0.18f}, {0.7f, 0.12f, 0.1f}, {0.3f, 0.4f, 0.22f}, {0.85f, 0.8f, 0.7f}};
        const float* c = shirts[r.next() % 10];
        const float* h = hats[r.next() % 4];
        CadParams pr{{"shirt_r", c[0]}, {"shirt_g", c[1]}, {"shirt_b", c[2]}, {"vest", vestAllowed && r.uniform() < 0.35f ? 1.f : 0.f},
                     {"skin", float(r.next() % 3) * 0.5f}, {"pants", r.uniform() < 0.25f ? 1.f : 0.f}, {"hat", r.uniform() < 0.45f ? 1.f : 0.f},
                     {"hat_r", h[0]}, {"hat_g", h[1]}, {"hat_b", h[2]}};
        if (pr["hat"] == 0.f) pr["hat_r"] = pr["hat_g"] = pr["hat_b"] = 0.f;
        // Body language: most stand arms-down, some hands on hips, a few cheering; tees when it's warm enough.
        float pq = r.uniform();
        pr["pose"] = pq < 0.6f ? 0.f : (pq < 0.82f ? 2.f : 1.f);
        pr["tee"] = pr["vest"] == 0.f && r.uniform() < 0.4f ? 1.f : 0.f;
        return pr;
    };
    // ---- Spectators along the north fence (photo 3) ----
    for (int s = 0; s < 46; s++) {
        float x = rng.range(-34, 34), z = pitZ + fz + rng.range(1.6f, 5.5f);
        CadParams pr = outfit(rng, true);
        addProp("props/spectator.mbcad", x, z, PI + rng.range(-0.5f, 0.5f), rng.range(0.92f, 1.08f), pr);
    }

    // ---- Parked rigs, dozer, bus, brush ----
    auto boxCol = [&](Vec3 c, Vec3 half, float yaw) { colliders.push_back({{c.x, ground(c.x, c.z) + half.y, c.z}, half, yaw}); };
    // Parked rig on its own wheels at ride height: body origin = wheel center - axleY.
    auto addParked = [&](const std::string& part, float x, float z, float yaw, float fa, float ra, float track, float axleY,
                         float tireR, float dirt, CadParams pr = {}) {
        pr["driver"] = 0;   // parked: nobody in the cab (the glass is see-through)
        float c = std::cos(yaw), s = std::sin(yaw);
        auto world = [&](float lx, float lz) { return Vec2{x + lx * c + lz * s, z - lx * s + lz * c}; };
        float gy = 1e9f;
        CadParams wp{{"tire_r", tireR}, {"tire_w", 0.3175f}, {"lugs", 18}};
        for (int i = 0; i < 4; i++) {
            float lx = (i & 1) ? -track * 0.5f : track * 0.5f, lz = (i & 2) ? ra : fa;
            Vec2 w = world(lx, lz);
            float g = ground(w.x, w.y);
            gy = std::min(gy, g);
            PropInstance wi{"wheels/wheel.mbcad", wp, {w.x, g + tireR, w.y}, yaw + ((i & 1) ? PI : 0.f), 1.f, std::min(1.f, dirt * 1.3f)};
            props.push_back(wi);
        }
        props.push_back({part, pr, {x, gy + tireR - axleY, z}, yaw, 1.f, dirt});
    };
    addParked("vehicles/silverado.mbcad", -54, pitZ + fz + 9, 0.35f, 1.56f, -2.34f, 1.73f, -0.46f, 0.4826f, 0.85f, {{"hood_open", 1.f}});
    boxCol({-54, 0, pitZ + fz + 9}, {1.0f, 0.9f, 3.0f}, 0.35f);
    addParked("vehicles/utv.mbcad", -46, pitZ + fz + 7.5f, -0.5f, 1.03f, -1.26f, 1.63f, -0.3f, 0.4064f, 0.6f);
    boxCol({-46, 0, pitZ + fz + 7.5f}, {0.8f, 0.8f, 1.7f}, -0.5f);
    addParked("vehicles/bogcar76.mbcad", -22, pitZ + fz - 2.2f, PI * 0.5f + 0.15f, 1.36f, -1.73f, 1.62f, -0.36f, 0.419f, 0.45f);
    boxCol({-22, 0, pitZ + fz - 2.2f}, {1.0f, 0.7f, 2.9f}, PI * 0.5f + 0.15f);
    addProp("props/dozer450.mbcad", dozerPos.x, dozerPos.z, -2.3f);
    boxCol(dozerPos, {1.2f, 1.3f, 1.9f}, -2.3f);
    addProp("props/schoolbus.mbcad", busPos.x, busPos.z, 2.45f);
    boxCol(busPos, {1.3f, 1.6f, 5.15f}, 2.45f);
    addProp("vegetation/brushpile.mbcad", busPos.x + 6, busPos.z + 4, 0.4f, 1.6f);
    addProp("vegetation/brushpile.mbcad", busPos.x + 2, busPos.z + 8, 2.0f, 1.3f);
    for (int s = 0; s < 14; s++)
        addProp("vegetation/sumac.mbcad", busPos.x + rng.range(-6, 14), busPos.z + rng.range(-4, 12), rng.range(0, 6.28f),
                rng.range(0.8f, 1.5f), {{"red", rng.uniform() < 0.5f ? 1.f : 0.25f}});

    // ---- Venue grounds: gate, village, paddock, staging, bleachers, shutdown, bounty, camp ----
    {
        Rng gr(seed * 977u + 3u);
        auto col3 = [](float r, float g, float b, const char* p) {
            return CadParams{{std::string(p) + "_r", r}, {std::string(p) + "_g", g}, {std::string(p) + "_b", b}};
        };
        auto with = [](CadParams a, const CadParams& b) { for (auto& kv : b) a[kv.first] = kv.second; return a; };
        auto person = [&](float x, float z, float yaw, bool vest, float y = -1e9f) {
            CadParams pr = outfit(gr, true);
            if (vest) { pr["shirt_r"] = 0.95f; pr["shirt_g"] = 0.55f; pr["shirt_b"] = 0.1f; pr["vest"] = 0.f; pr["hat"] = 1.f;   // hi-vis staff
                        pr["hat_r"] = 0.95f; pr["hat_g"] = 0.55f; pr["hat_b"] = 0.1f; if (pr["pose"] == 1.f) pr["pose"] = 2.f; }
            props.push_back({"props/spectator.mbcad", pr, {x, y > -1e8f ? y : ground(x, z), z}, yaw, gr.range(0.94f, 1.06f)});
        };
        // Footprint collider for a building (half extents in the part's own frame).
        auto building = [&](const std::string& part, float x, float z, float yaw, Vec3 half, CadParams pr = {}) {
            addProp(part, x, z, yaw, 1.f, pr);
            if (half.x > 0) boxCol({x, 0, z}, half, yaw);
        };
        auto car = [&](float x, float z, float yaw) {
            static const float paints[][3] = {{0.75f, 0.75f, 0.76f}, {0.1f, 0.1f, 0.11f}, {0.55f, 0.08f, 0.07f}, {0.9f, 0.9f, 0.9f},
                                              {0.12f, 0.2f, 0.38f}, {0.3f, 0.32f, 0.3f}, {0.45f, 0.36f, 0.25f}, {0.15f, 0.3f, 0.18f}};
            const float* c = paints[gr.next() % 8];
            bool truck = gr.uniform() < 0.55f;
            building(truck ? "props/pickup_parked.mbcad" : "props/sedan_parked.mbcad", x, z, yaw,
                     truck ? Vec3{1.0f, 0.9f, 2.9f} : Vec3{0.92f, 0.75f, 2.4f}, col3(c[0], c[1], c[2], "paint"));
            props.back().dirt = gr.range(0.05f, 0.35f);
        };

        // Gate and entrance: arch over the road, ticket booth, welcome-center trailer, gate staff.
        addProp("venue/gate_arch.mbcad", gatePos.x, gatePos.z, PI);
        boxCol({gatePos.x - 4.5f, 0, gatePos.z}, {0.3f, 2.8f, 0.3f}, 0);
        boxCol({gatePos.x + 4.5f, 0, gatePos.z}, {0.3f, 2.8f, 0.3f}, 0);
        building("venue/ticket_booth.mbcad", 6.f, -87.f, -PI * 0.5f, {1.2f, 1.4f, 1.2f});
        person(4.2f, -86.f, -PI * 0.5f, true);
        person(-3.8f, -89.f, PI * 0.5f, true);
        building("venue/office_trailer.mbcad", -9.f, -82.f, PI * 0.5f, {4.6f, 1.6f, 1.9f}, col3(0.9f, 0.89f, 0.84f, "color"));
        addProp("venue/sign.mbcad", 4.5f, -95.f, PI, 1.f, {{"label", 1}, {"arrow", 0}, {"panel_r", 0.85f}, {"panel_g", 0.4f}, {"panel_b", 0.08f}});
        addProp("venue/flag_pole.mbcad", -6.f, -93.f, 0, 1.f, col3(0.95f, 0.5f, 0.05f, "color"));
        addProp("venue/flag_pole.mbcad", 6.f, -93.f, 0, 1.f, col3(0.1f, 0.3f, 0.75f, "color"));

        // Visitor parking: rows of everyday pickups and cars, nose-in from the wheel tracks.
        for (float z = parkingMin.z + 3.f; z < parkingMax.z - 1.f; z += 8.f)
            for (float x = parkingMin.x + 2.f; x < parkingMax.x - 1.f; x += 3.2f) {
                if (gr.uniform() < 0.22f) continue;
                for (int side = -1; side <= 1; side += 2)
                    if (gr.uniform() < 0.8f) car(x + gr.range(-0.2f, 0.2f), z + side * 2.9f, (side < 0 ? 0.f : PI) + gr.range(-0.08f, 0.08f));
            }
        // Shops along the return road: pro shop/parts store, washroom block, first aid.
        building("venue/metal_building.mbcad", -42.f, -66.5f, 0, {5.f, 2.f, 4.f},
                 with(with(col3(0.55f, 0.12f, 0.1f, "wall"), col3(0.25f, 0.26f, 0.27f, "roof")), {{"w", 10}, {"l", 8}}));
        addProp("venue/sign.mbcad", -42.f, -61.5f, 0, 1.f, {{"label", 2}, {"arrow", 0}, {"panel_r", 0.1f}, {"panel_g", 0.1f}, {"panel_b", 0.12f}, {"w", 2.4f}});
        building("venue/metal_building.mbcad", -28.f, -66.f, 0, {4.f, 1.8f, 3.f},
                 with(with(col3(0.82f, 0.8f, 0.72f, "wall"), col3(0.18f, 0.32f, 0.2f, "roof")), {{"w", 8}, {"l", 6}, {"h", 3.f}}));
        building("venue/canopy.mbcad", -19.f, -66.f, 0, {1.5f, 1.4f, 1.5f}, {{"color_r", 0.92f}, {"color_g", 0.92f}, {"color_b", 0.92f}, {"walls", 1}});
        addProp("venue/sign.mbcad", -19.f, -63.5f, 0, 1.f, {{"label", 3}, {"arrow", 0}, {"panel_r", 0.85f}, {"panel_g", 0.1f}, {"panel_b", 0.1f}, {"w", 1.0f}, {"h", 0.6f}});
        person(-18.f, -64.5f, 0, true);
        for (int i = 0; i < 4; i++) building("venue/portajohn.mbcad", -33.5f + i * 1.3f, -71.5f, 0, {0.55f, 1.1f, 0.55f},
                                              col3(0.1f, 0.35f, 0.6f, "body"));
        addProp("venue/handwash.mbcad", -28.f, -71.5f, 0);

        // Village: food-truck row facing the walkway, pavilion, stage and lawn, concession, merch.
        const float trucks[][3] = {{0.85f, 0.2f, 0.12f}, {0.95f, 0.85f, 0.3f}, {0.2f, 0.45f, 0.65f}, {0.9f, 0.9f, 0.88f}};
        for (int i = 0; i < 4; i++) {
            float z = -84.f + i * 5.5f;
            building("venue/food_truck.mbcad", 10.f, z, PI * 0.5f, {3.0f, 1.5f, 1.25f}, col3(trucks[i][0], trucks[i][1], trucks[i][2], "body"));
            person(12.4f, z + gr.range(-1.f, 1.f), -PI * 0.5f, false);   // in the queue
            if (gr.uniform() < 0.6f) person(13.6f, z + gr.range(-1.f, 1.f), -PI * 0.5f, false);
        }
        addProp("venue/generator.mbcad", 7.5f, -88.f, 0);
        addProp("venue/water_tank.mbcad", 7.f, -64.5f, 0);
        building("venue/stage.mbcad", 22.f, -86.5f, 0, {5.f, 1.2f, 3.4f});
        building("venue/pavilion.mbcad", 22.f, -70.f, 0, {0.f, 0.f, 0.f});
        for (int i = 0; i < 4; i++) boxCol({22.f + (i % 2 ? 5.7f : -5.7f), 0, -70.f + (i < 2 ? 3.7f : -3.7f)}, {0.15f, 1.5f, 0.15f}, 0);
        for (int i = 0; i < 9; i++) person(gr.range(16.f, 28.f), gr.range(-81.f, -77.f), PI + gr.range(-0.4f, 0.4f), false);   // lawn crowd
        for (int i = 0; i < 6; i++) person(gr.range(17.f, 27.f), gr.range(-73.f, -67.f), gr.range(0.f, 6.28f), false);
        building("venue/metal_building.mbcad", 27.f, -64.f, 0, {3.5f, 1.6f, 2.5f},
                 with(with(col3(0.85f, 0.75f, 0.45f, "wall"), col3(0.5f, 0.12f, 0.1f, "roof")), {{"w", 7}, {"l", 5}, {"h", 2.8f}}));
        building("venue/canopy.mbcad", 15.f, -64.f, 0, {1.5f, 1.4f, 1.5f}, {{"color_r", 0.1f}, {"color_g", 0.12f}, {"color_b", 0.14f}});
        building("venue/canopy.mbcad", 19.f, -64.f, 0, {1.5f, 1.4f, 1.5f}, {{"color_r", 0.95f}, {"color_g", 0.5f}, {"color_b", 0.05f}});
        addProp("venue/folding_table.mbcad", 15.f, -63.f, 0);
        addProp("venue/folding_table.mbcad", 19.f, -63.f, 0);
        for (int i = 0; i < 4; i++) building("venue/portajohn.mbcad", 31.f, -82.f + i * 1.3f, -PI * 0.5f, {0.55f, 1.1f, 0.55f},
                                              col3(0.15f, 0.5f, 0.25f, "body"));
        addProp("venue/handwash.mbcad", 31.f, -76.f, -PI * 0.5f);
        building("venue/dumpster.mbcad", 31.f, -88.f, -PI * 0.5f, {0.9f, 0.65f, 0.6f});
        for (float x : {-20.f, 4.f, 16.f, 30.f}) addProp("venue/trash_barrel.mbcad", x, -60.5f, 0, 1.f, col3(0.1f, 0.3f, 0.6f, "body"));
        addProp("venue/light_tower.mbcad", 34.f, -60.f, 0);
        addProp("venue/light_tower.mbcad", -50.f, -62.5f, 0);

        // Pit-side: bleachers and the announcer tower on the north bank, results board at the finish,
        // sponsor banners on both fences, light towers on the south bank, a crowd on the bleachers.
        for (float cx : {-15.f, 15.f}) {
            building("venue/bleachers.mbcad", cx, -52.5f, 0, {6.f, 1.2f, 2.f}, {{"len", 12}});
            for (int row = 0; row < 5; row++)
                for (int i = 0; i < 7; i++)
                    if (gr.uniform() < 0.55f) person(cx - 5.4f + i * 1.8f + gr.range(-0.4f, 0.4f), -51.f - row * 0.75f, gr.range(-0.3f, 0.3f), false,
                                                     ground(cx, -52.5f) + 0.45f + row * 0.48f - 0.45f);
        }
        building("venue/announcer_tower.mbcad", 0.f, -53.f, 0, {1.6f, 3.f, 1.6f});
        // Start gantry over the lanes just before the pit, finish arch just past it (both face
        // the trucks coming from staging in the west); colliders on the towers/pillars only.
        addProp("venue/start_gantry.mbcad", pitX0 - 4.f, pitZ, -PI * 0.5f);
        addProp("venue/finish_arch.mbcad", pitX1 + 4.f, pitZ, -PI * 0.5f);
        for (float s : {-1.f, 1.f}) {
            boxCol({pitX0 - 4.f, 0, pitZ + s * 16.f}, {0.55f, 3.2f, 0.55f}, 0);
            boxCol({pitX1 + 4.f, 0, pitZ + s * 16.f}, {0.6f, 3.2f, 0.6f}, 0);
        }
        building("venue/results_board.mbcad", 30.f, -53.f, 0, {2.f, 2.5f, 0.3f});
        building("venue/results_board.mbcad", -62.f, -14.f, PI * 0.75f, {2.f, 2.5f, 0.3f});
        float fz2 = pitHalfW + 6.5f;
        for (float x = pitX0 - 2; x < pitX1; x += 13.f) {
            CadParams b1 = with(col3(0.95f, 0.75f, 0.1f, "a"), col3(0.08f, 0.08f, 0.08f, "b"));
            CadParams b2 = with(col3(0.85f, 0.12f, 0.1f, "a"), col3(0.95f, 0.95f, 0.95f, "b"));
            CadParams b3 = with(col3(0.1f, 0.35f, 0.75f, "a"), col3(0.95f, 0.8f, 0.2f, "b"));
            const CadParams* opts[] = {&b1, &b2, &b3};
            addProp("venue/banner.mbcad", x, pitZ - fz2 - 0.6f, PI, 1.f, *opts[gr.next() % 3]);
            addProp("venue/banner.mbcad", x + 6.f, pitZ + fz2 + 0.6f, 0, 1.f, *opts[gr.next() % 3]);
        }
        addProp("venue/light_tower.mbcad", -42.f, -4.f, PI);
        addProp("venue/light_tower.mbcad", 42.f, -4.f, PI);
        for (float x : {-38.f, 38.f}) { addProp("venue/trash_barrel.mbcad", x, -4.5f, 0, 1.f, col3(0.1f, 0.3f, 0.6f, "body")); }

        // Staging: cone lanes behind the start line, an official calling the next truck forward,
        // tech inspection canopy and registration on the paddock side.
        for (int l = 0; l <= lanes; l++) {
            float zl = pitZ - pitHalfW + l * (2 * pitHalfW / lanes);
            for (float x = pitX0 - 30.f; x <= pitX0 - 18.f; x += 4.f) addProp("venue/cone.mbcad", x, zl, gr.range(0, 6.28f));
        }
        person(pitX0 - 9.f, pitZ - pitHalfW - 2.f, PI * 0.5f + 0.6f, true);
        person(pitX0 - 9.f, pitZ + pitHalfW + 2.f, PI * 0.5f - 0.6f, true);
        building("venue/canopy.mbcad", -66.f, -48.f, PI * 0.5f, {1.5f, 1.4f, 1.5f}, {{"color_r", 0.1f}, {"color_g", 0.3f}, {"color_b", 0.7f}, {"walls", 1}});
        addProp("venue/folding_table.mbcad", -65.f, -48.f, PI * 0.5f);
        person(-64.5f, -47.f, PI * 0.5f, true);
        addProp("venue/sign.mbcad", -66.f, -45.f, PI * 0.5f, 1.f, {{"label", 4}, {"panel_r", 0.1f}, {"panel_g", 0.3f}, {"panel_b", 0.7f}, {"w", 1.4f}});
        for (int i = 0; i < 4; i++) addProp("venue/cone.mbcad", -62.f + (i % 2) * 5.f, -51.f + (i / 2) * 6.f, 0);
        building("venue/canopy.mbcad", -52.f, -64.f, 0, {1.5f, 1.4f, 1.5f}, {{"color_r", 0.95f}, {"color_g", 0.5f}, {"color_b", 0.05f}});
        addProp("venue/folding_table.mbcad", -52.f, -63.f, 0);
        person(-52.f, -64.f, 0, true);
        addProp("venue/sign.mbcad", -48.5f, -62.5f, 0, 1.f, {{"label", 5}, {"panel_r", 0.95f}, {"panel_g", 0.5f}, {"panel_b", 0.05f}, {"w", 1.4f}});

        // Recovery holding: loader tractor and the crew's tow truck by the pit's start corner.
        building("props/tractor_loader.mbcad", -50.f, -46.f, PI * 0.5f, {1.0f, 1.3f, 2.2f}, col3(0.2f, 0.45f, 0.15f, "body"));
        person(-47.5f, -44.5f, PI, true);

        // Shutdown area past the finish: cones funnel to the return road, sign, a flagger.
        for (int i = 0; i < 7; i++) {
            addProp("venue/cone.mbcad", 50.f + i * 4.f, -17.5f, 0);
            addProp("venue/cone.mbcad", 50.f + i * 4.f, -42.5f + std::max(0, i - 3) * 1.5f, 0);
        }
        addProp("venue/sign.mbcad", 74.f, -30.f, -PI * 0.5f, 1.f, {{"label", 6}, {"panel_r", 0.85f}, {"panel_g", 0.1f}, {"panel_b", 0.1f}, {"arrow", 1}});
        addProp("venue/sign.mbcad", 60.f, -44.f, -PI * 0.25f, 1.f, {{"label", 7}, {"panel_r", 0.1f}, {"panel_g", 0.4f}, {"panel_b", 0.15f}, {"arrow", 1}});
        person(52.f, -18.5f, -PI * 0.5f, true);
        for (int i = 0; i < 6; i++) addProp("venue/hay_bale.mbcad", 79.f, -38.f + i * 3.6f, PI * 0.5f, 1.f, {{"round", 0}});

        // Wash bay on the return road, with a hose tank and a crew washing.
        building("venue/washbay.mbcad", washCenter.x, washCenter.z, PI * 0.5f, {0.f, 0.f, 0.f}, {{"sign_r", 0.1f}, {"sign_g", 0.35f}, {"sign_b", 0.75f}});
        addProp("venue/water_tank.mbcad", washCenter.x + 6.5f, washCenter.z - 4.5f, 0);
        person(washCenter.x - 2.f, washCenter.z - 3.8f, 0, false);

        // Paddock: rows of tow rigs and trailers, canopies, pit clutter, crews working.
        int slot = 0, ownSpot = 0;
        for (float z : {-86.f, -76.f, -66.f})
            for (float x = paddockMin.x + 5.f; x < paddockMax.x - 4.f; x += 8.f, slot++) {
                if (std::fabs(x - garageSpot.x) < 6.f && std::fabs(z - garageSpot.z) < 7.f) {   // your own pit spot
                    if (ownSpot++) continue;
                    addProp("venue/canopy.mbcad", garageSpot.x - 1.5f, garageSpot.z - 4.6f, 0, 1.f, {{"color_r", 0.95f}, {"color_g", 0.55f}, {"color_b", 0.08f}, {"walls", 1}});
                    addProp("venue/pit_clutter.mbcad", garageSpot.x + 3.2f, garageSpot.z - 4.f, 0.3f);
                    continue;
                }
                float yaw = PI;   // trucks nose to the north lane, trailers behind them
                const float paints[][3] = {{0.9f, 0.9f, 0.9f}, {0.1f, 0.1f, 0.12f}, {0.6f, 0.08f, 0.06f}, {0.2f, 0.25f, 0.4f}, {0.75f, 0.75f, 0.72f}};
                const float* pc = paints[gr.next() % 5];
                building("props/dually_hauler.mbcad", x, z - 4.f, yaw, {1.2f, 1.0f, 3.3f}, col3(pc[0], pc[1], pc[2], "paint"));
                bool enclosed = gr.uniform() < 0.45f;
                if (enclosed) building("props/trailer_enclosed.mbcad", x, z + 4.2f, yaw, {1.3f, 1.5f, 3.8f},
                                       with(col3(0.18f, 0.18f, 0.2f, "body"), col3(pc[0], pc[1], pc[2], "stripe")));
                else building("props/trailer_flatbed.mbcad", x, z + 4.2f, yaw, {1.25f, 0.4f, 3.2f});
                // A competition rig unloaded beside it, under a canopy, with its crew.
                static const char* rigs[] = {"vehicles/squarebody.mbcad", "vehicles/fullsize_suv.mbcad", "vehicles/comanche.mbcad", "vehicles/bogcar76.mbcad"};
                static const float fa[] = {1.311f, 1.2105f, 1.29f, 1.36f}, ra[] = {-1.669f, -1.4795f, -1.58f, -1.73f},
                                   tr[] = {1.65f, 1.66f, 1.47f, 1.62f}, ay[] = {-0.42f, -0.42f, -0.42f, -0.36f};
                int r = int(gr.next() % 4);
                if (!enclosed || gr.uniform() < 0.5f) {
                    const float* rc = paints[gr.next() % 5];
                    float rx = x + 3.6f, rz = z + 1.f;
                    addParked(rigs[r], rx, rz, PI + gr.range(-0.2f, 0.2f), fa[r], ra[r], tr[r], ay[r] - 0.1f, 0.4445f, gr.range(0.3f, 0.9f),
                              col3(rc[0], rc[1], rc[2], "paint"));
                    boxCol({rx, 0, rz}, {1.0f, 0.9f, 2.4f}, PI);
                }
                if (gr.uniform() < 0.7f) addProp("venue/canopy.mbcad", x - 3.4f, z + 1.f, 0, 1.f, col3(gr.uniform(), gr.uniform(), gr.uniform(), "color"));
                addProp("venue/pit_clutter.mbcad", x - 3.4f, z + 1.f, gr.range(0, 6.28f), 1.f, col3(0.8f, 0.1f, 0.08f, "tool"));
                int crew = 1 + int(gr.next() % 3);
                for (int c = 0; c < crew; c++) person(x + gr.range(-4.5f, 4.5f), z + gr.range(-1.f, 2.5f), gr.range(0, 6.28f), false);
            }
        addProp("venue/light_tower.mbcad", -90.f, -60.f, PI * 0.25f);
        building("venue/dumpster.mbcad", -92.f, -72.f, PI * 0.5f, {0.9f, 0.65f, 0.6f});
        addProp("venue/generator.mbcad", -60.f, -88.f, 0);

        // Show-and-shine on the lawn west of the pond: clean rigs, hoods up, a few admirers.
        {
            static const char* rigs[] = {"vehicles/squarebody.mbcad", "vehicles/fullsize_suv.mbcad", "vehicles/comanche.mbcad"};
            static const float fa[] = {1.311f, 1.2105f, 1.29f}, ra[] = {-1.669f, -1.4795f, -1.58f}, tr[] = {1.65f, 1.66f, 1.47f};
            const float paints[][3] = {{0.75f, 0.1f, 0.08f}, {0.95f, 0.75f, 0.1f}, {0.1f, 0.35f, 0.6f}};
            for (int i = 0; i < 3; i++) {
                float x = -36.f + i * 6.f, z = 22.f;
                addParked(rigs[i], x, z, PI * 0.15f, fa[i], ra[i], tr[i], -0.52f, 0.4445f, 0.f, col3(paints[i][0], paints[i][1], paints[i][2], "paint"));
                boxCol({x, 0, z}, {1.0f, 0.9f, 2.4f}, PI * 0.15f);
                person(x + 2.2f, z + 1.f, -PI * 0.5f, false);
            }
            addProp("venue/banner.mbcad", -30.f, 27.5f, PI, 1.f, {{"len", 10}, {"a_r", 0.1f}, {"a_g", 0.1f}, {"a_b", 0.1f}, {"b_r", 0.85f}, {"b_g", 0.7f}, {"b_b", 0.2f}});
        }

        // Burnout pad: slab, tire-stack barriers, a small bleacher and crowd on the south side.
        {
            Vec3 c = burnoutCenter;
            addProp("venue/burnout_pad.mbcad", c.x, c.z, 0);
            props.back().pos.y = ground(c.x, c.z) - 0.02f;
            for (float x = -8.f; x <= 8.f; x += 2.f) {
                addProp("venue/tire_stack.mbcad", c.x + x, c.z - burnoutHalfZ - 1.3f, 0);
                addProp("venue/tire_stack.mbcad", c.x + x, c.z + burnoutHalfZ + 1.3f, 0);
            }
            building("venue/bleachers.mbcad", c.x, c.z + burnoutHalfZ + 5.f, PI, {6.f, 1.2f, 2.f}, {{"len", 12}});
            for (int i = 0; i < 14; i++) person(c.x + gr.range(-6.f, 6.f), c.z + burnoutHalfZ + gr.range(2.5f, 3.2f), PI + gr.range(-0.3f, 0.3f), false);
            addProp("venue/sign.mbcad", c.x - burnoutHalfX - 2.f, c.z + 1.f, -PI * 0.5f, 1.f, {{"label", 8}, {"arrow", 0}, {"panel_r", 0.1f}, {"panel_g", 0.1f}, {"panel_b", 0.1f}, {"w", 1.8f}});
            person(c.x - burnoutHalfX - 1.f, c.z - 2.f, PI * 0.5f, true);
        }
        // Bounty hole: prize board, viewing bank with bleachers and crowd, flags, hay bales.
        {
            Vec3 bc = bountyCenter();
            building("venue/bleachers.mbcad", bc.x, bountyZ - bountyHalfW - 7.f, 0, {6.f, 1.2f, 2.f}, {{"len", 12}});
            for (int i = 0; i < 18; i++) person(bc.x + gr.range(-13.f, 13.f), bountyZ - bountyHalfW - gr.range(4.5f, 5.5f), gr.range(-0.3f, 0.3f), false);
            for (int i = 0; i < 6; i++) person(bc.x - 8.f + i * 2.f, bountyZ - bountyHalfW - 6.6f, 0, false, ground(bc.x, bountyZ - bountyHalfW - 7.f) + 0.45f);
            building("venue/results_board.mbcad", bountyX0 - 4.f, bountyZ - bountyHalfW - 5.f, PI * 0.15f, {2.f, 2.5f, 0.3f});
            addProp("venue/sign.mbcad", bountyX0 - 6.f, bountyZ + 5.f, PI * 0.5f, 1.f, {{"label", 9}, {"arrow", 0}, {"panel_r", 0.85f}, {"panel_g", 0.65f}, {"panel_b", 0.1f}, {"w", 2.2f}, {"h", 1.1f}});
            for (float x = bountyX0; x <= bountyX1; x += 8.f) {
                addProp("venue/flag_pole.mbcad", x, bountyZ - bountyHalfW - 3.5f, 0, 1.f, col3(gr.uniform(), gr.uniform() * 0.6f, 0.1f, "color"));
                addProp("venue/hay_bale.mbcad", x + 2.f, bountyZ + bountyHalfW + 3.5f, gr.range(0, 6.28f), 1.f, {{"round", 1}});
            }
            addProp("venue/banner.mbcad", bc.x, bountyZ - bountyHalfW - 4.2f, 0, 1.f, {{"len", 8}, {"a_r", 0.85f}, {"a_g", 0.65f}, {"a_b", 0.1f}, {"b_r", 0.1f}, {"b_g", 0.1f}, {"b_b", 0.1f}});
            addProp("venue/light_tower.mbcad", bountyX1 + 4.f, bountyZ - 10.f, -PI * 0.6f);
            building("props/tractor_loader.mbcad", bountyX1 + 5.f, bountyZ + 4.f, -PI * 0.5f, {1.0f, 1.3f, 2.2f}, col3(0.85f, 0.55f, 0.08f, "body"));
        }

        // Campground: tents, RVs and campers with tow vehicles, tables, fire rings, a bathhouse.
        {
            building("venue/metal_building.mbcad", 106.f, 30.f, PI, {4.f, 1.8f, 3.f},
                     with(with(col3(0.62f, 0.5f, 0.36f, "wall"), col3(0.22f, 0.24f, 0.25f, "roof")), {{"w", 8}, {"l", 6}, {"h", 3.f}}));
            addProp("venue/sign.mbcad", 98.f, 6.f, PI * 0.5f, 1.f, {{"label", 10}, {"panel_r", 0.4f}, {"panel_g", 0.25f}, {"panel_b", 0.12f}});
            addProp("venue/sign.mbcad", 120.f, 36.f, PI, 1.f, {{"label", 11}, {"arrow", 0}, {"panel_r", 0.1f}, {"panel_g", 0.3f}, {"panel_b", 0.6f}, {"w", 1.0f}});   // RV dump station
            int site = 0;
            for (float z = 8.f; z < 34.f; z += 9.f)
                for (int side = -1; side <= 1; side += 2, site++) {
                    float x = 114.f + side * 8.f;
                    float yaw = side < 0 ? PI * 0.5f : -PI * 0.5f;   // fronts toward the camp road
                    int kind = int(gr.next() % 3);
                    const float stripes[][3] = {{0.6f, 0.15f, 0.1f}, {0.15f, 0.3f, 0.55f}, {0.35f, 0.3f, 0.2f}};
                    const float* sc = stripes[gr.next() % 3];
                    if (kind == 0) building("props/rv_motorhome.mbcad", x + side * 1.f, z, yaw + PI * 0.5f, {1.3f, 1.7f, 4.6f}, col3(sc[0], sc[1], sc[2], "stripe"));
                    else if (kind == 1) {
                        building("props/camper_trailer.mbcad", x + side * 1.f, z, yaw + PI * 0.5f, {1.25f, 1.5f, 3.6f}, col3(sc[0], sc[1], sc[2], "stripe"));
                        const float* pc = trucks[gr.next() % 4];
                        building("props/pickup_parked.mbcad", x + side * 1.f, z - 7.5f, yaw + PI * 0.5f, {1.0f, 0.9f, 2.9f}, col3(pc[0], pc[1], pc[2], "paint"));
                    } else {
                        addProp("venue/dome_tent.mbcad", x + side * 2.f, z - 1.5f, yaw, 1.f, col3(gr.uniform(), gr.uniform(), gr.uniform(), "color"));
                        addProp("venue/dome_tent.mbcad", x + side * 2.5f, z + 2.f, yaw + 0.3f, 1.f, col3(gr.uniform(), gr.uniform(), gr.uniform(), "color"));
                        car(x + side * 5.f, z, yaw + PI * 0.5f);
                    }
                    addProp("venue/picnic_table.mbcad", x - side * 3.5f, z + 2.5f, gr.range(0, 6.28f));
                    addProp("venue/fire_ring.mbcad", x - side * 3.5f, z - 2.f, 0);
                    addProp("venue/rv_hookup.mbcad", x - side * 4.8f, z + 4.f, yaw);
                    if (gr.uniform() < 0.6f) person(x - side * 3.f, z, gr.range(0, 6.28f), false);
                }
            addProp("venue/trash_barrel.mbcad", 110.f, 4.f, 0, 1.f, col3(0.1f, 0.3f, 0.15f, "body"));
            addProp("venue/handwash.mbcad", 102.f, 30.f, PI);
        }
    }

    // ---- Tree line (poplar/aspen in October, a few spruce) ----
    auto clearOfCourse = [&](float x, float z, float margin) {
        if (builtUp(x, z)) return false;
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
        if (std::fabs(x) < 7.f || std::fabs(z) < 7.f) continue;   // gaps out to the back-country
        bool spruce = rng.uniform() < (cluster ? 0.35f : 0.12f);
        float sc = rng.range(0.75f, 1.25f);
        if (spruce) addProp("vegetation/spruce.mbcad", x, z, rng.range(0, 6.28f), sc);
        else {
            // Quantized variants so trees share instanced batches.
            float yel = std::round(rng.range(0.55f, 1.f) * 4.f) / 4.f, ht = std::round(rng.range(7, 12) / 2.5f) * 2.5f;
            addProp("vegetation/poplar.mbcad", x, z, rng.range(0, 6.28f), sc, {{"yellow", yel}, {"h", ht}});
        }
        colliders.push_back({{x, ground(x, z) + 2, z}, {0.25f, 2, 0.25f}, 0});
    }
    for (int s = 0; s < 420; s++) {
        float x = rng.range(-90, 90), z = rng.range(-90, 90);
        if (!clearOfCourse(x, z, 1)) continue;
        addProp("vegetation/goldenrod.mbcad", x, z, rng.range(0, 6.28f), rng.range(0.7f, 1.3f));
    }
    // Barbed-wire perimeter posts (photo 1-2 foreground), with a gate gap.
    for (float x = -90; x <= 90; x += 4.5f)
        if (std::fabs(x) > 7.f) addProp("props/fencepost_wood.mbcad", x, -91.5f, rng.range(-0.2f, 0.2f));

    // ---- Back-country woods: patchy forest between the venue and the map edge ----
    {
        Rng fr(seed * 131u + 5u);
        const float lim = terrain.size() * 0.5f - 6.f;
        auto clearBack = [&](float x, float z) {
            for (size_t k = 0; k < trail.size(); k++) {
                const Vec3 &a = trail[k], &b = trail[(k + 1) % trail.size()];
                if (distToSegment({x, z}, {a.x, a.z}, {b.x, b.z}) < 5.5f) return false;
            }
            if (std::fabs(z - creekZ(x)) < creekHalfW + 2.5f) return false;
            if (distToSegment({x, z}, {chuteTop.x, chuteTop.z}, {chuteBottom.x, chuteBottom.z}) < 7.f) return false;
            float ex = (x - flatsCenter.x) / flatsHalfX, ez = (z - flatsCenter.z) / flatsHalfZ;
            if (ex * ex + ez * ez < 1.3f) return false;
            if (length(Vec2{x - swampCenter.x, z - swampCenter.z}) < swampRadius + 2.f) return false;
            if (std::fabs(x) < 8.f || std::fabs(z) < 8.f) return false;   // keep the way out open
            if (builtUp(x, z)) return false;
            return true;
        };
        for (int s = 0; s < 9000; s++) {
            float x = fr.range(-lim, lim), z = fr.range(-lim, lim);
            if (std::max(std::fabs(x), std::fabs(z)) < 100.f) continue;
            float dens = fbm(x * 0.012f + 4.f, z * 0.012f + 9.f, 3, seed + 33);
            if (fr.uniform() > smoothstep(0.42f, 0.62f, dens)) continue;
            if (!clearBack(x, z)) continue;
            bool spruce = fr.uniform() < 0.3f + 0.3f * smoothstep(0.55f, 0.7f, dens);
            float sc = fr.range(0.8f, 1.3f);
            if (spruce) addProp("vegetation/spruce.mbcad", x, z, fr.range(0, 6.28f), sc);
            else {
                float yel = std::round(fr.range(0.55f, 1.f) * 4.f) / 4.f, ht = std::round(fr.range(7, 12) / 2.5f) * 2.5f;
                addProp("vegetation/poplar.mbcad", x, z, fr.range(0, 6.28f), sc, {{"yellow", yel}, {"h", ht}});
            }
            colliders.push_back({{x, ground(x, z) + 2, z}, {0.25f, 2, 0.25f}, 0});
        }
        // Brush and goldenrod along trail edges and field margins.
        for (int s = 0; s < 1400; s++) {
            float x = fr.range(-lim, lim), z = fr.range(-lim, lim);
            if (std::max(std::fabs(x), std::fabs(z)) < 100.f || !clearBack(x, z)) continue;
            if (fr.uniform() < 0.8f) addProp("vegetation/goldenrod.mbcad", x, z, fr.range(0, 6.28f), fr.range(0.7f, 1.3f));
            else addProp("vegetation/sumac.mbcad", x, z, fr.range(0, 6.28f), fr.range(0.8f, 1.4f), {{"red", fr.uniform() < 0.5f ? 1.f : 0.25f}});
        }
        // A couple of abandoned rigs in the woods by the trail.
        auto nearTrail = [&](int k, float off) { const Vec3& a = trail[size_t(k)]; Vec3 n = normalize(Vec3{a.x, 0, a.z}); return a + n * off; };
        Vec3 r1 = nearTrail(14, 9.f), r2 = nearTrail(47, -9.f);
        addParked("vehicles/silverado.mbcad", r1.x, r1.z, 1.1f, 1.56f, -2.34f, 1.73f, -0.46f, 0.4826f, 0.95f, {{"paint_r", 0.42f}, {"paint_g", 0.16f}, {"paint_b", 0.12f}});
        boxCol(r1, {1.0f, 0.9f, 3.0f}, 1.1f);
        addParked("vehicles/comanche.mbcad", r2.x, r2.z, -0.6f, 1.29f, -1.58f, 1.47f, -0.42f, 0.4445f, 0.9f, {{"paint_r", 0.2f}, {"paint_g", 0.3f}, {"paint_b", 0.45f}});
        boxCol(r2, {0.9f, 0.8f, 2.3f}, -0.6f);
    }
    // Remember the venue pools' dry-weather levels so rain can raise them.
    rainPools.clear();
    poolRise = 0;
    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            Vec3 c = T.cellToWorld(i, j);
            if (c.x < -50 || c.x > 130 || c.z < -95 || c.z > 45) continue;
            int k = T.idx(i, j);
            if (T.water[k] > Terrain::NO_WATER + 1) rainPools.push_back({k, T.water[k]});
        }
    buildOverview();
}

void World::setRainRise(float r) {
    if (std::fabs(r - poolRise) < 0.01f) return;   // only re-upload in centimetre steps
    poolRise = r;
    for (auto& p : rainPools) {
        terrain.water[p.first] = p.second + r;
        terrain.dirty.add(p.first % Terrain::N, p.first / Terrain::N);
    }
}

void World::buildOverview() {
    const int M = overviewN;
    overview.assign(size_t(M) * M * 4, 255);
    const float S = terrain.size();
    for (int j = 0; j < M; j++)
        for (int i = 0; i < M; i++) {
            float x = terrain.originX() + (i + 0.5f) * S / M, z = terrain.originZ() + (j + 0.5f) * S / M;
            TerrainSample t = terrain.sample(x, z);
            float shade = clampf(0.85f + (terrain.heightAt(x + 1.f, z) - terrain.heightAt(x - 1.f, z)) * 0.25f, 0.6f, 1.15f);
            float r = 70, g = 98, b = 46;                                    // grass
            float mud = smoothstep(0.02f, 0.2f, t.mudDepth()) * smoothstep(0.4f, 0.7f, t.moisture);
            float bare = 1.f - t.grass;
            r = lerpf(r, 118, bare * 0.7f); g = lerpf(g, 100, bare * 0.7f); b = lerpf(b, 70, bare * 0.7f);
            r = lerpf(r, 72, mud); g = lerpf(g, 52, mud); b = lerpf(b, 34, mud);
            if (t.soil == uint8_t(Soil::Gravel)) { r = 150; g = 146; b = 136; }
            if (t.waterDepth > 0.03f) { r = 88; g = 104; b = 128; }
            size_t o = (size_t(j) * M + i) * 4;
            overview[o] = uint8_t(clampf(r * shade, 0, 255));
            overview[o + 1] = uint8_t(clampf(g * shade, 0, 255));
            overview[o + 2] = uint8_t(clampf(b * shade, 0, 255));
        }
    for (const PropInstance& p : props) {   // trees as dark dots
        if (p.part.find("poplar") == std::string::npos && p.part.find("spruce") == std::string::npos) continue;
        int i = int((p.pos.x - terrain.originX()) / S * M), j = int((p.pos.z - terrain.originZ()) / S * M);
        if (i < 0 || j < 0 || i >= M || j >= M) continue;
        size_t o = (size_t(j) * M + i) * 4;
        overview[o] = 38; overview[o + 1] = 56; overview[o + 2] = 30;
    }
    // Buildings and parked vehicles as their collider footprints (light grey roofs).
    for (const BoxCollider& c : colliders) {
        if (c.half.x < 0.5f) continue;
        float cs = std::cos(c.yaw), sn = std::sin(c.yaw);
        for (float a = -c.half.x; a <= c.half.x; a += 0.7f)
            for (float b = -c.half.z; b <= c.half.z; b += 0.7f) {
                float x = c.center.x + a * cs + b * sn, z = c.center.z - a * sn + b * cs;
                int i = int((x - terrain.originX()) / S * M), j = int((z - terrain.originZ()) / S * M);
                if (i < 0 || j < 0 || i >= M || j >= M) continue;
                size_t o = (size_t(j) * M + i) * 4;
                overview[o] = 196; overview[o + 1] = 192; overview[o + 2] = 184;
            }
    }
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
    // Venue boundary (the barbed-wire perimeter): soft wall that turns you around.
    const float lim = terrain.size() * 0.5f - 4.f;
    for (int ax = 0; ax < 3; ax += 2) {
        float& p = ax == 0 ? v.pos.x : v.pos.z;
        float& vel = ax == 0 ? v.vel.x : v.vel.z;
        if (std::fabs(p) > lim) {
            float s = signf(p);
            p = s * lim;
            if (vel * s > 0) vel = -vel * 0.3f;
        }
    }
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

bool World::builtUp(float x, float z) const {
    auto in = [&](Vec3 lo, Vec3 hi, float m) { return x > lo.x - m && x < hi.x + m && z > lo.z - m && z < hi.z + m; };
    if (in(paddockMin, paddockMax, 3) || in(parkingMin, parkingMax, 3) || in(villageMin, villageMax, 3) || in(shutdownMin, shutdownMax, 2)) return true;
    if (in(campMin, campMax, 6)) return true;
    if (in({bountyX0 - 16, 0, bountyZ - 20}, {bountyX1 + 6, 0, bountyZ + 12}, 0)) return true;
    if (in({-12, 0, -130}, {12, 0, -56}, 0)) return true;              // gate + entrance road
    if (in({-60, 0, -62}, {48, 0, -50}, 0)) return true;               // return road, bleachers
    if (x > 30 && x < 86 && std::fabs(z + 56.f) < 5.f) return true;    // path to the bounty hole
    if (z > -6 && z < 6 && x > 78 && x < 100) return true;             // path to the campground
    if (x > -40 && x < -10 && z > 16 && z < 30) return true;           // show-and-shine lawn
    if (std::fabs(x - burnoutCenter.x) < burnoutHalfX + 6 && std::fabs(z - burnoutCenter.z) < burnoutHalfZ + 9) return true;
    return false;
}

}  // namespace mb
