#include "challenges.h"
#include <cmath>

namespace mb {

void Challenges::reset() {
    goals = {
        {"hill", "King of the Hill", "Get to the top of the hill in the south-east (the mud chute is the hard way)", 300, false},
        {"swamp", "Peat Swamp Crossing", "Drive clean across the black peat swamp in the south-west", 400, false},
        {"creek", "Ford the Creek", "Cross the creek along the south edge of the park through the water", 150, false},
        {"flats", "Sand Flats Speed Run", "Hold 35+ mph for 3 s on the western sand flats", 200, false},
        {"trail", "Trail Loop", "Reach all four corners of the woods trail loop", 500, false},
    };
    inSwamp = false;
    creekSide = 0;
    trailMask = 0;
    flatsTimer = 0;
}

int Challenges::complete(int i) {
    if (goals[size_t(i)].done) return -1;
    goals[size_t(i)].done = true;
    lastReward = goals[size_t(i)].pay;
    lastName = goals[size_t(i)].name;
    rewardSerial++;
    return i;
}

int Challenges::update(const World& w, const Vehicle& v, float dt) {
    const Vec3 p = v.pos;
    auto flat = [](Vec3 a, Vec3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); };
    // Hill: at the crown, upright, on the wheels.
    if (flat(p, w.hillCenter) < 7.f && v.wheelsInContact >= 3 && v.up().y > 0.8f) { int r = complete(0); if (r >= 0) return r; }
    // Swamp: enter, leave on the far side (exit at least ~1.5 radii from where you went in).
    float ds = flat(p, w.swampCenter);
    if (!inSwamp && ds < w.swampRadius - 1.f) { inSwamp = true; swampEntry = p; }
    else if (inSwamp && ds > w.swampRadius + 1.f) {
        inSwamp = false;
        if (flat(p, swampEntry) > w.swampRadius * 1.5f) { int r = complete(1); if (r >= 0) return r; }
    }
    // Creek: change sides of the channel while actually in it.
    float rel = p.z - w.creekZ(p.x);
    if (std::fabs(rel) < w.creekHalfW + 6.f && std::fabs(p.x) < 180.f) {
        int side = rel > 0 ? 1 : -1;
        if (creekSide != 0 && side != creekSide && std::fabs(rel) < w.creekHalfW) { int r = complete(2); if (r >= 0) { creekSide = side; return r; } }
        if (std::fabs(rel) > w.creekHalfW) creekSide = side;
    } else creekSide = 0;
    // Flats: sustained speed inside the sand ellipse.
    float ex = (p.x - w.flatsCenter.x) / w.flatsHalfX, ez = (p.z - w.flatsCenter.z) / w.flatsHalfZ;
    flatsTimer = (ex * ex + ez * ez < 1.f && v.speed() > 15.6f) ? flatsTimer + dt : 0.f;
    if (flatsTimer > 3.f) { int r = complete(3); if (r >= 0) return r; }
    // Trail: the four compass points of the loop, any order.
    if (!w.trail.empty()) {
        size_t n = w.trail.size();
        for (unsigned q = 0; q < 4; q++)
            if (flat(p, w.trail[q * n / 4]) < 9.f) trailMask |= 1u << q;
        if (trailMask == 15u) { int r = complete(4); if (r >= 0) return r; }
    }
    return -1;
}

}  // namespace mb
