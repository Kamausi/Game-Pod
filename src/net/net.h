// Multiplayer state replication. Transport is a browser WebSocket owned by web/app.js; this
// module owns the binary wire format, snapshot buffering, clock-aligned interpolation
// (cubic Hermite on position, slerp on orientation) and bounded extrapolation.
// See docs/NETCODE.md.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
#include "../core/math.h"

namespace mb {

struct NetSnapshot {
    uint16_t id = 0;
    uint32_t seq = 0;
    double time = 0;          // sender's server-clock time (seconds)
    Vec3 pos, vel;
    Quat rot;
    float rpm = 0, throttle = 0, steer = 0, boost = 0, dirt = 0;
    int8_t gear = 1;
    uint8_t flags = 0;        // bit0 fuelCut, bit1 running, bit2 lockers, bit3 horn, bit4 brake
    float wheelOmega[4] = {0, 0, 0, 0};
    float wheelComp[4] = {0, 0, 0, 0};
    float mudSlap = 0, waterSpeed = 0, lugRate = 0;
    float score = 0, distance = 0;

    static constexpr size_t WIRE_SIZE = 96;
    static constexpr uint8_t TYPE = 0x51;
    size_t write(uint8_t* out) const;                     // returns WIRE_SIZE
    bool read(const uint8_t* in, size_t len);
};

class RemoteVehicle {
public:
    void push(const NetSnapshot& s);
    // Sample at renderTime (server clock seconds). Returns false if no data.
    bool sample(double renderTime, NetSnapshot& out) const;
    double lastTime() const { return buf.empty() ? 0 : buf.back().time; }
    std::string config;       // serialized VehicleConfig
    std::string name;

private:
    std::deque<NetSnapshot> buf;
};

constexpr double kInterpDelay = 0.12;     // seconds behind newest server time
constexpr double kMaxExtrapolate = 0.25;  // seconds

// NTP-style offset estimate from a ping exchange: t0 client send, t1 server recv,
// t2 server send, t3 client recv. offset = ((t1 - t0) + (t2 - t3)) / 2
inline double clockOffset(double t0, double t1, double t2, double t3) { return ((t1 - t0) + (t2 - t3)) * 0.5; }

}  // namespace mb
