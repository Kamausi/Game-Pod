// Mud Buddies - core math. Y up, right-handed.
// Vehicle-local convention: +Z forward, +X left, +Y up (so right = -X).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mb {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG = PI / 180.0f;

inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}
inline float signf(float x) { return x < 0.f ? -1.f : 1.f; }
inline float sq(float x) { return x * x; }
// Frame-rate independent exponential approach.
inline float approach(float cur, float target, float rate, float dt) {
    return target + (cur - target) * std::exp(-rate * dt);
}

struct Vec2 {
    float x = 0, y = 0;
    constexpr Vec2() = default;
    constexpr Vec2(float a, float b) : x(a), y(b) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
};
inline float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 a) { return std::sqrt(dot(a, a)); }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    constexpr Vec3() = default;
    constexpr Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(Vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float& operator[](int i) { return (&x)[i]; }
    float operator[](int i) const { return (&x)[i]; }
};
inline Vec3 operator*(float s, Vec3 v) { return v * s; }
inline Vec3 mul(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    float l = length(a);
    return l > 1e-8f ? a / l : Vec3{0, 1, 0};
}
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
    constexpr Quat() = default;
    constexpr Quat(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    static Quat axisAngle(Vec3 axis, float a) {
        axis = normalize(axis);
        float s = std::sin(a * 0.5f);
        return {axis.x * s, axis.y * s, axis.z * s, std::cos(a * 0.5f)};
    }
    static Quat euler(float rx, float ry, float rz) {  // applied X, then Y, then Z (intrinsic ZYX order)
        return axisAngle({0, 0, 1}, rz) * axisAngle({0, 1, 0}, ry) * axisAngle({1, 0, 0}, rx);
    }
    Quat operator*(Quat q) const {
        return {w * q.x + x * q.w + y * q.z - z * q.y, w * q.y - x * q.z + y * q.w + z * q.x,
                w * q.z + x * q.y - y * q.x + z * q.w, w * q.w - x * q.x - y * q.y - z * q.z};
    }
    Quat conj() const { return {-x, -y, -z, w}; }
    Vec3 rotate(Vec3 v) const {
        Vec3 u{x, y, z};
        Vec3 t = cross(u, v) * 2.f;
        return v + t * w + cross(u, t);
    }
    Vec3 unrotate(Vec3 v) const { return conj().rotate(v); }
    Quat normalized() const {
        float l = std::sqrt(x * x + y * y + z * z + w * w);
        return l > 0 ? Quat{x / l, y / l, z / l, w / l} : Quat{};
    }
    // q' = q + 0.5 * (omega ⊗ q) dt, omega in world space
    Quat integrated(Vec3 omega, float dt) const {
        Quat o{omega.x, omega.y, omega.z, 0};
        Quat d = o * (*this);
        return Quat{x + 0.5f * dt * d.x, y + 0.5f * dt * d.y, z + 0.5f * dt * d.z, w + 0.5f * dt * d.w}.normalized();
    }
};
inline Quat slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {
        return Quat{lerpf(a.x, b.x, t), lerpf(a.y, b.y, t), lerpf(a.z, b.z, t), lerpf(a.w, b.w, t)}.normalized();
    }
    float th = std::acos(d), s = std::sin(th);
    float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
    return {a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb};
}

// Column-major 4x4 (OpenGL layout).
struct Mat4 {
    float m[16];
    static Mat4 identity() {
        Mat4 r{};
        for (int i = 0; i < 16; i++) r.m[i] = (i % 5 == 0) ? 1.f : 0.f;
        return r;
    }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
    Mat4 operator*(const Mat4& b) const {
        Mat4 r{};
        for (int c = 0; c < 4; c++)
            for (int rr = 0; rr < 4; rr++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += at(rr, k) * b.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }
    Vec3 transformPoint(Vec3 p) const {
        float x = at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z + at(0, 3);
        float y = at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z + at(1, 3);
        float z = at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z + at(2, 3);
        float w = at(3, 0) * p.x + at(3, 1) * p.y + at(3, 2) * p.z + at(3, 3);
        return w != 0 ? Vec3{x / w, y / w, z / w} : Vec3{x, y, z};
    }
    Vec3 transformDir(Vec3 p) const {
        return {at(0, 0) * p.x + at(0, 1) * p.y + at(0, 2) * p.z, at(1, 0) * p.x + at(1, 1) * p.y + at(1, 2) * p.z,
                at(2, 0) * p.x + at(2, 1) * p.y + at(2, 2) * p.z};
    }
    static Mat4 perspective(float fovy, float aspect, float n, float f) {
        Mat4 r{};
        float t = 1.f / std::tan(fovy * 0.5f);
        r.at(0, 0) = t / aspect;
        r.at(1, 1) = t;
        r.at(2, 2) = (f + n) / (n - f);
        r.at(2, 3) = 2 * f * n / (n - f);
        r.at(3, 2) = -1;
        return r;
    }
    static Mat4 ortho(float l, float rgt, float b, float t, float n, float f) {
        Mat4 r = identity();
        r.at(0, 0) = 2 / (rgt - l);
        r.at(1, 1) = 2 / (t - b);
        r.at(2, 2) = -2 / (f - n);
        r.at(0, 3) = -(rgt + l) / (rgt - l);
        r.at(1, 3) = -(t + b) / (t - b);
        r.at(2, 3) = -(f + n) / (f - n);
        return r;
    }
    static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
        r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
        r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
        r.at(0, 3) = -dot(s, eye);
        r.at(1, 3) = -dot(u, eye);
        r.at(2, 3) = dot(f, eye);
        return r;
    }
    static Mat4 trs(Vec3 t, Quat q, Vec3 s = {1, 1, 1}) {
        Vec3 bx = q.rotate({s.x, 0, 0}), by = q.rotate({0, s.y, 0}), bz = q.rotate({0, 0, s.z});
        Mat4 r = identity();
        r.at(0, 0) = bx.x; r.at(1, 0) = bx.y; r.at(2, 0) = bx.z;
        r.at(0, 1) = by.x; r.at(1, 1) = by.y; r.at(2, 1) = by.z;
        r.at(0, 2) = bz.x; r.at(1, 2) = bz.y; r.at(2, 2) = bz.z;
        r.at(0, 3) = t.x; r.at(1, 3) = t.y; r.at(2, 3) = t.z;
        return r;
    }
};

}  // namespace mb
