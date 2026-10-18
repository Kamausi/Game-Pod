// Vehicle catalog + customization. Real-world reference figures are documented in
// docs/FORENSICS.md (vehicles identified in the reference photos).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../core/math.h"

namespace mb {

enum class Tread : uint8_t { AllTerrain = 0, MudTerrain = 1, Paddle = 2 };

struct EngineSpec {
    std::string id, name;
    float idleRpm, redlineRpm, stallRpm, inertia;   // inertia kg*m^2 (crank + flywheel)
    std::vector<float> rpmPts, torquePts;           // torque curve (N*m)
    int cylinders;
    std::vector<float> firingDeg;                   // firing angle of each cylinder in the 720 deg cycle
    std::vector<int> bank;                          // exhaust bank of each cylinder (0/1)
    float pipeLen[2];                               // exhaust waveguide length per bank (m)
    float openness;                                 // 0 = muffled, 1 = open headers
    bool diesel = false, turbo = false, supercharged = false;
    float mass;                                     // kg added to chassis
    float torqueAt(float rpm) const;
    float peakTorque() const;
};

struct BodySpec {
    std::string id, name, cadPart;
    float mass;            // kg incl. driver, excl. engine
    float wheelbase, track;
    float axleY;           // wheel-center height in body frame at static ride (m, negative = below origin)
    float cgY;             // CG offset above body origin
    float dims[3];         // inertia box (w, h, l)
    float cdA;             // drag area (m^2)
    float intakeY, intakeZ;// intake location (body frame)
    float bellyY;          // underside of frame/body (body frame)
    float travel;          // suspension travel (m)
    float defaultTireIn;
    std::string defaultEngine;
    bool default4wd;
    float frontBias;       // static front weight fraction
    float paint[3];
};

// Differential per axle (field guide 5): open, limited-slip, selectable locker, spool/welded.
enum class Diff : uint8_t { Open = 0, Limited, Locker, Spool };
const char* diffName(Diff d);

struct VehicleConfig {
    std::string name = "Buddy";
    std::string body = "comanche";
    std::string engine = "i6_40";
    float tireIn = 35.f;      // diameter (in)
    float widthIn = 12.5f;    // section width (in)
    Tread tread = Tread::MudTerrain;
    float liftIn = 4.f;
    float axleRatio = 4.56f;
    bool lockFront = false, lockRear = true, fourWD = true, snorkel = false;  // lock*: locker engaged at start
    Diff diffF = Diff::Open, diffR = Diff::Locker;
    bool beadlock = false;   // bead-lock rings: safe to air down below ~8 psi
    bool btc = false;        // brake traction control (electronic)
    bool winch = false;      // front winch fitted
    float psi = 18.f;
    float paint[3] = {0.36f, 0.38f, 0.36f};
    std::string serialize() const;
    static VehicleConfig parse(const std::string& s);
};

const std::vector<EngineSpec>& engineCatalog();
const std::vector<BodySpec>& bodyCatalog();
const EngineSpec& findEngine(const std::string& id);
const BodySpec& findBody(const std::string& id);
const char* treadName(Tread t);

// Garage spec sheet (field guide 6, 7, 14): the numbers a builder works from.
struct SpecSheet {
    float crawlRatio;      // 1st x low range x axle
    float effRatio;        // axle ratio as the stock tire would feel it
    float regearTarget;    // standard ratio that restores the stock effective gearing
    float diffClearIn;     // ground to the bottom of the diff housing (in)
    float groundKpa;       // average tire contact pressure at the set psi
    float lbPerHp;
    const char* cls;       // Stock / Modified / Pro
    std::string json() const;
};
SpecSheet specSheet(const VehicleConfig& c);

// Gearbox (3-speed auto like a TH400 / A904 family, plus 2.72 transfer-case low range).
struct Gearbox {
    std::vector<float> ratios{2.48f, 1.48f, 1.0f};
    float reverse = 2.08f;
    float lowRange = 2.72f;
};

}  // namespace mb
