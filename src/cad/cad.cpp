#include "cad.h"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace mb {

static std::string g_root = "cad/parts";

int cadMaterialId(const std::string& n) {
    static const char* names[] = {"plastic", "paint", "glass", "chrome", "rubber", "lens", "matte", "steel", "mud", "leaf"};
    for (int i = 0; i < 10; i++)
        if (n == names[i]) return i;
    return -1;
}
void cadSetRoot(const std::string& root) { g_root = root; }
const std::string& cadRoot() { return g_root; }

// ------------------------------------------------------------------------------------------
// Mesh utilities
void CadMesh::append(const CadMesh& o, const Mat4& m, bool flip) {
    // Normal matrix = cofactor of the upper 3x3 (inverse-transpose up to scale).
    float a = m.at(0, 0), b = m.at(0, 1), c = m.at(0, 2);
    float d = m.at(1, 0), e = m.at(1, 1), f = m.at(1, 2);
    float g = m.at(2, 0), h = m.at(2, 1), i = m.at(2, 2);
    float cof[9] = {e * i - f * h, -(d * i - f * g), d * h - e * g,
                    -(b * i - c * h), a * i - c * g, -(a * h - b * g),
                    b * f - c * e, -(a * f - c * d), a * e - b * d};
    uint32_t baseIdx = uint32_t(verts.size());
    for (const CadVertex& v : o.verts) {
        Vec3 p = m.transformPoint({v.px, v.py, v.pz});
        Vec3 n{cof[0] * v.nx + cof[1] * v.ny + cof[2] * v.nz, cof[3] * v.nx + cof[4] * v.ny + cof[5] * v.nz,
               cof[6] * v.nx + cof[7] * v.ny + cof[8] * v.nz};
        n = normalize(n);
        verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, v.r, v.g, v.b, v.mat});
    }
    for (size_t k = 0; k + 2 < o.idx.size(); k += 3) {
        if (flip) {
            idx.push_back(baseIdx + o.idx[k]);
            idx.push_back(baseIdx + o.idx[k + 2]);
            idx.push_back(baseIdx + o.idx[k + 1]);
        } else {
            idx.push_back(baseIdx + o.idx[k]);
            idx.push_back(baseIdx + o.idx[k + 1]);
            idx.push_back(baseIdx + o.idx[k + 2]);
        }
    }
}

Vec3 CadMesh::boundsMin() const {
    Vec3 r{1e9f, 1e9f, 1e9f};
    for (auto& v : verts) { r.x = std::min(r.x, v.px); r.y = std::min(r.y, v.py); r.z = std::min(r.z, v.pz); }
    return r;
}
Vec3 CadMesh::boundsMax() const {
    Vec3 r{-1e9f, -1e9f, -1e9f};
    for (auto& v : verts) { r.x = std::max(r.x, v.px); r.y = std::max(r.y, v.py); r.z = std::max(r.z, v.pz); }
    return r;
}

namespace {
struct Builder {
    CadMesh m;
    Vec3 col;
    explicit Builder(Vec3 c) : col(c) {}
    uint32_t v(Vec3 p, Vec3 n) {
        m.verts.push_back({p.x, p.y, p.z, n.x, n.y, n.z, col.x, col.y, col.z, 0.f});
        return uint32_t(m.verts.size() - 1);
    }
    Vec3 P(uint32_t i) const { return {m.verts[i].px, m.verts[i].py, m.verts[i].pz}; }
    // Auto-orienting triangle: winding chosen so the face normal agrees with `out`.
    void tri(uint32_t a, uint32_t b, uint32_t c, Vec3 out) {
        Vec3 n = cross(P(b) - P(a), P(c) - P(a));
        if (dot(n, out) < 0) std::swap(b, c);
        m.idx.push_back(a); m.idx.push_back(b); m.idx.push_back(c);
    }
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, Vec3 out) {
        tri(a, b, c, out);
        tri(a, c, d, out);
    }
};

float polyArea(const std::vector<Vec2>& p) {
    float a = 0;
    for (size_t i = 0; i < p.size(); i++) {
        const Vec2 &u = p[i], &w = p[(i + 1) % p.size()];
        a += u.x * w.y - w.x * u.y;
    }
    return 0.5f * a;
}

bool pointInTri(Vec2 p, Vec2 a, Vec2 b, Vec2 c) {
    auto s = [](Vec2 p1, Vec2 p2, Vec2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
    float d1 = s(p, a, b), d2 = s(p, b, c), d3 = s(p, c, a);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// Ear clipping for a CCW simple polygon. Returns index triples.
std::vector<int> earClip(const std::vector<Vec2>& poly) {
    std::vector<int> idx(poly.size()), out;
    for (size_t i = 0; i < poly.size(); i++) idx[i] = int(i);
    int guard = 0;
    while (idx.size() > 3 && guard++ < 10000) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); i++) {
            int ia = idx[(i + idx.size() - 1) % idx.size()], ib = idx[i], ic = idx[(i + 1) % idx.size()];
            Vec2 a = poly[ia], b = poly[ib], c = poly[ic];
            float cr = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cr <= 1e-9f) continue;  // reflex
            bool inside = false;
            for (int j : idx) {
                if (j == ia || j == ib || j == ic) continue;
                if (pointInTri(poly[j], a, b, c)) { inside = true; break; }
            }
            if (inside) continue;
            out.push_back(ia); out.push_back(ib); out.push_back(ic);
            idx.erase(idx.begin() + long(i));
            clipped = true;
            break;
        }
        if (!clipped) break;  // degenerate; bail out
    }
    if (idx.size() == 3) { out.push_back(idx[0]); out.push_back(idx[1]); out.push_back(idx[2]); }
    return out;
}
}  // namespace

namespace cadprim {

CadMesh box(Vec3 s, Vec3 color) {
    Builder B(color);
    Vec3 h = s * 0.5f;
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; a++)
        for (int sg = -1; sg <= 1; sg += 2) {
            Vec3 n = axes[a] * float(sg);
            Vec3 u = axes[(a + 1) % 3], w = axes[(a + 2) % 3];
            Vec3 c = mul(n, h);
            Vec3 uu = mul(u, h), ww = mul(w, h);
            uint32_t i0 = B.v(c - uu - ww, n), i1 = B.v(c + uu - ww, n), i2 = B.v(c + uu + ww, n), i3 = B.v(c - uu + ww, n);
            B.quad(i0, i1, i2, i3, n);
        }
    return B.m;
}

// Box with rounded edges/corners: each face is a grid whose outer bands are projected onto a
// radius-r rounding about the inner box, giving smooth normals across every edge.
CadMesh roundedBox(Vec3 s, float bevel, int seg, Vec3 color) {
    Vec3 h = s * 0.5f;
    float r = std::min(bevel, 0.999f * std::min(h.x, std::min(h.y, h.z)));
    if (r <= 1e-5f) return box(s, color);
    seg = std::max(1, seg);
    Builder B(color);
    Vec3 in = h - Vec3{r, r, r};
    auto coords = [&](float hh, float ii) {
        std::vector<float> c;
        for (int k = 0; k <= seg; k++) c.push_back(-hh + (hh - ii) * float(k) / float(seg));
        for (int k = 0; k <= seg; k++) c.push_back(ii + (hh - ii) * float(k) / float(seg));
        if (ii < 1e-5f) c.erase(c.begin() + seg + 1);  // inner span collapsed
        return c;
    };
    const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    auto comp = [](Vec3 v, int a) { return a == 0 ? v.x : (a == 1 ? v.y : v.z); };
    for (int a = 0; a < 3; a++)
        for (int sg = -1; sg <= 1; sg += 2) {
            int au = (a + 1) % 3, aw = (a + 2) % 3;
            Vec3 nf = axes[a] * float(sg);
            std::vector<float> cu = coords(comp(h, au), comp(in, au)), cw = coords(comp(h, aw), comp(in, aw));
            std::vector<uint32_t> g;
            for (float w : cw)
                for (float u : cu) {
                    Vec3 p = nf * comp(h, a) + axes[au] * u + axes[aw] * w;
                    Vec3 c{clampf(p.x, -in.x, in.x), clampf(p.y, -in.y, in.y), clampf(p.z, -in.z, in.z)};
                    Vec3 d = p - c;
                    float l = length(d);
                    Vec3 nn = l > 1e-6f ? d * (1.f / l) : nf;
                    g.push_back(B.v(c + nn * r, nn));
                }
            size_t nu = cu.size();
            for (size_t j = 0; j + 1 < cw.size(); j++)
                for (size_t i = 0; i + 1 < nu; i++)
                    B.quad(g[j * nu + i], g[j * nu + i + 1], g[(j + 1) * nu + i + 1], g[(j + 1) * nu + i], nf);
        }
    return B.m;
}

CadMesh cylinder(float r, float r2, float hgt, int seg, Vec3 color) {
    Builder B(color);
    seg = std::max(3, seg);
    float y0 = -hgt * 0.5f, y1 = hgt * 0.5f;
    float slope = (r - r2) / std::max(hgt, 1e-6f);
    for (int i = 0; i < seg; i++) {
        float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
        Vec3 n0 = normalize({std::cos(a0), slope, std::sin(a0)}), n1 = normalize({std::cos(a1), slope, std::sin(a1)});
        uint32_t b0 = B.v({r * std::cos(a0), y0, r * std::sin(a0)}, n0), b1 = B.v({r * std::cos(a1), y0, r * std::sin(a1)}, n1);
        uint32_t t1 = B.v({r2 * std::cos(a1), y1, r2 * std::sin(a1)}, n1), t0 = B.v({r2 * std::cos(a0), y1, r2 * std::sin(a0)}, n0);
        Vec3 mid{std::cos((a0 + a1) * 0.5f), 0, std::sin((a0 + a1) * 0.5f)};
        B.quad(b0, b1, t1, t0, mid);
    }
    for (int cap = 0; cap < 2; cap++) {
        float y = cap ? y1 : y0, rr = cap ? r2 : r;
        if (rr <= 1e-5f) continue;
        Vec3 n{0, cap ? 1.f : -1.f, 0};
        uint32_t c = B.v({0, y, 0}, n);
        for (int i = 0; i < seg; i++) {
            float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
            uint32_t p0 = B.v({rr * std::cos(a0), y, rr * std::sin(a0)}, n), p1 = B.v({rr * std::cos(a1), y, rr * std::sin(a1)}, n);
            B.tri(c, p0, p1, n);
        }
    }
    return B.m;
}

CadMesh sphere(float r, int seg, Vec3 color) {
    Builder B(color);
    seg = std::max(4, seg);
    int rings = std::max(3, seg / 2);
    std::vector<uint32_t> grid;
    for (int j = 0; j <= rings; j++) {
        float th = PI * j / rings;
        for (int i = 0; i <= seg; i++) {
            float ph = 2 * PI * i / seg;
            Vec3 n{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
            grid.push_back(B.v(n * r, n));
        }
    }
    for (int j = 0; j < rings; j++)
        for (int i = 0; i < seg; i++) {
            uint32_t a = grid[j * (seg + 1) + i], b = grid[j * (seg + 1) + i + 1];
            uint32_t c = grid[(j + 1) * (seg + 1) + i + 1], d = grid[(j + 1) * (seg + 1) + i];
            Vec3 out = B.P(a) + B.P(c);
            if (j == 0) B.tri(a, c, d, out);
            else if (j == rings - 1) B.tri(a, b, c, out);
            else B.quad(a, b, c, d, out);
        }
    return B.m;
}

CadMesh lathe(const std::vector<Vec2>& prof, int seg, Vec3 color) {
    // Convention: walk the profile with the outside of the surface on your right
    // (r to the right, y up). Each profile segment is flat-shaded across, smooth around.
    Builder B(color);
    seg = std::max(3, seg);
    for (size_t k = 0; k + 1 < prof.size(); k++) {
        Vec2 p0 = prof[k], p1 = prof[k + 1];
        float dr = p1.x - p0.x, dy = p1.y - p0.y;
        float l = std::sqrt(dr * dr + dy * dy);
        if (l < 1e-6f) continue;
        Vec2 n2{dy / l, -dr / l};
        for (int i = 0; i < seg; i++) {
            float a0 = 2 * PI * i / seg, a1 = 2 * PI * (i + 1) / seg;
            Vec3 na{n2.x * std::cos(a0), n2.y, n2.x * std::sin(a0)}, nb{n2.x * std::cos(a1), n2.y, n2.x * std::sin(a1)};
            uint32_t q0 = B.v({p0.x * std::cos(a0), p0.y, p0.x * std::sin(a0)}, na);
            uint32_t q1 = B.v({p0.x * std::cos(a1), p0.y, p0.x * std::sin(a1)}, nb);
            uint32_t q2 = B.v({p1.x * std::cos(a1), p1.y, p1.x * std::sin(a1)}, nb);
            uint32_t q3 = B.v({p1.x * std::cos(a0), p1.y, p1.x * std::sin(a0)}, na);
            float am = (a0 + a1) * 0.5f;
            Vec3 out{n2.x * std::cos(am), n2.y, n2.x * std::sin(am)};
            if (p0.x < 1e-5f) B.tri(q0, q2, q3, out);
            else if (p1.x < 1e-5f) B.tri(q0, q1, q2, out);
            else B.quad(q0, q1, q2, q3, out);
        }
    }
    return B.m;
}

CadMesh extrude(const std::vector<Vec2>& poly, float depth, Vec3 color) { return extrudeBevel(poly, depth, 0.f, 0, color); }

// Side-profile extrusion along X with optional rounded rims. Profile edges meeting at less than
// ~40 degrees share a smoothed normal, so arcs (wheel arches, noses) shade as curves while real
// corners (windshield base, tailgate) stay crisp.
CadMesh extrudeBevel(const std::vector<Vec2>& polyIn, float depth, float bevel, int seg, Vec3 color) {
    Builder B(color);
    std::vector<Vec2> poly;
    for (const Vec2& p : polyIn)
        if (poly.empty() || std::fabs(p.x - poly.back().x) + std::fabs(p.y - poly.back().y) > 1e-5f) poly.push_back(p);
    while (poly.size() > 1 && std::fabs(poly[0].x - poly.back().x) + std::fabs(poly[0].y - poly.back().y) <= 1e-5f) poly.pop_back();
    if (poly.size() < 3) return B.m;
    if (polyArea(poly) < 0) std::reverse(poly.begin(), poly.end());  // CCW in (z, y)
    const size_t n = poly.size();
    float r = std::max(0.f, std::min(bevel, depth * 0.45f));
    if (r <= 1e-5f) seg = 0;
    seg = std::max(seg, r > 0 ? 1 : 0);
    // Outward edge normals (CCW -> interior on the left) and per-vertex inward miters.
    std::vector<Vec2> eo(n), miter(n);
    for (size_t i = 0; i < n; i++) {
        Vec2 a = poly[i], b = poly[(i + 1) % n];
        float dx = b.x - a.x, dy = b.y - a.y, l = std::max(1e-6f, std::sqrt(dx * dx + dy * dy));
        eo[i] = {dy / l, -dx / l};
    }
    for (size_t i = 0; i < n; i++) {
        Vec2 n0 = eo[(i + n - 1) % n], n1 = eo[i];
        Vec2 m{-(n0.x + n1.x), -(n0.y + n1.y)};
        float l = std::sqrt(m.x * m.x + m.y * m.y);
        if (l < 1e-5f) m = {-n1.x, -n1.y};
        else m = {m.x / l, m.y / l};
        float c = std::max(0.35f, -(m.x * n1.x + m.y * n1.y));
        miter[i] = {m.x / c, m.y / c};
    }
    auto smoothN = [&](size_t v, size_t e) {  // normal of vertex v as seen from edge e
        size_t other = (e == v) ? (v + n - 1) % n : (v % n);
        Vec2 a = eo[e], b = eo[other];
        if (a.x * b.x + a.y * b.y < 0.766f) return a;  // > 40 deg: hard edge
        Vec2 m{a.x + b.x, a.y + b.y};
        float l = std::sqrt(m.x * m.x + m.y * m.y);
        return Vec2{m.x / l, m.y / l};
    };
    float hx = depth * 0.5f;
    for (size_t e = 0; e < n; e++) {
        size_t ia = e, ib = (e + 1) % n;
        Vec2 na = smoothN(ia, e), nb = smoothN(ib, e);
        Vec3 out{0, eo[e].y, eo[e].x};
        // rings from -X cap through the side wall to +X cap
        std::vector<uint32_t> ra, rb;
        auto ring = [&](float x, float inset, float nx, float wside) {
            Vec2 pa{poly[ia].x + miter[ia].x * inset, poly[ia].y + miter[ia].y * inset};
            Vec2 pb{poly[ib].x + miter[ib].x * inset, poly[ib].y + miter[ib].y * inset};
            ra.push_back(B.v({x, pa.y, pa.x}, normalize(Vec3{nx, na.y * wside, na.x * wside})));
            rb.push_back(B.v({x, pb.y, pb.x}, normalize(Vec3{nx, nb.y * wside, nb.x * wside})));
        };
        for (int k = seg; k >= 0; k--) {
            float th = seg ? 0.5f * PI * float(k) / float(seg) : 0.f;
            ring(-(hx - r + r * std::sin(th)), r * (1 - std::cos(th)), -std::sin(th), std::cos(th));
        }
        for (int k = 0; k <= seg; k++) {
            float th = seg ? 0.5f * PI * float(k) / float(seg) : 0.f;
            ring(hx - r + r * std::sin(th), r * (1 - std::cos(th)), std::sin(th), std::cos(th));
        }
        for (size_t k = 0; k + 1 < ra.size(); k++) {
            float xm = 0.5f * (B.P(ra[k]).x + B.P(ra[k + 1]).x);
            Vec3 o = normalize(out + Vec3{xm / std::max(hx, 1e-4f) * (seg ? 0.6f : 0.f), 0, 0});
            B.quad(ra[k], rb[k], rb[k + 1], ra[k + 1], o);
        }
    }
    std::vector<Vec2> cap(n);
    for (size_t i = 0; i < n; i++) cap[i] = {poly[i].x + miter[i].x * r, poly[i].y + miter[i].y * r};
    std::vector<int> tris = earClip(cap);
    for (int side = 0; side < 2; side++) {
        float x = side ? hx : -hx;
        Vec3 nn{side ? 1.f : -1.f, 0, 0};
        std::vector<uint32_t> ids;
        for (auto& p : cap) ids.push_back(B.v({x, p.y, p.x}, nn));
        for (size_t t = 0; t + 2 < tris.size(); t += 3) B.tri(ids[size_t(tris[t])], ids[size_t(tris[t + 1])], ids[size_t(tris[t + 2])], nn);
    }
    return B.m;
}

CadMesh wedge(Vec3 s, Vec3 color) {
    // Full height at -Z, tapering to zero height at +Z (windshield / ramp shape).
    std::vector<Vec2> tri{{-s.z * 0.5f, -s.y * 0.5f}, {s.z * 0.5f, -s.y * 0.5f}, {-s.z * 0.5f, s.y * 0.5f}};
    return extrude(tri, s.x, color);
}

CadMesh tire(float r, float w, float rimR, int lugs, float depth, int tread, Vec3 color) {
    // Carcass profile in (radius, axial) walked with the outside on the right.
    float rt = r - depth, hw = w * 0.5f;
    float sh = std::min(0.05f, (rt - rimR) * 0.3f);
    std::vector<Vec2> prof{{rimR, -hw * 0.82f}, {rimR + (rt - rimR) * 0.5f, -hw * 1.02f}, {rt - sh, -hw},
                           {rt, -hw + sh}, {rt, hw - sh}, {rt - sh, hw}, {rimR + (rt - rimR) * 0.5f, hw * 1.02f},
                           {rimR, hw * 0.82f}};
    CadMesh m = lathe(prof, 36, color);
    Vec3 lugCol = color * 1.15f;
    float pitch = 2 * PI * rt / std::max(lugs, 1);
    for (int i = 0; i < lugs; i++) {
        float a = 2 * PI * i / lugs;
        auto place = [&](Vec3 size, float axial, float angOff, float chevron, float radialExtra) {
            CadMesh lug = box(size, lugCol);
            Quat q = Quat::axisAngle({0, 1, 0}, -(a + angOff)) * Quat::axisAngle({1, 0, 0}, chevron);
            Mat4 M = Mat4::trs(Quat::axisAngle({0, 1, 0}, -(a + angOff)).rotate({rt + size.x * 0.5f - radialExtra, axial, 0}), q);
            m.append(lug, M);
        };
        if (tread == 2) {  // paddle: full-width scoops
            place({depth * 2.4f, w * 0.96f, std::max(0.018f, pitch * 0.12f)}, 0.f, 0.f, 0.f, 0.005f);
        } else if (tread == 1) {  // mud-terrain: staggered chevron blocks
            place({depth, w * 0.46f, pitch * 0.48f}, w * 0.25f, 0.f, 0.26f, 0.003f);
            place({depth, w * 0.46f, pitch * 0.48f}, -w * 0.25f, PI / std::max(lugs, 1), -0.26f, 0.003f);
        } else {  // all-terrain: three tight blocks per row
            for (int k = -1; k <= 1; k++)
                place({depth * 0.7f, w * 0.28f, pitch * 0.6f}, k * w * 0.32f, (k & 1) ? PI / lugs * 0.5f : 0.f, 0.f, 0.003f);
        }
    }
    // Lathe axis Y -> axle axis X
    CadMesh out;
    out.append(m, Mat4::trs({}, Quat::axisAngle({0, 0, 1}, -PI * 0.5f)));
    return out;
}

CadMesh rim(float r, float w, int spokes, Vec3 color) {
    CadMesh m;
    // Barrel: open tube approximated by lathe with outside-right walk (outer surface).
    std::vector<Vec2> barrel{{r * 0.9f, -w * 0.5f}, {r, -w * 0.48f}, {r, w * 0.48f}, {r * 0.9f, w * 0.5f}};
    m.append(lathe(barrel, 28, color), Mat4::identity());
    // Face disc set into the outboard side (+Y here, mapped to +X/outboard later).
    m.append(cylinder(r * 0.92f, r * 0.92f, 0.012f, 28, color * 0.95f), Mat4::trs({0, w * 0.32f, 0}, Quat{}));
    m.append(cylinder(r * 0.28f, r * 0.24f, 0.05f, 16, color * 1.25f), Mat4::trs({0, w * 0.36f, 0}, Quat{}));
    for (int i = 0; i < spokes; i++) {
        float a = 2 * PI * i / spokes;
        CadMesh s = box({r * 0.6f, 0.02f, r * 0.16f}, color * 0.8f);
        m.append(s, Mat4::trs(Quat::axisAngle({0, 1, 0}, a).rotate({r * 0.55f, w * 0.345f, 0}), Quat::axisAngle({0, 1, 0}, a)));
    }
    CadMesh out;
    out.append(m, Mat4::trs({}, Quat::axisAngle({0, 0, 1}, -PI * 0.5f)));
    return out;
}

}  // namespace cadprim

// ------------------------------------------------------------------------------------------
// Expression evaluator
namespace {
struct Expr {
    const std::string& s;
    size_t i = 0;
    const CadParams& vars;
    std::string* err;
    Expr(const std::string& str, const CadParams& v, std::string* e) : s(str), vars(v), err(e) {}
    void ws() { while (i < s.size() && std::isspace((unsigned char)s[i])) i++; }
    bool eat(char c) { ws(); if (i < s.size() && s[i] == c) { i++; return true; } return false; }
    void fail(const std::string& m) { if (err && err->empty()) *err = m + " in '" + s + "'"; }
    float primary() {
        ws();
        if (eat('(')) { float v = expr(); if (!eat(')')) fail("missing )"); return v; }
        if (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.')) {
            size_t st = i;
            while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '.' || s[i] == 'e' ||
                                    ((s[i] == '-' || s[i] == '+') && i > st && (s[i - 1] == 'e'))))
                i++;
            return float(std::atof(s.substr(st, i - st).c_str()));
        }
        if (i < s.size() && (std::isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == '$')) {
            if (s[i] == '$') i++;
            size_t st = i;
            while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i] == '_')) i++;
            std::string id = s.substr(st, i - st);
            if (eat('(')) {
                std::vector<float> args;
                if (!eat(')')) {
                    do args.push_back(expr()); while (eat(','));
                    if (!eat(')')) fail("missing ) after args");
                }
                auto A = [&](size_t k) { return k < args.size() ? args[k] : 0.f; };
                if (id == "sin") return std::sin(A(0) * DEG);
                if (id == "cos") return std::cos(A(0) * DEG);
                if (id == "sqrt") return std::sqrt(std::max(0.f, A(0)));
                if (id == "abs") return std::fabs(A(0));
                if (id == "min") return std::min(A(0), A(1));
                if (id == "max") return std::max(A(0), A(1));
                fail("unknown function " + id);
                return 0;
            }
            if (id == "pi") return PI;
            auto it = vars.find(id);
            if (it == vars.end()) { fail("unknown name " + id); return 0; }
            return it->second;
        }
        fail("unexpected character");
        i = s.size();
        return 0;
    }
    float unary() { if (eat('-')) return -unary(); if (eat('+')) return unary(); return power(); }
    float power() { float b = primary(); if (eat('^')) return std::pow(b, unary()); return b; }
    float term() {
        float v = unary();
        for (;;) {
            if (eat('*')) v *= unary();
            else if (eat('/')) { float d = unary(); v = d != 0 ? v / d : 0; }
            else return v;
        }
    }
    float expr() {
        float v = term();
        for (;;) {
            if (eat('+')) v += term();
            else if (eat('-')) v -= term();
            else return v;
        }
    }
};

std::vector<std::string> splitTopLevel(const std::string& s, char sep) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if (c == '(') depth++;
        if (c == ')') depth--;
        if (c == sep && depth == 0) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4g", v);
    return b;
}
std::string fmt3(Vec3 v) { return "[" + fmt(v.x) + "," + fmt(v.y) + "," + fmt(v.z) + "]"; }

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
}  // namespace

float cadEval(const std::string& expr, const CadParams& vars, std::string* err) {
    Expr e(expr, vars, err);
    float v = e.expr();
    e.ws();
    if (e.i < expr.size() && err && err->empty()) *err = "trailing characters in '" + expr + "'";
    return v;
}

// ------------------------------------------------------------------------------------------
// Part interpreter
namespace {
struct Interp {
    CadParams vars;
    Vec3 color{0.7f, 0.7f, 0.7f};
    int material = MatPlastic;
    CadMesh mesh;
    std::string scad;
    std::string err;
    int depth = 0;
    std::string indent = "  ";

    std::vector<float> evalList(const std::string& v) {
        std::vector<float> out;
        for (auto& p : splitTopLevel(v, ',')) out.push_back(cadEval(p, vars, &err));
        return out;
    }
    Vec3 vec3(const std::map<std::string, std::string>& a, const char* k, Vec3 def) {
        auto it = a.find(k);
        if (it == a.end()) return def;
        auto l = evalList(it->second);
        if (l.size() == 1) return {l[0], l[0], l[0]};
        if (l.size() < 3) { err = std::string("expected 3 values for ") + k; return def; }
        return {l[0], l[1], l[2]};
    }
    float num(const std::map<std::string, std::string>& a, const char* k, float def) {
        auto it = a.find(k);
        if (it == a.end()) return def;
        return cadEval(it->second, vars, &err);
    }
    std::string str(const std::map<std::string, std::string>& a, const char* k, const std::string& def) {
        auto it = a.find(k);
        return it == a.end() ? def : it->second;
    }
    std::vector<Vec2> pts(const std::map<std::string, std::string>& a) {
        std::vector<Vec2> p;
        auto it = a.find("pts");
        if (it == a.end()) return p;
        std::vector<float> l;
        for (auto& tok : splitTopLevel(it->second, ',')) {
            std::string t = tok;
            t.erase(0, t.find_first_not_of(" \t"));
            if (t.compare(0, 4, "arc(") == 0 && t.back() == ')') {
                // arc(zc, yc, r, a0, a1, n): flush pending coordinates, then n+1 arc points
                auto a = evalList(t.substr(4, t.size() - 5));
                if (a.size() < 6) { err = "arc needs zc,yc,r,a0,a1,n"; return p; }
                for (size_t i = 0; i + 1 < l.size(); i += 2) p.push_back({l[i], l[i + 1]});
                l.clear();
                int cnt = std::max(1, int(a[5] + 0.5f));
                for (int k = 0; k <= cnt; k++) {
                    float ang = (a[3] + (a[4] - a[3]) * float(k) / float(cnt)) * DEG;
                    p.push_back({a[0] + a[2] * std::cos(ang), a[1] + a[2] * std::sin(ang)});
                }
                continue;
            }
            l.push_back(cadEval(t, vars, &err));
            if (l.size() == 2) { p.push_back({l[0], l[1]}); l.clear(); }
        }
        return p;
    }
    static Quat axisQuat(const std::string& axis) {
        if (axis == "x") return Quat::axisAngle({0, 0, 1}, -PI * 0.5f);
        if (axis == "z") return Quat::axisAngle({1, 0, 0}, PI * 0.5f);
        return Quat{};
    }
    static std::string axisScad(const std::string& axis) {
        if (axis == "x") return "rotate([0,0,-90])";
        if (axis == "z") return "rotate([90,0,0])";
        return "";
    }

    void emit(const CadMesh& prim, const std::map<std::string, std::string>& a, const std::string& scadPrim, int forceMat = -2) {
        // Primitives take the current material (or a forced one); `use` children keep their own.
        size_t first = mesh.verts.size();
        emitRaw(prim, a, scadPrim);
        if (forceMat != -1) {
            float m = float(forceMat >= 0 ? forceMat : material);
            for (size_t k = first; k < mesh.verts.size(); k++) mesh.verts[k].mat = m;
        }
    }
    void emitRaw(const CadMesh& prim, const std::map<std::string, std::string>& a, const std::string& scadPrim) {
        Vec3 pos = vec3(a, "pos", {0, 0, 0}), rot = vec3(a, "rot", {0, 0, 0}), scl = vec3(a, "scale", {1, 1, 1});
        Quat q = Quat::euler(rot.x * DEG, rot.y * DEG, rot.z * DEG);
        Mat4 M = Mat4::trs(pos, q, scl);
        mesh.append(prim, M);
        std::string mir = str(a, "mirror", "");
        std::string body = "translate(" + fmt3(pos) + ") rotate(" + fmt3(rot) + ") scale(" + fmt3(scl) + ") color(" +
                           fmt3(color) + ") " + scadPrim + ";\n";
        scad += indent + body;
        for (char ax : mir) {
            Vec3 s{ax == 'x' ? -1.f : 1.f, ax == 'y' ? -1.f : 1.f, ax == 'z' ? -1.f : 1.f};
            Mat4 R = Mat4::trs({}, Quat{}, s);
            mesh.append(prim, R * M, true);
            scad += indent + "mirror(" + fmt3({ax == 'x' ? 1.f : 0.f, ax == 'y' ? 1.f : 0.f, ax == 'z' ? 1.f : 0.f}) + ") " + body;
        }
    }

    void run(const std::string& src, const std::string& name) {
        std::stringstream ss(src);
        std::string line;
        int lineNo = 0;
        while (std::getline(ss, line) && err.empty()) {
            lineNo++;
            auto hash = line.find('#');
            if (hash != std::string::npos) {
                // allow "#rrggbb" in color lines
                size_t lead = line.find_first_not_of(" \t");
                bool colorHex = lead != std::string::npos && line.compare(lead, 5, "color") == 0 && hash > lead + 5;   // indented too (loop bodies)
                if (!colorHex) line = line.substr(0, hash);
            }
            std::stringstream ls(line);
            std::string op;
            if (!(ls >> op)) continue;
            std::string rest;
            std::getline(ls, rest);
            if (op == "end") { err = name + ":" + std::to_string(lineNo) + ": 'end' without 'for'"; break; }
            if (op == "for") {
                // for i = a, b [, step] ... end   (inclusive; nestable; i is visible to expressions)
                auto eq = rest.find('=');
                if (eq == std::string::npos) { err = name + ":" + std::to_string(lineNo) + ": for needs 'var = a, b'"; break; }
                std::string var = rest.substr(0, eq);
                var.erase(std::remove_if(var.begin(), var.end(), ::isspace), var.end());
                std::vector<std::string> parts;
                {
                    std::stringstream rs(rest.substr(eq + 1));
                    std::string tok;
                    while (std::getline(rs, tok, ',')) parts.push_back(tok);
                }
                if (parts.size() < 2) { err = name + ":" + std::to_string(lineNo) + ": for needs 'var = a, b'"; break; }
                float a0 = cadEval(parts[0], vars, &err), a1 = cadEval(parts[1], vars, &err);
                float step = parts.size() > 2 ? cadEval(parts[2], vars, &err) : 1.f;
                // Gather the body up to the matching 'end'.
                std::string body, bl;
                int depth = 1, startLine = lineNo;
                while (std::getline(ss, bl)) {
                    lineNo++;
                    std::stringstream bs(bl.substr(0, bl.find('#') == std::string::npos || bl.compare(0, 5, "color") == 0 ? bl.size() : bl.find('#')));
                    std::string bop;
                    bs >> bop;
                    if (bop == "for") depth++;
                    else if (bop == "end" && --depth == 0) break;
                    body += bl + "\n";
                }
                if (depth != 0) { err = name + ":" + std::to_string(startLine) + ": 'for' without 'end'"; break; }
                if (step == 0.f || std::fabs((a1 - a0) / step) > 4096.f) { err = name + ":" + std::to_string(startLine) + ": bad for range"; break; }
                bool had = vars.count(var) > 0;
                float saved = had ? vars[var] : 0.f;
                for (float v = a0; step > 0 ? v <= a1 + 1e-4f : v >= a1 - 1e-4f; v += step) {
                    vars[var] = v;
                    run(body, name + ":" + std::to_string(startLine));
                    if (!err.empty()) break;
                }
                if (had) vars[var] = saved; else vars.erase(var);
                continue;
            }
            if (op == "param" || op == "set") {
                auto eq = rest.find('=');
                std::string k, v;
                if (eq != std::string::npos) { k = rest.substr(0, eq); v = rest.substr(eq + 1); }
                else { std::stringstream r(rest); r >> k; std::getline(r, v); }
                k.erase(std::remove_if(k.begin(), k.end(), ::isspace), k.end());
                if (op == "param" && vars.count(k)) continue;  // overridden from outside
                vars[k] = cadEval(v, vars, &err);
                if (!err.empty()) err = name + ":" + std::to_string(lineNo) + ": " + err;
                continue;
            }
            if (op == "material") {
                std::stringstream ms(rest);
                std::string mname;
                ms >> mname;
                int id = cadMaterialId(mname);
                if (id < 0) err = "unknown material '" + mname + "'";
                else material = id;
                if (!err.empty()) err = name + ":" + std::to_string(lineNo) + ": " + err;
                continue;
            }
            if (op == "color") {
                std::stringstream cs(rest);
                std::string t;
                cs >> t;
                if (!t.empty() && t[0] == '#') {
                    unsigned c = unsigned(std::strtoul(t.c_str() + 1, nullptr, 16));
                    color = {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f};
                } else {
                    std::string g, b;
                    cs >> g >> b;
                    color = {cadEval(t, vars, &err), cadEval(g, vars, &err), cadEval(b, vars, &err)};
                }
                if (!err.empty()) err = name + ":" + std::to_string(lineNo) + ": " + err;
                continue;
            }
            std::map<std::string, std::string> a;
            std::vector<std::string> positional;
            {
                std::stringstream as(rest);
                std::string tok;
                while (as >> tok) {
                    auto eq = tok.find('=');
                    if (eq == std::string::npos) positional.push_back(tok);
                    else a[tok.substr(0, eq)] = tok.substr(eq + 1);
                }
            }
            if (op == "box") {
                Vec3 s = vec3(a, "size", {1, 1, 1});
                float bv = num(a, "bevel", 0.f);
                if (bv > 0) emit(cadprim::roundedBox(s, bv, int(num(a, "seg", 2)), color), a, "mb_rbox(" + fmt3(s) + "," + fmt(bv) + ")");
                else emit(cadprim::box(s, color), a, "cube(" + fmt3(s) + ", center=true)");
            } else if (op == "cyl") {
                float r = num(a, "r", 0.5f), r2 = num(a, "r2", r), h = num(a, "h", 1.f);
                int seg = int(num(a, "seg", 20));
                std::string ax = str(a, "axis", "y");
                CadMesh c;
                c.append(cadprim::cylinder(r, r2, h, seg, color), Mat4::trs({}, axisQuat(ax)));
                emit(c, a, axisScad(ax) + " mb_cyl(" + fmt(r) + "," + fmt(r2) + "," + fmt(h) + "," + std::to_string(seg) + ")");
            } else if (op == "sphere") {
                float r = num(a, "r", 0.5f);
                int seg = int(num(a, "seg", 16));
                emit(cadprim::sphere(r, seg, color), a, "sphere(r=" + fmt(r) + ", $fn=" + std::to_string(seg) + ")");
            } else if (op == "wedge") {
                Vec3 s = vec3(a, "size", {1, 1, 1});
                emit(cadprim::wedge(s, color), a, "mb_wedge(" + fmt3(s) + ")");
            } else if (op == "lathe" || op == "extrude") {
                auto p = pts(a);
                std::string list = "[";
                for (size_t k = 0; k < p.size(); k++) list += (k ? "," : "") + std::string("[") + fmt(p[k].x) + "," + fmt(p[k].y) + "]";
                list += "]";
                std::string ax = str(a, "axis", op == "lathe" ? "y" : "x");
                CadMesh c;
                if (op == "lathe") {
                    int seg = int(num(a, "seg", 24));
                    Quat q = ax == "x" ? axisQuat("x") : (ax == "z" ? axisQuat("z") : Quat{});
                    c.append(cadprim::lathe(p, seg, color), Mat4::trs({}, q));
                    emit(c, a, axisScad(ax) + " mb_lathe(" + list + "," + std::to_string(seg) + ")");
                } else {
                    float d = num(a, "depth", 1.f), bv = num(a, "bevel", 0.f);
                    c = cadprim::extrudeBevel(p, d, bv, int(num(a, "seg", 2)), color);
                    emit(c, a, "mb_extrude(" + list + "," + fmt(d) + ")");
                }
            } else if (op == "tire") {
                float r = num(a, "r", 0.44f), w = num(a, "w", 0.32f), rimR = num(a, "rim", 0.19f), d = num(a, "depth", 0.03f);
                int lugs = int(num(a, "lugs", 18));
                std::string t = str(a, "tread", "mt");
                int tr = t == "paddle" ? 2 : (t == "at" ? 0 : (t == "mt" ? 1 : int(cadEval(t, vars, &err) + 0.5f)));
                emit(cadprim::tire(r, w, rimR, lugs, d, tr, color), a,
                     "mb_tire(" + fmt(r) + "," + fmt(w) + "," + fmt(rimR) + "," + std::to_string(lugs) + "," + fmt(d) + "," +
                         std::to_string(tr) + ")",
                     MatRubber);
            } else if (op == "rim") {
                float r = num(a, "r", 0.19f), w = num(a, "w", 0.25f);
                int sp = int(num(a, "spokes", 5));
                emit(cadprim::rim(r, w, sp, color), a, "mb_rim(" + fmt(r) + "," + fmt(w) + "," + std::to_string(sp) + ")");
            } else if (op == "use") {
                if (positional.empty()) { err = "use needs a path"; break; }
                if (depth > 12) { err = "use nesting too deep"; break; }
                CadParams ov;
                for (auto& kv : a) {
                    if (kv.first == "pos" || kv.first == "rot" || kv.first == "scale" || kv.first == "mirror") continue;
                    ov[kv.first] = cadEval(kv.second, vars, &err);
                }
                std::string src2;
                std::string path = cadRoot() + "/" + positional[0];
                if (!readFile(path, src2)) { err = "cannot read " + path; break; }
                Interp child;
                child.vars = ov;
                child.color = color;
                child.material = material;
                child.depth = depth + 1;
                child.indent = indent + "  ";
                child.run(src2, positional[0]);
                if (!child.err.empty()) { err = child.err; break; }
                emit(child.mesh, a, "{\n" + child.scad + indent + "}", -1);
            } else {
                err = "unknown op '" + op + "'";
            }
            if (!err.empty()) err = name + ":" + std::to_string(lineNo) + ": " + err;
        }
    }
};

std::map<std::string, CadResult>& cache() {
    static std::map<std::string, CadResult> c;
    return c;
}
}  // namespace

CadResult cadBuildSource(const std::string& source, const CadParams& overrides, const std::string& name) {
    Interp in;
    in.vars = overrides;
    in.run(source, name);
    CadResult r;
    r.mesh = std::move(in.mesh);
    r.error = in.err;
    // Y-up/+Z-forward game space -> OpenSCAD Z-up.
    r.scad = "// Generated by mbcad from " + name + "\nuse <../lib/mbcad.scad>\nrotate([90,0,0]) {\n" + in.scad + "}\n";
    return r;
}

CadResult cadBuild(const std::string& partPath, const CadParams& overrides) {
    std::string key = partPath;
    for (auto& kv : overrides) key += ";" + kv.first + "=" + fmt(kv.second);
    auto it = cache().find(key);
    if (it != cache().end()) return it->second;
    std::string src;
    CadResult r;
    if (!readFile(cadRoot() + "/" + partPath, src)) {
        r.error = "cannot read " + cadRoot() + "/" + partPath;
        return r;
    }
    r = cadBuildSource(src, overrides, partPath);
    cache()[key] = r;
    return r;
}

std::string cadToObj(const CadMesh& m, const std::string& name) {
    std::string o = "# mbcad export: " + name + "\n# vertex colors as 'v x y z r g b'\no " + name + "\n";
    char b[160];
    for (auto& v : m.verts) {
        std::snprintf(b, sizeof b, "v %.5f %.5f %.5f %.3f %.3f %.3f\n", v.px, v.py, v.pz, v.r, v.g, v.b);
        o += b;
    }
    for (auto& v : m.verts) {
        std::snprintf(b, sizeof b, "vn %.4f %.4f %.4f\n", v.nx, v.ny, v.nz);
        o += b;
    }
    for (size_t i = 0; i + 2 < m.idx.size(); i += 3) {
        std::snprintf(b, sizeof b, "f %u//%u %u//%u %u//%u\n", m.idx[i] + 1, m.idx[i] + 1, m.idx[i + 1] + 1, m.idx[i + 1] + 1,
                      m.idx[i + 2] + 1, m.idx[i + 2] + 1);
        o += b;
    }
    return o;
}

}  // namespace mb
