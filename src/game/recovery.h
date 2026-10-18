// Self-recovery kit (field guide sections 3, 9, 10): scout the line ahead, dig out with a shovel,
// lay traction boards, or winch to an anchor. The dozer tow stays as the last resort.
#pragma once
#include <string>
#include <vector>
#include "../physics/vehicle.h"
#include "world.h"

namespace mb {

struct WinchState {
    bool active = false, broken = false;
    Vec3 anchor;
    std::string anchorName;
    float lineLen = 0;      // paid-out rope (m)
    float heat = 0;         // motor heat 0..1 (duty cycle)
    float force = 0;        // current line pull (N)
    static constexpr float kRope = 30.f;       // m of rope on the drum
    static constexpr float kRated = 42000.f;   // N first-layer line pull (~9,500 lb)
};

class Recovery {
public:
    std::vector<SupportPad> pads;   // traction boards on the ground
    int boardsCarried = 2;
    float digTimer = 0;             // > 0 while shovelling
    WinchState winch;

    void reset() { pads.clear(); boardsCarried = 2; digTimer = 0; winch = WinchState{}; }

    // Each returns a message for the HUD (empty if nothing to say).
    std::string scout(const World& w, const Vehicle& v) const;
    std::string startShovel(const Vehicle& v);
    std::string placeBoards(const World& w, Vehicle& v);
    std::string toggleWinch(const World& w, const Vehicle& v);

    // Per physics step: winch pull into v.extForce. Returns a message on events (snap, overheat...).
    std::string stepWinch(Vehicle& v, float dt);
    // Per frame: shovel progress, board wear/retrieval. Returns a message on events.
    std::string update(World& w, Vehicle& v, float dt);

    Vec3 hookPoint(const Vehicle& v) const;
};

}  // namespace mb
