// The Mud Buddies venue, laid out from the reference photos: an excavated drag bog pit with
// spoil berms, orange safety fence on green T-posts, sponsor barrels, spectators and a dozer;
// a mowed two-track circuit with a water hole and the brush mound with the abandoned bus;
// a freestyle mud bowl; a poplar/spruce tree line and goldenrod/teasel along the fences.
#pragma once
#include <string>
#include <vector>
#include "../cad/cad.h"
#include "../physics/vehicle.h"
#include "../terrain/terrain.h"

namespace mb {

struct PropInstance {
    std::string part;
    CadParams params;
    Vec3 pos;
    float yaw = 0;
    float scale = 1;
};

struct Barrel {
    Vec3 pos, vel;
    float yaw = 0, tilt = 0, tiltVel = 0, spin = 0;
    int variant = 0;       // 0 = black/yellow sponsor drum, 1 = faded blue
    bool knocked = false;
};

struct FencePost {
    Vec3 base;
    float lean = 0;        // radians, plastic deformation from impacts
    Vec3 leanDir{0, 0, 1};
    Vec3 top() const;
};

struct BoxCollider {
    Vec3 center, half;     // half extents in local (yaw-rotated) frame
    float yaw = 0;
};

struct WorldEvents {
    std::vector<Vec3> barrelHits, fenceHits;
    std::vector<float> barrelStrength;
    int barrelsKnocked = 0;
    void clear() { barrelHits.clear(); fenceHits.clear(); barrelStrength.clear(); barrelsKnocked = 0; }
};

class World {
public:
    void generate(uint32_t seed);
    void stepDynamics(float dt, std::vector<Vehicle*>& vehicles, WorldEvents& ev);
    void collideStatic(Vehicle& v, float dt);
    void resetDynamics();

    Terrain terrain;
    std::vector<PropInstance> props;
    std::vector<Barrel> barrels, barrelsInitial;
    std::vector<FencePost> posts;
    std::vector<std::pair<int, int>> fenceSegs;
    std::vector<BoxCollider> colliders;

    // Course geometry
    float pitX0 = -40, pitX1 = 40, pitZ = -30, pitHalfW = 13;
    int lanes = 4;
    float laneZ(int lane) const { return pitZ - pitHalfW + (lane + 0.5f) * (2 * pitHalfW / lanes); }
    std::vector<Vec3> circuit;      // closed loop of checkpoints
    Vec3 pondCenter{0, 0, 32};
    float pondRadius = 9;
    Vec3 bowlCenter{60, 0, -66};
    float bowlRadius = 17;
    float kingRadius = 6;
    Vec3 garageSpot{-70, 0, -62};
    Vec3 dozerPos{47, 0, -40};
    Vec3 crowdCenter{0, 0, -8};
    Vec3 busPos{-55, 0, 58};
};

}  // namespace mb
