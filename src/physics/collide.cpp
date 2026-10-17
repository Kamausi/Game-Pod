#include "collide.h"

namespace mb {

void applyImpulse(Vehicle& v, Vec3 point, Vec3 J) {
    v.vel += J / v.mass;
    Vec3 tb = v.rot.unrotate(cross(point - v.pos, J));
    Vec3 dw{tb.x / v.inertia.x, tb.y / v.inertia.y, tb.z / v.inertia.z};
    v.angVel += v.rot.rotate(dw);
}

namespace {
struct Box2 {
    Vec3 c, f, l;   // center, horizontal forward / left unit axes
    float hz, hx, y0, y1;
};
Box2 box(const Vehicle& v) {
    Box2 b;
    b.c = v.pos;
    Vec3 f = v.forward(), l = v.left();
    b.f = normalize(Vec3{f.x, 0, f.z});
    b.l = normalize(Vec3{l.x, 0, l.z});
    b.hz = v.body->dims[2] * 0.5f;
    b.hx = v.body->dims[0] * 0.5f;
    b.y0 = v.pos.y + v.body->bellyY - 0.1f;
    b.y1 = v.pos.y + v.body->bellyY + v.body->dims[1];
    return b;
}
float radius(const Box2& b, Vec3 axis) { return std::fabs(dot(b.f, axis)) * b.hz + std::fabs(dot(b.l, axis)) * b.hx; }
bool inside(const Box2& b, Vec3 p) {
    Vec3 d = p - b.c;
    return std::fabs(dot(d, b.f)) < b.hz && std::fabs(dot(d, b.l)) < b.hx;
}
}  // namespace

// Separating-axis test of the two chassis footprints (oriented rectangles in XZ) with a
// vertical overlap check; minimum-overlap axis oriented from b to a gives a stable normal
// that never flips mid-penetration (no tunneling when a heavy truck shoves a light one).
float collideVehicles(Vehicle& a, Vehicle& b, bool bMovable) {
    Box2 A = box(a), B = box(b);
    Vec3 d = A.c - B.c;
    d.y = 0;
    float reach = A.hz + B.hz + 0.5f;
    if (dot(d, d) > reach * reach) return 0;
    if (A.y1 < B.y0 || B.y1 < A.y0) return 0;  // one is airborne over the other
    const Vec3 axes[4] = {A.f, A.l, B.f, B.l};
    float best = 1e9f;
    Vec3 n;
    for (const Vec3& ax : axes) {
        float ov = radius(A, ax) + radius(B, ax) - std::fabs(dot(d, ax));
        if (ov <= 0) return 0;
        if (ov < best) { best = ov; n = dot(d, ax) >= 0 ? ax : -ax; }
    }
    // Contact point: mean of corners inside the other box, else the midpoint.
    Vec3 cp{}, sum{};
    int cnt = 0;
    for (int s = 0; s < 2; s++) {
        const Box2& P = s ? B : A;
        const Box2& Q = s ? A : B;
        for (int i = -1; i <= 1; i += 2)
            for (int k = -1; k <= 1; k += 2) {
                Vec3 c = P.c + P.f * (i * P.hz) + P.l * (k * P.hx);
                if (inside(Q, c)) { sum += c; cnt++; }
            }
    }
    cp = cnt ? sum / float(cnt) : (A.c + B.c) * 0.5f;
    cp.y = std::max(A.y0, B.y0) + 0.35f;

    const float invA = 1.f / a.mass, invB = bMovable ? 1.f / b.mass : 0.f, w = invA + invB;
    float corr = std::max(0.f, best - 0.01f);
    a.pos += n * (corr * invA / w);
    if (bMovable) b.pos -= n * (corr * invB / w);

    Vec3 va = a.vel + cross(a.angVel, cp - a.pos), vb = b.vel + cross(b.angVel, cp - b.pos);
    Vec3 rel = va - vb;
    float vn = dot(rel, n);
    if (vn >= 0) return 0;
    const float e = 0.25f;
    float j = -(1.f + e) * vn / (w * 1.4f);  // 1.4 ~ rotational share of the effective mass
    Vec3 J = n * j;
    Vec3 vt = rel - n * vn;
    vt.y = 0;
    float vtl = length(vt);
    if (vtl > 1e-3f) J -= vt * (std::min(0.35f * j, vtl / (w * 3.f)) / vtl);
    applyImpulse(a, cp, J);
    if (bMovable) applyImpulse(b, cp, -J);
    if (j > a.events.impact) { a.events.impact = j; a.events.impactPos = cp; }
    if (bMovable && j > b.events.impact) { b.events.impact = j; b.events.impactPos = cp; }
    return j;
}

}  // namespace mb
