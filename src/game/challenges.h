// Open Pits challenges: paid goals out in the back-country for free roam (career income).
#pragma once
#include <string>
#include <vector>
#include "../physics/vehicle.h"
#include "world.h"

namespace mb {

class Challenges {
public:
    struct Goal { const char* id; const char* name; const char* hint; int pay; bool done; };
    std::vector<Goal> goals;
    int rewardSerial = 0, lastReward = 0;   // the HUD/JS pays out when the serial changes
    std::string lastName;

    Challenges() { reset(); }
    void reset();
    // Returns the index of a goal completed this step, or -1.
    int update(const World& w, const Vehicle& v, float dt);

private:
    bool inSwamp = false;
    Vec3 swampEntry;
    int creekSide = 0;
    unsigned trailMask = 0;
    float flatsTimer = 0;
    int complete(int i);
};

}  // namespace mb
