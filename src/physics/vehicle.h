// Sim-cade vehicle: 6-DOF rigid chassis, quarter-car suspension (unsprung wheel mass, series tire
// spring, bump/rebound damping, anti-roll bars), terramechanics tire
// model blended with a Pacejka model for firm ground, torque-converter drivetrain with open /
// locked differentials and a part-time transfer case, engine thermals, hydrolock and tread
// packing. Runs at a fixed 240 Hz.
#pragma once
#include <array>
#include <vector>
#include "../core/math.h"
#include "../terrain/terrain.h"
#include "specs.h"

namespace mb {

struct VehicleInput {
    float throttle = 0, brake = 0, steer = 0;  // steer: +1 = left
    bool handbrake = false;
    bool shiftUp = false, shiftDown = false;   // edge-triggered (manual mode)
    float airDelta = 0;                         // -1 deflate, +1 inflate (held)
};

struct WheelState {
    Vec3 mount;            // body-frame suspension top
    bool front = false, left = false;
    float radius = 0.44f, width = 0.32f, inertia = 4.f;
    float susLen = 0, compression = 0, prevCompression = 0;
    // Quarter-car state: the wheel is a mass sliding on the suspension axis (susVel = d susLen/dt)
    // and the tire is a second, pressure-dependent spring between wheel and ground.
    float susVel = 0, unsprung = 70.f, springK = 40000.f, bumpC = 2000.f, reboundC = 4000.f;
    float tirePen = 0, prevTirePen = 0;
    float nomLoad = 4000.f;   // static corner load, reference for tire load sensitivity
    float omega = 0, spin = 0, steer = 0;
    bool contact = false;
    float load = 0, fx = 0, fy = 0;
    float slipRatio = 0, slipAngle = 0, slipSpeed = 0;
    float mudW = 0, moisture = 0, waterDepth = 0, rutWall = 0;
    float pack = 0;         // tread packed with clay 0..1
    float ejectVolume = 0;  // m^3 excavated this frame (for particles/score)
    float driveTorque = 0;
    Vec3 contactPos, contactNormal{0, 1, 0}, fwdDir{0, 0, 1};
    Vec3 worldCenter;
};

struct VehicleEvents {
    float impact = 0;        // strongest chassis impact this frame (N*s-ish)
    Vec3 impactPos;
    float bellyScrape = 0;   // 0..1
    float splash = 0;        // water entry intensity
    Vec3 splashPos;
    int limiterHits = 0, backfires = 0, shifts = 0;
    bool hydrolocked = false;
    void clear() { *this = VehicleEvents{}; }
};

class Vehicle {
public:
    static constexpr float DT = 1.f / 240.f;

    void configure(const VehicleConfig& cfg);
    void reset(Vec3 pos, float yaw);
    // After reset(): sit the truck on the ground at ride height, pitched and rolled to the slope.
    void placeOnGround(const Terrain& terrain);
    void step(Terrain& terrain, const VehicleInput& in, float dt);

    // State
    VehicleConfig cfg;
    const BodySpec* body = nullptr;
    const EngineSpec* engine = nullptr;
    Gearbox gearbox;
    float mass = 1500, finalDrive = 4.56f;
    Vec3 inertia{1, 1, 1};
    Vec3 pos, vel, angVel;
    Quat rot;
    std::array<WheelState, 4> wheels;  // FL, FR, RL, RR
    float springK = 40000, damperC = 2800, travel = 0.25f, rideAxleY = -0.4f;
    float sprungMass = 1300;      // chassis mass the springs carry (mass minus four wheel ends)
    float arbFront = 0, arbRear = 0;  // anti-roll bar rates (N/m of left-right compression difference)
    Vec3 lastAccel;               // chassis non-gravity acceleration from the previous step

    // Drivetrain
    int gear = 1;            // -1 reverse, 0 neutral, 1..3
    bool lowRange = false, manual = false;
    float engineRpm = 800, throttleApplied = 0, steerApplied = 0;
    bool fuelCut = false;
    float stallTimer = 0;
    float shiftTimer = 0;
    float boost = 0;         // turbo/supercharger spool 0..1
    float clutchSlip = 0;
    float psi = 18.f;
    bool lockFront = false, lockRear = true, fourWD = true;

    // Thermals / damage-lite
    float engineTemp = 85.f, radiatorClog = 0.f;
    float dirt = 0.f;        // visual mud coverage 0..1

    // Derived / telemetry
    float speed() const { return length(vel); }
    float forwardSpeed() const { return dot(vel, forward()); }
    Vec3 forward() const { return rot.rotate({0, 0, 1}); }
    Vec3 up() const { return rot.rotate({0, 1, 0}); }
    Vec3 left() const { return rot.rotate({1, 0, 0}); }
    float bellyLoad = 0;     // fraction of weight carried by chassis (high-centered)
    float airTime = 0;       // continuous seconds with no wheel contact
    float avgSlip = 0;
    int wheelsInContact = 0;
    float submerged = 0;     // chassis water submersion depth
    VehicleEvents events;

private:
    void stepDrivetrain(float dt, const VehicleInput& in, float& totalTorque, float& reflectedInertia);
    float derate() const;
};

}  // namespace mb
