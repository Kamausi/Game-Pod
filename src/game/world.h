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
    float dirt = 0;  // baked-on mud (0..1), drawn by the same shader as the players' trucks
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
    Vec3 bowlCenter{64, 0, -68};
    float bowlRadius = 17;
    float kingRadius = 6;
    Vec3 garageSpot{-70, 0, -62};
    Vec3 dozerPos{47, 0, -40};
    Vec3 crowdCenter{0, 0, -8};
    Vec3 busPos{-55, 0, 58};

    // Back-country beyond the venue (the map is 384 m across; the venue is the middle 190 m).
    std::vector<Vec3> trail;        // closed two-track loop through the woods
    Vec3 hillCenter{128, 0, -128};
    float hillRadius = 50, hillHeight = 16;
    Vec3 chuteTop, chuteBottom;     // muddy hill-climb chute down the hill's venue-facing side
    Vec3 flatsCenter{-138, 0, -18};
    float flatsHalfX = 36, flatsHalfZ = 62;
    float creekZ(float x) const { return 150.f + 10.f * std::sin(x * 0.035f) + 6.f * std::sin(x * 0.011f + 1.f); }
    float creekHalfW = 4.f;
    Vec3 swampCenter{-120, 0, 104};   // black peat muck: the "bottomless" hole
    float swampRadius = 17.f;

    // Venue grounds (north = -z): gate and entrance road from the north edge, visitor parking and
    // the village (food, shops, stage) north of the pit, bleachers and announcer on the pit's north
    // side, paddock/trailer parking in the north-west, staging at the pit's west end, shutdown area
    // past the finish, and a return road behind the bleachers back to the paddock via the wash bay.
    std::vector<Vec3> entranceRoad, returnRoad, campRoad;
    Vec3 gatePos{0, 0, -91.5f};
    Vec3 washCenter{-40, 0, -58};   // wash bay pad (drive on, stop: pressure washer)
    float washHalfX = 4.5f, washHalfZ = 3.0f;
    Vec3 paddockMin{-94, 0, -90}, paddockMax{-56, 0, -62};
    Vec3 parkingMin{-48, 0, -90}, parkingMax{-12, 0, -70};
    Vec3 villageMin{5, 0, -90}, villageMax{33, 0, -62};
    Vec3 shutdownMin{47, 0, -44}, shutdownMax{78, 0, -16};
    // Bounty hole east of the venue: one long, bottomless clay trench with a prize on it.
    float bountyX0 = 88, bountyX1 = 120, bountyZ = -50, bountyHalfW = 7;
    Vec3 bountyCenter() const { return {0.5f * (bountyX0 + bountyX1), 0, bountyZ}; }
    // Weekend campground through the east tree-line gap.
    Vec3 campMin{98, 0, -4}, campMax{130, 0, 38};
    bool builtUp(float x, float z) const;   // grounds where no brush/trees grow
    // Overview image for the minimap (RGBA, overviewN^2, north = -z up).
    std::vector<uint8_t> overview;
    int overviewN = 256;
    void buildOverview();
};

}  // namespace mb
