#include "net.h"
#include <cstring>

namespace mb {

namespace {
struct W {
    uint8_t* p;
    size_t n = 0;
    template <class T> void put(T v) { std::memcpy(p + n, &v, sizeof v); n += sizeof v; }
    void q16(float v, float range) { put<int16_t>(int16_t(clampf(v / range, -1.f, 1.f) * 32767.f)); }
    void u8n(float v, float range) { put<uint8_t>(uint8_t(clampf(v / range, 0.f, 1.f) * 255.f + 0.5f)); }
};
struct R {
    const uint8_t* p;
    size_t n = 0;
    template <class T> T get() { T v; std::memcpy(&v, p + n, sizeof v); n += sizeof v; return v; }
    float q16(float range) { return get<int16_t>() / 32767.f * range; }
    float u8n(float range) { return get<uint8_t>() / 255.f * range; }
};
}  // namespace

size_t NetSnapshot::write(uint8_t* out) const {
    std::memset(out, 0, WIRE_SIZE);
    W w{out};
    w.put<uint8_t>(TYPE);
    w.put<uint8_t>(flags);
    w.put<uint16_t>(id);
    w.put<uint32_t>(seq);
    w.put<double>(time);
    w.put<float>(pos.x); w.put<float>(pos.y); w.put<float>(pos.z);
    w.q16(vel.x, 80); w.q16(vel.y, 80); w.q16(vel.z, 80);
    Quat q = rot.normalized();
    w.q16(q.x, 1); w.q16(q.y, 1); w.q16(q.z, 1); w.q16(q.w, 1);
    w.put<uint16_t>(uint16_t(clampf(rpm, 0, 65535)));
    w.u8n(throttle, 1);
    w.put<int8_t>(int8_t(clampf(steer, -1, 1) * 127));
    w.put<int8_t>(gear);
    w.u8n(boost, 1);
    w.u8n(dirt, 1);
    for (float o : wheelOmega) w.q16(o, 200);
    for (float c : wheelComp) w.u8n(c, 1);
    w.u8n(mudSlap, 1);
    w.u8n(waterSpeed, 25);
    w.u8n(lugRate, 120);
    w.put<float>(score);
    w.put<float>(distance);
    return WIRE_SIZE;
}

bool NetSnapshot::read(const uint8_t* in, size_t len) {
    if (len < WIRE_SIZE || in[0] != TYPE) return false;
    R r{in};
    r.get<uint8_t>();
    flags = r.get<uint8_t>();
    id = r.get<uint16_t>();
    seq = r.get<uint32_t>();
    time = r.get<double>();
    pos.x = r.get<float>(); pos.y = r.get<float>(); pos.z = r.get<float>();
    vel.x = r.q16(80); vel.y = r.q16(80); vel.z = r.q16(80);
    rot.x = r.q16(1); rot.y = r.q16(1); rot.z = r.q16(1); rot.w = r.q16(1);
    rot = rot.normalized();
    rpm = r.get<uint16_t>();
    throttle = r.u8n(1);
    steer = r.get<int8_t>() / 127.f;
    gear = r.get<int8_t>();
    boost = r.u8n(1);
    dirt = r.u8n(1);
    for (float& o : wheelOmega) o = r.q16(200);
    for (float& c : wheelComp) c = r.u8n(1);
    mudSlap = r.u8n(1);
    waterSpeed = r.u8n(25);
    lugRate = r.u8n(120);
    score = r.get<float>();
    distance = r.get<float>();
    // Reject garbage: NaN or absurd coordinates.
    if (!(std::fabs(pos.x) < 1e4f && std::fabs(pos.y) < 1e4f && std::fabs(pos.z) < 1e4f)) return false;
    return true;
}

void RemoteVehicle::push(const NetSnapshot& s) {
    if (!buf.empty() && s.seq <= buf.back().seq) return;  // out of order / duplicate
    buf.push_back(s);
    while (buf.size() > 40) buf.pop_front();
}

bool RemoteVehicle::sample(double t, NetSnapshot& out) const {
    if (buf.empty()) return false;
    if (t <= buf.front().time) { out = buf.front(); return true; }
    for (size_t i = 1; i < buf.size(); i++) {
        const NetSnapshot &a = buf[i - 1], &b = buf[i];
        if (t > b.time) continue;
        double dt = b.time - a.time;
        float u = dt > 1e-6 ? float((t - a.time) / dt) : 1.f;
        float h = float(dt);
        // Cubic Hermite position using the snapshot velocities as tangents.
        float u2 = u * u, u3 = u2 * u;
        float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2, h11 = u3 - u2;
        out = u < 0.5f ? a : b;
        out.pos = a.pos * h00 + a.vel * (h10 * h) + b.pos * h01 + b.vel * (h11 * h);
        out.vel = lerp(a.vel, b.vel, u);
        out.rot = slerp(a.rot, b.rot, u);
        out.rpm = lerpf(a.rpm, b.rpm, u);
        out.throttle = lerpf(a.throttle, b.throttle, u);
        out.steer = lerpf(a.steer, b.steer, u);
        for (int k = 0; k < 4; k++) {
            out.wheelOmega[k] = lerpf(a.wheelOmega[k], b.wheelOmega[k], u);
            out.wheelComp[k] = lerpf(a.wheelComp[k], b.wheelComp[k], u);
        }
        return true;
    }
    // Past the newest snapshot: extrapolate briefly, then hold.
    const NetSnapshot& b = buf.back();
    float ex = float(std::min(t - b.time, kMaxExtrapolate));
    out = b;
    out.pos = b.pos + b.vel * ex;
    return true;
}

}  // namespace mb
