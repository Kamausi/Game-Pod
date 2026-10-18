#include "recovery.h"
#include <cmath>
#include <cstdio>
#include "../physics/terramechanics.h"

namespace mb {

Vec3 Recovery::hookPoint(const Vehicle& v) const {
    return v.pos + v.rot.rotate({0, v.body->bellyY + 0.25f, v.body->dims[2] * 0.5f});
}

// Read the section ahead before committing: water, soft depth, firm bottom, soil, your ground pressure.
std::string Recovery::scout(const World& w, const Vehicle& v) const {
    Vec3 f = v.forward();
    f = normalize(Vec3{f.x, 0, f.z});
    float maxWater = 0, maxSoft = 0, maxRise = 0;
    int counts[int(Soil::Count)] = {0};
    float h0 = w.terrain.heightAt(v.pos.x, v.pos.z);
    for (float d = 3.f; d <= 16.f; d += 0.5f) {
        Vec3 p = v.pos + f * d;
        TerrainSample s = w.terrain.sample(p.x, p.z);
        maxWater = std::max(maxWater, s.waterDepth);
        float soft = s.moisture > 0.45f ? s.mudDepth() : 0.f;
        maxSoft = std::max(maxSoft, soft);
        maxRise = std::max(maxRise, s.height - h0);
        if (soft > 0.05f || s.waterDepth > 0.05f) counts[s.soil]++;
    }
    int soil = 0;
    for (int k = 1; k < int(Soil::Count); k++)
        if (counts[k] > counts[soil]) soil = k;
    // Average contact pressure of one tire at the current pressure (supported weight / footprint).
    const WheelState& wh = v.wheels[0];
    float load = v.mass * 9.81f * 0.25f;
    float kTire = 1.6e5f * std::pow(v.psi / 25.f, 0.85f);
    float defl = std::min(load / kTire, 0.5f * std::max(0.08f, wh.radius - 0.2f));
    float L = 2.f * std::sqrt(std::max(1e-4f, 2.f * wh.radius * defl - defl * defl));
    float kpa = load / std::max(0.01f, wh.width * L) / 1000.f;
    float reach = wh.radius * 0.8f;   // how deep a tire can sink before the axle/belly drags
    char b[240];
    if (maxSoft < 0.05f && maxWater < 0.05f) {
        std::snprintf(b, sizeof b, "SCOUT: firm ground ahead%s · your tires ~%.0f kPa", maxRise > 1.2f ? " (steep climb)" : "", kpa);
    } else {
        std::snprintf(b, sizeof b, "SCOUT: %s · water %.1f m · soft %.1f m · %s · tires ~%.0f kPa", soilMods(uint8_t(soil)).name,
                      maxWater, maxSoft, maxSoft > reach ? "NO firm bottom in reach" : "firm bottom within reach", kpa);
    }
    return b;
}

std::string Recovery::startShovel(const Vehicle& v) {
    if (v.speed() > 1.0f) return "Stop before you grab the shovel";
    if (digTimer > 0) return "";
    digTimer = 5.f;
    return "Digging out... clear the tires and the belly (5 s)";
}

// Lay two boards in front of the tires on the axle that will climb onto them first.
std::string Recovery::placeBoards(const World& w, Vehicle& v) {
    if (v.speed() > 1.2f) return "Stop before placing traction boards";
    if (boardsCarried < 2) return boardsCarried == 0 ? "No boards left - drive clear to pick them up" : "Need both boards back first";
    bool reverse = v.gear < 0;
    Vec3 f = v.forward();
    f = normalize(Vec3{f.x, 0, f.z});
    Vec3 dir = reverse ? f * -1.f : f;
    // Under the tires that are doing the pulling: the rear on a 2WD (or reversing), the front on a 4WD.
    bool rear = reverse || !v.fourWD || v.frontShaftBroken;
    for (int side = 0; side < 2; side++) {
        int wi = rear ? 2 + side : side;
        const WheelState& wh = v.wheels[size_t(wi)];
        // Shoved hard against the tread so the tire grabs the leading edge straight away.
        Vec3 c = wh.worldCenter + dir * (SupportPad{}.halfL - 0.12f);
        SupportPad p;
        p.center = {c.x, 0, c.z};
        p.dir = dir;
        // Wedged: the back end sits down in the tire's hole, the front end on the ground ahead.
        Vec3 back = c - dir * p.halfL, front = c + dir * p.halfL;
        float hb = std::min(w.terrain.heightAt(back.x, back.z), wh.contactPos.y) + 0.03f;
        float hf = w.terrain.heightAt(front.x, front.z) + 0.03f;
        hb = std::max(hb, hf - 0.45f * 2.f * p.halfL);   // a board can only be angled so steeply
        p.top = 0.5f * (hb + hf);
        p.slope = (hf - hb) / (2.f * p.halfL);
        p.center.y = p.top;
        pads.push_back(p);
    }
    boardsCarried = 0;
    return rear ? "Boards under the rear tires. Ease on - gentle throttle, don't spin on them"
                : "Boards under the front tires. Ease on - gentle throttle, don't spin on them";
}

std::string Recovery::toggleWinch(const World& w, const Vehicle& v) {
    if (!v.cfg.winch) return "No winch fitted (add one in the garage)";
    if (winch.broken) return "Winch line is broken - repair in the garage";
    if (winch.active) { winch.active = false; return "Winch spooled off"; }
    if (winch.heat > 0.85f) return "Winch motor still too hot";
    Vec3 hook = hookPoint(v);
    Vec3 f = normalize(Vec3{v.forward().x, 0, v.forward().z});
    float best = 1e9f;
    for (const PropInstance& p : w.props) {
        bool tree = p.part.find("poplar") != std::string::npos || p.part.find("spruce") != std::string::npos;
        bool post = p.part.find("fencepost") != std::string::npos;
        bool rig = p.part.find("vehicles/") != std::string::npos || p.part.find("dozer") != std::string::npos;
        if (!tree && !post && !rig) continue;
        Vec3 d{p.pos.x - hook.x, 0, p.pos.z - hook.z};
        float dist = length(d);
        if (dist < 4.f || dist > 38.f) continue;
        if (dot(d * (1.f / dist), f) < 0.4f) continue;   // in front, within ~65 degrees
        float score = dist * (post ? 1.3f : 1.f);        // posts are a weak anchor: prefer trees/rigs
        if (score < best) {
            best = score;
            winch.anchor = p.pos + Vec3{0, 0.5f, 0};
            winch.anchorName = tree ? "a tree" : (post ? "a fence post" : "a parked rig");
        }
    }
    if (best > 1e8f) return "No anchor in reach ahead (tree, post or rig within 38 m)";
    winch.active = true;
    winch.lineLen = std::min(WinchState::kRope, length(winch.anchor - hook) + 0.3f);
    return "Winch line on " + winch.anchorName + ". Keep clear of the line - pulling";
}

std::string Recovery::stepWinch(Vehicle& v, float dt) {
    v.extForce = {};
    if (!winch.active) {
        winch.heat = std::max(0.f, winch.heat - dt / 120.f);
        winch.force = 0;
        return "";
    }
    Vec3 hook = hookPoint(v);
    Vec3 d = winch.anchor - hook;
    float dist = length(d);
    if (dist < 3.5f) { winch.active = false; return "Winched up to the anchor"; }
    Vec3 dirN = d * (1.f / dist);
    // More rope on the drum = bigger effective drum radius = less pull (about 9% per layer).
    float spooled = WinchState::kRope - winch.lineLen;
    int layer = 1 + int(std::max(0.f, spooled) / 7.f);
    float fmax = WinchState::kRated * (1.f - 0.09f * float(layer - 1));
    float stretch = dist - winch.lineLen;
    float sepRate = -dot(v.vel, dirN);   // >0: truck moving away from the anchor
    float demand = stretch > 0 ? 90000.f * stretch + 8000.f * std::max(0.f, sepRate) : 0.f;
    if (demand > fmax * 1.7f) {   // shock load: driving against the line
        winch.active = false;
        winch.broken = true;
        return "LINE SNAPPED - never jerk a vehicle against a winch line";
    }
    float F = std::min(demand, fmax);
    winch.force = F;
    v.extForce = dirN * F;
    v.extPoint = hook;
    // Reel in: fast unloaded, slow near rated pull.
    float lineSpeed = 0.55f * (1.f - 0.65f * F / fmax);
    winch.lineLen = std::max(0.f, winch.lineLen - lineSpeed * dt);
    winch.heat += dt * (0.15f + F / fmax) / 50.f;
    if (winch.heat >= 1.f) { winch.active = false; return "Winch motor overheated - let it cool before the next pull"; }
    return "";
}

std::string Recovery::update(World& w, Vehicle& v, float dt) {
    std::string msg;
    if (digTimer > 0) {
        if (v.speed() > 1.5f) { digTimer = 0; return "Shovelling interrupted"; }
        digTimer -= dt;
        if (digTimer <= 0) {
            // Clear a ramp in front of each tire (direction of last travel) and the material under the belly.
            Vec3 f = normalize(Vec3{v.forward().x, 0, v.forward().z});
            Vec3 dir = v.gear < 0 ? f * -1.f : f;
            for (auto& wh : v.wheels) {
                Vec3 c = wh.worldCenter + dir * (wh.radius * 0.6f);
                c.y = w.terrain.heightAt(c.x, c.z);
                w.terrain.digPatch(c, dir, 0.55f, wh.width * 0.6f, 0.18f);
                wh.pack = 0;
            }
            Vec3 c = v.pos;
            c.y = w.terrain.heightAt(c.x, c.z);
            w.terrain.digPatch(c, f, v.body->dims[2] * 0.42f, v.body->dims[0] * 0.3f, 0.2f);
            msg = "Dug out: ramps cleared in front of the tires and under the belly";
        }
    }
    // Boards: retrieve once the truck is clear of them; chewed boards are gone.
    bool any = false;
    for (auto it = pads.begin(); it != pads.end();) {
        if (it->wear >= 1.f) { it = pads.erase(it); msg = "Chewed up a traction board - don't spin on them"; continue; }
        any = true;
        ++it;
    }
    if (any) {
        bool clear = true;
        for (const SupportPad& p : pads)
            if (length(Vec3{p.center.x - v.pos.x, 0, p.center.z - v.pos.z}) < v.body->dims[2] * 0.5f + 3.f) clear = false;
        if (clear) {
            boardsCarried += int(pads.size());
            pads.clear();
            if (msg.empty()) msg = "Traction boards retrieved";
        }
    }
    return msg;
}

}  // namespace mb
