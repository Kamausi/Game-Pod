#include "cad.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <vector>
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
    // The cofactor carries det(m): under a mirror it would point every normal inward.
    if (a * cof[0] + b * cof[1] + c * cof[2] < 0)
        for (float& k : cof) k = -k;
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


static Vec2 crom2(Vec2 a, Vec2 b, Vec2 c, Vec2 d, float t) {
    float t2 = t * t, t3 = t2 * t;
    return {0.5f * (2 * b.x + (-a.x + c.x) * t + (2 * a.x - 5 * b.x + 4 * c.x - d.x) * t2 + (-a.x + 3 * b.x - 3 * c.x + d.x) * t3),
            0.5f * (2 * b.y + (-a.y + c.y) * t + (2 * a.y - 5 * b.y + 4 * c.y - d.y) * t2 + (-a.y + 3 * b.y - 3 * c.y + d.y) * t3)};
}
static float crom1(float a, float b, float c, float d, float t) {
    float t2 = t * t, t3 = t2 * t;
    return 0.5f * (2 * b + (-a + c) * t + (2 * a - 5 * b + 4 * c - d) * t2 + (-a + 3 * b - 3 * c + d) * t3);
}

CadMesh tube(const std::vector<Vec3>& path, float r, int seg, Vec3 color) {
    CadMesh m;
    if (path.size() < 2 || r <= 0) return m;
    seg = std::max(3, seg);
    size_t n = path.size();
    std::vector<Vec3> T(n);
    for (size_t i = 0; i < n; i++) {
        Vec3 d = path[std::min(i + 1, n - 1)] - path[i > 0 ? i - 1 : 0];
        T[i] = normalize(d);
    }
    // Initial normal: any vector not parallel to the first tangent; then parallel transport.
    Vec3 up = std::fabs(T[0].y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    Vec3 N = normalize(cross(cross(T[0], up), T[0]));
    std::vector<Vec3> Ns(n), Bs(n);
    for (size_t i = 0; i < n; i++) {
        if (i > 0) {
            Vec3 ax = cross(T[i - 1], T[i]);
            float s = length(ax);
            if (s > 1e-6f) {
                float ang = std::atan2(s, dot(T[i - 1], T[i]));
                N = Quat::axisAngle(ax * (1.f / s), ang).rotate(N);
            }
            N = normalize(N - T[i] * dot(N, T[i]));
        }
        Ns[i] = N;
        Bs[i] = cross(T[i], N);
    }
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k <= seg; k++) {
            float a = 2 * PI * k / seg;
            Vec3 nn = Ns[i] * std::cos(a) + Bs[i] * std::sin(a);
            Vec3 p = path[i] + nn * r;
            m.verts.push_back({p.x, p.y, p.z, nn.x, nn.y, nn.z, color.x, color.y, color.z, 0});
        }
    auto id = [&](size_t i, int k) { return uint32_t(i * (seg + 1) + k); };
    for (size_t i = 0; i + 1 < n; i++)
        for (int k = 0; k < seg; k++) {
            uint32_t a = id(i, k), b = id(i, k + 1), c = id(i + 1, k + 1), d = id(i + 1, k);
            m.idx.insert(m.idx.end(), {a, c, b, a, d, c});
        }
    // Caps
    for (int e = 0; e < 2; e++) {
        size_t i = e ? n - 1 : 0;
        Vec3 nn = e ? T[i] : T[i] * -1.f;
        uint32_t c0 = uint32_t(m.verts.size());
        m.verts.push_back({path[i].x, path[i].y, path[i].z, nn.x, nn.y, nn.z, color.x, color.y, color.z, 0});
        for (int k = 0; k <= seg; k++) {
            float a = 2 * PI * k / seg;
            Vec3 p = path[i] + (Ns[i] * std::cos(a) + Bs[i] * std::sin(a)) * r;
            m.verts.push_back({p.x, p.y, p.z, nn.x, nn.y, nn.z, color.x, color.y, color.z, 0});
        }
        for (int k = 0; k < seg; k++) {
            if (e) m.idx.insert(m.idx.end(), {c0, c0 + 1 + uint32_t(k), c0 + 2 + uint32_t(k)});
            else m.idx.insert(m.idx.end(), {c0, c0 + 2 + uint32_t(k), c0 + 1 + uint32_t(k)});
        }
    }
    return m;
}

CadMesh loft(const std::vector<float>& zs, const std::vector<std::vector<Vec2>>& half, int n, int m, bool caps, Vec3 color, bool full) {
    CadMesh out;
    size_t S = zs.size();
    if (S < 2 || half.size() != S) return out;
    size_t P = half[0].size();
    for (auto& h : half) if (h.size() != P || P < 2) return out;
    n = std::max(1, n); m = std::max(1, m);
    // Closed loops: half profile, then the mirror back down (skipping points on the centreline).
    std::vector<std::vector<Vec2>> loops(S);
    for (size_t s = 0; s < S; s++) {
        auto& L = loops[s];
        for (auto& p : half[s]) L.push_back(p);
        if (full) continue;   // closed, asymmetric section given in full
        for (size_t i = P; i-- > 0;) {
            Vec2 q{-half[s][i].x, half[s][i].y};
            if (std::fabs(half[s][i].x) < 1e-5f) continue;
            L.push_back(q);
        }
        // drop duplicate centreline points at the seam (first == last after mirroring)
    }
    size_t Lc = loops[0].size();
    for (auto& L : loops) if (L.size() != Lc) return out;   // centreline points must match across stations
    // Smooth round the loop
    std::vector<std::vector<Vec2>> rings(S);
    for (size_t s = 0; s < S; s++) {
        auto& L = loops[s];
        for (size_t i = 0; i < Lc; i++)
            for (int k = 0; k < m; k++)
                rings[s].push_back(crom2(L[(i + Lc - 1) % Lc], L[i], L[(i + 1) % Lc], L[(i + 2) % Lc], float(k) / m));
    }
    size_t R = rings[0].size();
    // Smooth along z (clamped ends)
    std::vector<std::vector<Vec3>> grid;
    for (size_t s = 0; s + 1 < S; s++)
        for (int k = 0; k < n || (s + 2 == S && k == n); k++) {
            float t = float(k) / n;
            size_t a = s > 0 ? s - 1 : 0, b = s, c = s + 1, d = std::min(s + 2, S - 1);
            float z = crom1(zs[a], zs[b], zs[c], zs[d], t);
            std::vector<Vec3> row(R);
            for (size_t j = 0; j < R; j++) {
                Vec2 q = crom2(rings[a][j], rings[b][j], rings[c][j], rings[d][j], t);
                row[j] = {q.x, q.y, z};
            }
            grid.push_back(row);
        }
    size_t G = grid.size();
    // Smooth normals from the grid
    for (size_t i = 0; i < G; i++)
        for (size_t j = 0; j <= R; j++) {
            size_t jj = j % R;
            Vec3 du = grid[i][(jj + 1) % R] - grid[i][(jj + R - 1) % R];
            Vec3 dv = grid[std::min(i + 1, G - 1)][jj] - grid[i > 0 ? i - 1 : 0][jj];
            Vec3 nn = normalize(cross(dv, du));
            Vec3 p = grid[i][jj];
            out.verts.push_back({p.x, p.y, p.z, nn.x, nn.y, nn.z, color.x, color.y, color.z, 0});
        }
    auto id = [&](size_t i, size_t j) { return uint32_t(i * (R + 1) + j); };
    for (size_t i = 0; i + 1 < G; i++)
        for (size_t j = 0; j < R; j++) {
            uint32_t a = id(i, j), b = id(i, j + 1), c = id(i + 1, j + 1), d = id(i + 1, j);
            out.idx.insert(out.idx.end(), {a, b, c, a, c, d});
        }
    // Outward check: flip winding (and normals) if the normals point at the section centre.
    {
        Vec3 c{0, 0, 0};
        for (auto& p : grid[G / 2]) c = c + p;
        c = c * (1.f / R);
        const CadVertex& v = out.verts[id(G / 2, 0)];
        Vec3 o{v.px - c.x, v.py - c.y, v.pz - c.z};
        if (o.x * v.nx + o.y * v.ny + o.z * v.nz < 0) {
            for (auto& vv : out.verts) { vv.nx = -vv.nx; vv.ny = -vv.ny; vv.nz = -vv.nz; }
            for (size_t k = 0; k + 2 < out.idx.size(); k += 3) std::swap(out.idx[k + 1], out.idx[k + 2]);
        }
        // Make the triangle winding agree with the (now outward) normals.
        const CadVertex &A = out.verts[out.idx[0]], &B = out.verts[out.idx[1]], &C = out.verts[out.idx[2]];
        Vec3 fn = cross(Vec3{B.px - A.px, B.py - A.py, B.pz - A.pz}, Vec3{C.px - A.px, C.py - A.py, C.pz - A.pz});
        if (fn.x * A.nx + fn.y * A.ny + fn.z * A.nz < 0)
            for (size_t k = 0; k + 2 < out.idx.size(); k += 3) std::swap(out.idx[k + 1], out.idx[k + 2]);
    }
    if (caps) {
        for (int e = 0; e < 2; e++) {
            size_t i = e ? G - 1 : 0;
            Vec3 c{0, 0, 0};
            for (auto& p : grid[i]) c = c + p;
            c = c * (1.f / R);
            Vec3 nn{0, 0, e ? 1.f : -1.f};
            uint32_t c0 = uint32_t(out.verts.size());
            out.verts.push_back({c.x, c.y, c.z, nn.x, nn.y, nn.z, color.x, color.y, color.z, 0});
            for (size_t j = 0; j <= R; j++) {
                Vec3 p = grid[i][j % R];
                out.verts.push_back({p.x, p.y, p.z, nn.x, nn.y, nn.z, color.x, color.y, color.z, 0});
            }
            for (size_t j = 0; j < R; j++) {
                uint32_t a = c0 + 1 + uint32_t(j), b = c0 + 2 + uint32_t(j);
                Vec3 fn = cross(Vec3{out.verts[a].px - c.x, out.verts[a].py - c.y, 0}, Vec3{out.verts[b].px - c.x, out.verts[b].py - c.y, 0});
                if ((fn.z > 0) == (e == 1)) out.idx.insert(out.idx.end(), {c0, a, b});
                else out.idx.insert(out.idx.end(), {c0, b, a});
            }
        }
    }
    return out;
}


CadMesh liner(float r, float w, float a0, float a1, int seg, Vec3 color, float rb, float pe) {
    // Open partial cylinder (or superellipse) along X, faces pointing at the axis (a wheelhouse
    // seen from outside). rb <= 0 means a circle of radius r.
    CadMesh m;
    seg = std::max(2, seg);
    if (rb <= 0) rb = r;
    for (int k = 0; k <= seg; k++) {
        float a = (a0 + (a1 - a0) * float(k) / seg) * DEG;
        float c = std::cos(a), sn = std::sin(a);
        float z = (c < 0 ? -1.f : 1.f) * std::pow(std::fabs(c), 2.f / pe) * r;
        float y = (sn < 0 ? -1.f : 1.f) * std::pow(std::fabs(sn), 2.f / pe) * rb;
        // inward normal = -gradient of |z/r|^p + |y/rb|^p
        float gz = (z < 0 ? -1.f : 1.f) * std::pow(std::fabs(z) / r, pe - 1.f) / r;
        float gy = (y < 0 ? -1.f : 1.f) * std::pow(std::fabs(y) / rb, pe - 1.f) / rb;
        float gl = std::max(1e-6f, std::sqrt(gz * gz + gy * gy));
        for (int e = 0; e < 2; e++)
            m.verts.push_back({e ? w * 0.5f : -w * 0.5f, y, z, 0, -gy / gl, -gz / gl, color.x, color.y, color.z, 0});
    }
    for (int k = 0; k < seg; k++) {
        uint32_t a = uint32_t(2 * k), b = a + 1, c = a + 3, d = a + 2;
        m.idx.insert(m.idx.end(), {a, c, b, a, d, c});
    }
    // Wind so the faces look along their (inward) normals.
    const CadVertex &A = m.verts[m.idx[0]], &B = m.verts[m.idx[1]], &C = m.verts[m.idx[2]];
    Vec3 fn = cross(Vec3{B.px - A.px, B.py - A.py, B.pz - A.pz}, Vec3{C.px - A.px, C.py - A.py, C.pz - A.pz});
    if (fn.x * A.nx + fn.y * A.ny + fn.z * A.nz < 0)
        for (size_t k = 0; k + 2 < m.idx.size(); k += 3) std::swap(m.idx[k + 1], m.idx[k + 2]);
    return m;
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
    // Bead, rim guard, bulging sidewall, rounded shoulder, flat crown (mirrored for both sides).
    float sw = rt - rimR;
    std::vector<Vec2> half{{rimR, hw * 0.80f},          {rimR + sw * 0.07f, hw * 0.93f}, {rimR + sw * 0.14f, hw * 0.91f},
                           {rimR + sw * 0.32f, hw * 1.00f}, {rimR + sw * 0.55f, hw * 1.04f}, {rimR + sw * 0.78f, hw * 1.02f},
                           {rt - sh, hw * 0.98f},           {rt - sh * 0.35f, hw - sh * 0.35f}, {rt, hw - sh}};
    std::vector<Vec2> prof;
    for (auto it = half.begin(); it != half.end(); ++it) prof.push_back({it->x, -it->y});
    for (auto it = half.rbegin(); it != half.rend(); ++it) prof.push_back(*it);
    CadMesh m = lathe(prof, 48, color);
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

// Block-letter text from box strokes (same glyphs as tools/cad_letters.py): centred on the local
// XY plane, facing +Z, cap height 1.4*size. Unknown characters are reported through *bad.
CadMesh text(const std::string& str, float size, float t, float depth, Vec3 color, char* bad) {
    static const std::map<char, std::vector<float>> G = {
        {'M', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,1.4f,0.5f,0.7f,1.f,1.4f,0.5f,0.7f}},
        {'U', {0.f,0.1f,0.f,1.4f,1.f,0.1f,1.f,1.4f,0.f,0.f,1.f,0.f}},
        {'D', {0.f,0.f,0.f,1.4f,0.f,1.4f,0.7f,1.4f,0.f,0.f,0.7f,0.f,1.f,0.25f,1.f,1.15f,0.7f,1.4f,1.f,1.15f,0.7f,0.f,1.f,0.25f}},
        {'B', {0.f,0.f,0.f,1.4f,0.f,1.4f,0.8f,1.4f,0.f,0.7f,0.8f,0.7f,0.f,0.f,0.85f,0.f,0.95f,0.8f,0.95f,1.3f,1.f,0.1f,1.f,0.6f}},
        {'I', {0.5f,0.f,0.5f,1.4f,0.15f,1.4f,0.85f,1.4f,0.15f,0.f,0.85f,0.f}},
        {'E', {0.f,0.f,0.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.7f,0.75f,0.7f,0.f,0.f,1.f,0.f}},
        {'S', {0.f,1.4f,1.f,1.4f,0.f,0.7f,1.f,0.7f,0.f,0.f,1.f,0.f,0.f,0.7f,0.f,1.4f,1.f,0.f,1.f,0.7f}},
        {'O', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.f,1.f,0.f}},
        {'F', {0.f,0.f,0.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.7f,0.75f,0.7f}},
        {'R', {0.f,0.f,0.f,1.4f,0.f,1.4f,0.9f,1.4f,0.f,0.7f,0.9f,0.7f,1.f,0.75f,1.f,1.35f,0.4f,0.7f,1.f,0.f}},
        {'A', {0.f,0.f,0.f,1.3f,1.f,0.f,1.f,1.3f,0.f,1.4f,1.f,1.4f,0.f,0.7f,1.f,0.7f}},
        {'P', {0.f,0.f,0.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.7f,1.f,0.7f,1.f,0.7f,1.f,1.4f}},
        {'K', {0.f,0.f,0.f,1.4f,0.05f,0.7f,1.f,1.4f,0.3f,0.9f,1.f,0.f}},
        {'C', {0.f,0.f,0.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.f,1.f,0.f}},
        {'G', {0.f,0.f,0.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.f,1.f,0.f,1.f,0.f,1.f,0.6f,0.5f,0.6f,1.f,0.6f}},
        {'H', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,0.7f,1.f,0.7f}},
        {'J', {1.f,0.f,1.f,1.4f,0.f,0.f,1.f,0.f,0.f,0.f,0.f,0.4f}},
        {'L', {0.f,0.f,0.f,1.4f,0.f,0.f,1.f,0.f}},
        {'N', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,1.4f,1.f,0.f}},
        {'Q', {0.f,0.f,0.f,1.4f,1.f,0.2f,1.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.f,0.8f,0.f,0.6f,0.35f,1.f,-0.1f}},
        {'T', {0.5f,0.f,0.5f,1.4f,0.f,1.4f,1.f,1.4f}},
        {'V', {0.f,1.4f,0.5f,0.f,1.f,1.4f,0.5f,0.f}},
        {'W', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,0.f,0.5f,0.7f,1.f,0.f,0.5f,0.7f}},
        {'X', {0.f,0.f,1.f,1.4f,0.f,1.4f,1.f,0.f}},
        {'Y', {0.f,1.4f,0.5f,0.7f,1.f,1.4f,0.5f,0.7f,0.5f,0.f,0.5f,0.7f}},
        {'Z', {0.f,1.4f,1.f,1.4f,1.f,1.4f,0.f,0.f,0.f,0.f,1.f,0.f}},
        {'0', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.f,1.f,0.f,0.f,0.f,1.f,1.4f}},
        {'1', {0.5f,0.f,0.5f,1.4f,0.2f,1.15f,0.5f,1.4f,0.15f,0.f,0.85f,0.f}},
        {'2', {0.f,1.4f,1.f,1.4f,1.f,0.7f,1.f,1.4f,0.f,0.7f,1.f,0.7f,0.f,0.f,0.f,0.7f,0.f,0.f,1.f,0.f}},
        {'3', {0.f,1.4f,1.f,1.4f,0.3f,0.7f,1.f,0.7f,0.f,0.f,1.f,0.f,1.f,0.f,1.f,1.4f}},
        {'4', {0.f,0.7f,0.f,1.4f,0.f,0.7f,1.f,0.7f,1.f,0.f,1.f,1.4f}},
        {'5', {0.f,1.4f,1.f,1.4f,0.f,0.7f,0.f,1.4f,0.f,0.7f,1.f,0.7f,1.f,0.f,1.f,0.7f,0.f,0.f,1.f,0.f}},
        {'6', {0.f,0.f,0.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.7f,1.f,0.7f,1.f,0.f,1.f,0.7f,0.f,0.f,1.f,0.f}},
        {'7', {0.f,1.4f,1.f,1.4f,1.f,1.4f,0.4f,0.f}},
        {'8', {0.f,0.f,0.f,1.4f,1.f,0.f,1.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.7f,1.f,0.7f,0.f,0.f,1.f,0.f}},
        {'9', {1.f,0.f,1.f,1.4f,0.f,1.4f,1.f,1.4f,0.f,0.7f,1.f,0.7f,0.f,0.7f,0.f,1.4f,0.f,0.f,1.f,0.f}},
        {'!', {0.5f,0.35f,0.5f,1.4f,0.5f,0.f,0.5f,0.1f}},
        {'&', {0.f,0.f,0.f,0.7f,0.f,0.f,0.8f,0.f,0.f,0.7f,0.6f,1.4f,0.6f,1.4f,0.9f,1.15f,0.2f,1.1f,1.f,0.f}},
        {'\'', {0.5f,1.0f,0.5f,1.4f}},
        {':', {0.5f,0.25f,0.5f,0.45f,0.5f,0.95f,0.5f,1.15f}},
        {'.', {0.5f,0.f,0.5f,0.2f}},
        {'/', {0.f,0.f,1.f,1.4f}},
        {'-', {0.2f,0.7f,0.8f,0.7f}},
        {' ', {}}
    };
    CadMesh out;
    const float adv = 1.45f;
    float width = (float(str.size()) * adv - 0.45f) * size, x0 = -width * 0.5f;
    for (size_t i = 0; i < str.size(); i++) {
        auto it = G.find(str[i]);
        if (it == G.end()) { if (bad && !*bad) *bad = str[i]; continue; }
        const std::vector<float>& sg = it->second;
        for (size_t k = 0; k + 3 < sg.size(); k += 4) {
            float X0 = x0 + (float(i) * adv + sg[k]) * size, Y0 = (sg[k + 1] - 0.7f) * size;
            float X1 = x0 + (float(i) * adv + sg[k + 2]) * size, Y1 = (sg[k + 3] - 0.7f) * size;
            float len = std::sqrt((X1 - X0) * (X1 - X0) + (Y1 - Y0) * (Y1 - Y0)) + t;
            Mat4 M = Mat4::trs({(X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, 0}, Quat::axisAngle({0, 0, 1}, std::atan2(Y1 - Y0, X1 - X0)), {1, 1, 1});
            out.append(box({len, t, depth}, color), M);
        }
    }
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
            std::string tok = s.substr(st, i - st);
            char* end = nullptr;
            float v = std::strtof(tok.c_str(), &end);
            if (end != tok.c_str() + tok.size()) fail("bad number '" + tok + "'");   // e.g. 3.18.0
            return v;
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
                if (id == "atan2") return std::atan2(A(0), A(1)) / DEG;   // degrees, like sin/cos
                if (id == "tan") return std::tan(A(0) * DEG);
                if (id == "floor") return std::floor(A(0));
                if (id == "ceil") return std::ceil(A(0));
                if (id == "round") return std::round(A(0));
                if (id == "mod") { float b = A(1); return b != 0.f ? A(0) - b * std::floor(A(0) / b) : 0.f; }
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
// ------------------------------------------------------------------------------------------
// Body shaping: a smooth deformation field applied to a finished part (deform op). Large
// triangles are first split edge by edge (an edge splits only on its own length, so neighbours
// always agree and no cracks open), then every vertex moves through the field and normals are
// carried through the field's Jacobian. Gives flat-sided bodies tumblehome, barrelled sides,
// tapered noses and tails, a plan-view bow and crowned hoods and roofs, with every detail on
// the panels following the surface.
struct DeformSpec {
    bool on = false;
    float res = 0.10f;                                   // edges shorter than this never split
    float tol = 0.003f;                                  // ...and longer ones only if they'd bend more
    float tumble = 0, ty0 = 0, ty1 = 1;                  // narrow toward the roof between ty0..ty1
    float barrel = 0, by0 = 0, by1 = 1;                  // bulge the sides, peaking mid by0..by1
    float bow = 0, bz0 = -2, bz1 = 2;                    // plan-view bow, peaking mid bz0..bz1
    float taperf = 0, tf0 = 1, tf1 = 2;                  // narrow the nose from tf0 to tf1 (z)
    float taperr = 0, tr0 = -1, tr1 = -2;                // narrow the tail from tr0 to tr1 (z)
    float crown = 0, cy0 = 0, cy1 = 1, hw = 1;           // raise the centreline above cy0..cy1
};
static float smooth01(float a, float b, float v) {
    if (std::fabs(b - a) < 1e-6f) return v >= b ? 1.f : 0.f;
    float t = std::min(1.f, std::max(0.f, (v - a) / (b - a)));
    return t * t * (3 - 2 * t);
}
static float bell01(float a, float b, float v) {
    if (std::fabs(b - a) < 1e-6f) return 0.f;
    float u = std::min(1.f, std::max(0.f, (v - a) / (b - a)));
    return 4.f * u * (1.f - u);
}
static Vec3 deformPoint(const DeformSpec& d, Vec3 p) {
    float s = (1.f - d.tumble * smooth01(d.ty0, d.ty1, p.y)) * (1.f + d.barrel * bell01(d.by0, d.by1, p.y)) *
              (1.f + d.bow * bell01(d.bz0, d.bz1, p.z)) * (1.f - d.taperf * smooth01(d.tf0, d.tf1, p.z)) *
              (1.f - d.taperr * smooth01(d.tr0, d.tr1, p.z));
    float u = std::min(1.f, std::fabs(p.x) / std::max(0.05f, d.hw));
    float y = p.y + d.crown * (1.f - u * u) * smooth01(d.cy0, d.cy1, p.y);
    return {p.x * s, y, p.z};
}
static void subdivide(CadMesh& m, const DeformSpec& d) {
    const float res = std::max(0.02f, d.res);
    for (int pass = 0; pass < 10; pass++) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        auto edgeLen = [&](uint32_t a, uint32_t b) {
            const CadVertex &A = m.verts[a], &B = m.verts[b];
            float dx = A.px - B.px, dy = A.py - B.py, dz = A.pz - B.pz;
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        };
        auto split = [&](uint32_t a, uint32_t b) -> int64_t {
            if (edgeLen(a, b) <= res) return -1;
            {
                // Only where the field would bend the edge: deformed midpoint vs. the chord.
                const CadVertex &A = m.verts[a], &B = m.verts[b];
                Vec3 pa{A.px, A.py, A.pz}, pb{B.px, B.py, B.pz};
                Vec3 fm = deformPoint(d, (pa + pb) * 0.5f), fc = (deformPoint(d, pa) + deformPoint(d, pb)) * 0.5f;
                if (length(fm - fc) <= d.tol) return -1;
            }
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            const CadVertex &A = m.verts[a], &B = m.verts[b];
            CadVertex v{(A.px + B.px) * 0.5f, (A.py + B.py) * 0.5f, (A.pz + B.pz) * 0.5f,
                        A.nx + B.nx, A.ny + B.ny, A.nz + B.nz,
                        (A.r + B.r) * 0.5f, (A.g + B.g) * 0.5f, (A.b + B.b) * 0.5f, A.mat};
            float l = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
            if (l > 1e-6f) { v.nx /= l; v.ny /= l; v.nz /= l; } else { v.nx = A.nx; v.ny = A.ny; v.nz = A.nz; }
            m.verts.push_back(v);
            uint32_t id = uint32_t(m.verts.size() - 1);
            mid[key] = id;
            return id;
        };
        std::vector<uint32_t> out;
        out.reserve(m.idx.size() * 2);
        bool any = false;
        for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
            uint32_t a = m.idx[k], b = m.idx[k + 1], c = m.idx[k + 2];
            int64_t ab = split(a, b), bc = split(b, c), ca = split(c, a);
            int n = (ab >= 0) + (bc >= 0) + (ca >= 0);
            auto tri = [&](uint32_t x, uint32_t y, uint32_t z) { out.push_back(x); out.push_back(y); out.push_back(z); };
            if (n == 0) { tri(a, b, c); continue; }
            any = true;
            if (n == 3) {
                uint32_t p = uint32_t(ab), q = uint32_t(bc), r = uint32_t(ca);
                tri(a, p, r); tri(p, b, q); tri(r, q, c); tri(p, q, r);
                continue;
            }
            // Rotate so the split edges start at 'a'
            uint32_t v0 = a, v1 = b, v2 = c; int64_t e01 = ab, e12 = bc, e20 = ca;
            for (int rr = 0; rr < 3 && !(e01 >= 0 && (n == 1 || e12 >= 0)); rr++) {
                uint32_t t = v0; v0 = v1; v1 = v2; v2 = t;
                int64_t te = e01; e01 = e12; e12 = e20; e20 = te;
            }
            if (n == 1) {
                tri(v0, uint32_t(e01), v2); tri(uint32_t(e01), v1, v2);
            } else {
                uint32_t p = uint32_t(e01), q = uint32_t(e12);
                tri(p, v1, q); tri(v0, p, q); tri(v0, q, v2);
            }
        }
        m.idx.swap(out);
        if (!any) break;
    }
}
static void applyDeform(CadMesh& m, const DeformSpec& d) {
    if (!d.on || m.verts.empty()) return;
    subdivide(m, d);
    const float h = 0.002f;
    for (CadVertex& v : m.verts) {
        Vec3 p{v.px, v.py, v.pz};
        Vec3 c0 = deformPoint(d, p);
        Vec3 dx = (deformPoint(d, {p.x + h, p.y, p.z}) - deformPoint(d, {p.x - h, p.y, p.z})) * (0.5f / h);
        Vec3 dy = (deformPoint(d, {p.x, p.y + h, p.z}) - deformPoint(d, {p.x, p.y - h, p.z})) * (0.5f / h);
        Vec3 dz = (deformPoint(d, {p.x, p.y, p.z + h}) - deformPoint(d, {p.x, p.y, p.z - h})) * (0.5f / h);
        // n' = cof(J) n, J columns = dx, dy, dz
        Vec3 n{v.nx, v.ny, v.nz};
        Vec3 c1 = cross(dy, dz), c2 = cross(dz, dx), c3 = cross(dx, dy);
        Vec3 nn{c1.x * n.x + c2.x * n.y + c3.x * n.z, c1.y * n.x + c2.y * n.y + c3.y * n.z, c1.z * n.x + c2.z * n.y + c3.z * n.z};
        nn = normalize(nn);
        v.px = c0.x; v.py = c0.y; v.pz = c0.z;
        v.nx = nn.x; v.ny = nn.y; v.nz = nn.z;
    }
}


// Wheel-arch cut: remove everything outboard of |x| > xmin that lies inside the circle of radius r
// about (zc, yc) in the z-y plane. Edges near the circle are refined first (edge-local rule, so no
// cracks), then straddling triangles are clipped with intersection points bisected onto the circle.
static void applyCut(CadMesh& m, float zc, float yc, float ra, float rb, float pe, float xmin) {
    // Superellipse |dz/a|^p + |dy/b|^p = 1 (p = 2: the circle; ~3-4: squared truck arches),
    // as an approximate signed distance (positive inside the opening).
    const float r = std::min(ra, rb);
    auto f = [&](const CadVertex& v) {
        float S = std::pow(std::fabs(v.pz - zc) / ra, pe) + std::pow(std::fabs(v.py - yc) / rb, pe);
        return (1.f - std::pow(S, 1.f / pe)) * r;
    };
    const float res = 0.025f;
    for (int pass = 0; pass < 12; pass++) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> mid;
        bool any = false;
        auto split = [&](uint32_t a, uint32_t b) -> int64_t {
            const CadVertex &A = m.verts[a], &B = m.verts[b];
            if (std::max(std::fabs(A.px), std::fabs(B.px)) <= xmin) return -1;
            float dx = A.px - B.px, dy = A.py - B.py, dz = A.pz - B.pz, len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len <= res) return -1;
            float fa = f(A), fb = f(B);
            CadVertex M{(A.px + B.px) * 0.5f, (A.py + B.py) * 0.5f, (A.pz + B.pz) * 0.5f, 0, 0, 0, 0, 0, 0, 0};
            float fm = f(M);
            bool near = (fa > 0) != (fb > 0) || std::min(std::fabs(fa), std::min(std::fabs(fb), std::fabs(fm))) < len;
            if (!near) return -1;
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = mid.find(key);
            if (it != mid.end()) return it->second;
            CadVertex v{M.px, M.py, M.pz, A.nx + B.nx, A.ny + B.ny, A.nz + B.nz,
                        (A.r + B.r) * 0.5f, (A.g + B.g) * 0.5f, (A.b + B.b) * 0.5f, A.mat};
            float l = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
            if (l > 1e-6f) { v.nx /= l; v.ny /= l; v.nz /= l; } else { v.nx = A.nx; v.ny = A.ny; v.nz = A.nz; }
            m.verts.push_back(v);
            uint32_t id = uint32_t(m.verts.size() - 1);
            mid[key] = id;
            any = true;
            return id;
        };
        std::vector<uint32_t> out;
        out.reserve(m.idx.size());
        for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
            uint32_t a = m.idx[k], b = m.idx[k + 1], c = m.idx[k + 2];
            int64_t ab = split(a, b), bc = split(b, c), ca = split(c, a);
            int n = (ab >= 0) + (bc >= 0) + (ca >= 0);
            auto tri = [&](uint32_t x, uint32_t y, uint32_t z) { out.push_back(x); out.push_back(y); out.push_back(z); };
            if (n == 0) { tri(a, b, c); continue; }
            if (n == 3) {
                uint32_t p = uint32_t(ab), q = uint32_t(bc), rr = uint32_t(ca);
                tri(a, p, rr); tri(p, b, q); tri(rr, q, c); tri(p, q, rr);
                continue;
            }
            uint32_t v0 = a, v1 = b, v2 = c; int64_t e01 = ab, e12 = bc, e20 = ca;
            for (int rr = 0; rr < 3 && !(e01 >= 0 && (n == 1 || e12 >= 0)); rr++) {
                uint32_t t = v0; v0 = v1; v1 = v2; v2 = t;
                int64_t te = e01; e01 = e12; e12 = e20; e20 = te;
            }
            if (n == 1) { tri(v0, uint32_t(e01), v2); tri(uint32_t(e01), v1, v2); }
            else { uint32_t p = uint32_t(e01), q = uint32_t(e12); tri(p, v1, q); tri(v0, p, q); tri(v0, q, v2); }
        }
        m.idx.swap(out);
        if (!any) break;
    }
    // Clip
    auto lerpV = [&](const CadVertex& A, const CadVertex& B) {
        // bisect for f = 0 along A (outside, f<=0) -> B (inside, f>0)
        float lo = 0, hi = 1;
        for (int it = 0; it < 14; it++) {
            float t = (lo + hi) * 0.5f;
            CadVertex M{A.px + (B.px - A.px) * t, A.py + (B.py - A.py) * t, A.pz + (B.pz - A.pz) * t, 0, 0, 0, 0, 0, 0, 0};
            if (f(M) > 0) hi = t; else lo = t;
        }
        float t = lo;
        CadVertex v{A.px + (B.px - A.px) * t, A.py + (B.py - A.py) * t, A.pz + (B.pz - A.pz) * t,
                    A.nx + (B.nx - A.nx) * t, A.ny + (B.ny - A.ny) * t, A.nz + (B.nz - A.nz) * t,
                    A.r, A.g, A.b, A.mat};
        float l = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (l > 1e-6f) { v.nx /= l; v.ny /= l; v.nz /= l; }
        return v;
    };
    std::vector<uint32_t> out;
    out.reserve(m.idx.size());
    for (size_t k = 0; k + 2 < m.idx.size(); k += 3) {
        uint32_t id[3] = {m.idx[k], m.idx[k + 1], m.idx[k + 2]};
        float cx = (m.verts[id[0]].px + m.verts[id[1]].px + m.verts[id[2]].px) / 3.f;
        if (std::fabs(cx) <= xmin) { out.insert(out.end(), id, id + 3); continue; }
        bool in[3];
        int nin = 0;
        for (int e = 0; e < 3; e++) { in[e] = f(m.verts[id[e]]) > 0; nin += in[e]; }
        if (nin == 0) { out.insert(out.end(), id, id + 3); continue; }
        if (nin == 3) continue;
        std::vector<uint32_t> poly;
        for (int e = 0; e < 3; e++) {
            uint32_t a = id[e], b = id[(e + 1) % 3];
            bool ia = in[e], ib = in[(e + 1) % 3];
            if (!ia) poly.push_back(a);
            if (ia != ib) {
                CadVertex v = ia ? lerpV(m.verts[b], m.verts[a]) : lerpV(m.verts[a], m.verts[b]);
                m.verts.push_back(v);
                poly.push_back(uint32_t(m.verts.size() - 1));
            }
        }
        for (size_t t = 1; t + 1 < poly.size(); t++) { out.push_back(poly[0]); out.push_back(poly[t]); out.push_back(poly[t + 1]); }
    }
    m.idx.swap(out);
}

struct Interp {
    CadParams vars;
    Vec3 color{0.7f, 0.7f, 0.7f};
    int material = MatPlastic;
    CadMesh mesh;
    DeformSpec deform;
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
    // Segment counts scale with the part's _segmul / _bevelmul smoothness settings.
    int segs(const std::map<std::string, std::string>& a, int def, bool bevel = false) {
        int sg = int(num(a, "seg", float(def)));
        auto it = vars.find(bevel ? "_bevelmul" : "_segmul");
        if (it != vars.end() && it->second > 1.f) sg = int(std::ceil(sg * it->second));
        return sg;
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
            if (t.compare(0, 5, "sarc(") == 0 && t.back() == ')') {
                // sarc(zc, yc, a, b, p, a0, a1, n): n+1 points on a superellipse (squared arches)
                auto a = evalList(t.substr(5, t.size() - 6));
                if (a.size() < 8) { err = "sarc needs zc,yc,a,b,p,a0,a1,n"; return p; }
                for (size_t i = 0; i + 1 < l.size(); i += 2) p.push_back({l[i], l[i + 1]});
                l.clear();
                int cnt = std::max(1, int(a[7] + 0.5f));
                for (int k = 0; k <= cnt; k++) {
                    float ang = (a[5] + (a[6] - a[5]) * float(k) / float(cnt)) * DEG;
                    float c = std::cos(ang), sn = std::sin(ang);
                    p.push_back({a[0] + a[2] * (c < 0 ? -1.f : 1.f) * std::pow(std::fabs(c), 2.f / a[4]),
                                 a[1] + a[3] * (sn < 0 ? -1.f : 1.f) * std::pow(std::fabs(sn), 2.f / a[4])});
                }
                continue;
            }
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
        // Parts switch features off by scaling them to zero (hat*, vest*, beadlock*): emit nothing
        // for a primitive collapsed on any axis instead of a fan of degenerate triangles.
        if (prim.verts.empty() || std::fabs(scl.x * scl.y * scl.z) < 1e-9f) return;
        {
            Vec3 mn = prim.boundsMin(), mx = prim.boundsMax();
            if (mx.x - mn.x < 1e-6f || mx.y - mn.y < 1e-6f || mx.z - mn.z < 1e-6f) return;
        }
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
            if (op == "deform") {
                // deform [on=] [res=] [tol=] [tumble= ty0= ty1=] [barrel= by0= by1=] [bow= bz0= bz1=]
                //        [taperf= tf0= tf1=] [taperr= tr0= tr1=] [crown= cy0= cy1= hw=]
                DeformSpec& d = deform;
                d.on = num(a, "on", 1.f) > 0.5f;
                d.res = num(a, "res", d.res);
                d.tol = num(a, "tol", d.tol);
                d.tumble = num(a, "tumble", d.tumble); d.ty0 = num(a, "ty0", d.ty0); d.ty1 = num(a, "ty1", d.ty1);
                d.barrel = num(a, "barrel", d.barrel); d.by0 = num(a, "by0", d.by0); d.by1 = num(a, "by1", d.by1);
                d.bow = num(a, "bow", d.bow); d.bz0 = num(a, "bz0", d.bz0); d.bz1 = num(a, "bz1", d.bz1);
                d.taperf = num(a, "taperf", d.taperf); d.tf0 = num(a, "tf0", d.tf0); d.tf1 = num(a, "tf1", d.tf1);
                d.taperr = num(a, "taperr", d.taperr); d.tr0 = num(a, "tr0", d.tr0); d.tr1 = num(a, "tr1", d.tr1);
                d.crown = num(a, "crown", d.crown); d.cy0 = num(a, "cy0", d.cy0); d.cy1 = num(a, "cy1", d.cy1);
                d.hw = num(a, "hw", d.hw);
                if (!err.empty()) err = name + ":" + std::to_string(lineNo) + ": " + err;
                continue;
            }
            if (op == "box") {
                Vec3 s = vec3(a, "size", {1, 1, 1});
                float bv = num(a, "bevel", 0.f);
                if (bv > 0) emit(cadprim::roundedBox(s, bv, segs(a, 2, true), color), a, "mb_rbox(" + fmt3(s) + "," + fmt(bv) + ")");
                else emit(cadprim::box(s, color), a, "cube(" + fmt3(s) + ", center=true)");
            } else if (op == "text") {
                // text str=MUD_BUDDIES size=0.3 [t=0.07] [depth=0.02]   (underscores are spaces)
                std::string t = str(a, "str", "");
                std::replace(t.begin(), t.end(), '_', ' ');
                float sz = num(a, "size", 0.3f), th = num(a, "t", sz * 0.22f), dp = num(a, "depth", 0.02f);
                char bad = 0;
                CadMesh m = cadprim::text(t, sz, th, dp, color, &bad);
                if (bad) err = std::string("no glyph for '") + bad + "'";
                else emit(m, a, "mb_text(\"" + t + "\"," + fmt(sz) + ")");
            } else if (op == "cyl") {
                float r = num(a, "r", 0.5f), r2 = num(a, "r2", r), h = num(a, "h", 1.f);
                int seg = segs(a, 20);
                std::string ax = str(a, "axis", "y");
                CadMesh c;
                c.append(cadprim::cylinder(r, r2, h, seg, color), Mat4::trs({}, axisQuat(ax)));
                emit(c, a, axisScad(ax) + " mb_cyl(" + fmt(r) + "," + fmt(r2) + "," + fmt(h) + "," + std::to_string(seg) + ")");
            } else if (op == "sphere") {
                float r = num(a, "r", 0.5f);
                int seg = segs(a, 16);
                emit(cadprim::sphere(r, seg, color), a, "sphere(r=" + fmt(r) + ", $fn=" + std::to_string(seg) + ")");
            } else if (op == "cut") {
                // cut cyl=zc,yc,r [xmin=]: clip the wheel opening out of everything emitted so far
                // cut cyl=zc,yc,a[,b] [p=2] [xmin=]: b = height (defaults to a), p = squareness
                auto c = evalList(str(a, "cyl", "0,0,0.5"));
                if (c.size() < 3) err = "cut needs cyl=zc,yc,r";
                else applyCut(mesh, c[0], c[1], c[2], c.size() > 3 ? c[3] : c[2], std::max(1.f, num(a, "p", 2.f)), num(a, "xmin", 0.f));
            } else if (op == "liner") {
                // liner r= w= [a0=0] [a1=180] [seg=]: inward-facing partial cylinder along X (wheelhouse)
                emit(cadprim::liner(num(a, "r", 0.5f), num(a, "w", 0.3f), num(a, "a0", 0.f), num(a, "a1", 180.f), segs(a, 16), color,
                                    num(a, "b", 0.f), std::max(1.f, num(a, "p", 2.f))), a,
                     "/* liner */ cube(0.001)");
            } else if (op == "tube") {
                // tube pts=x,y,z,x,y,z,... r= [seg=]   or   tube helix=coil_r,turns,length r=wire [seg=]
                float rr = num(a, "r", 0.02f);
                int seg = segs(a, 8);
                std::vector<Vec3> path;
                if (a.count("helix")) {
                    auto h = evalList(a["helix"]);
                    if (h.size() < 3) err = "helix needs coil_r,turns,length";
                    else {
                        int steps = std::max(8, int(h[1] * 16));
                        for (int k = 0; k <= steps; k++) {
                            float t = float(k) / steps, ang = t * h[1] * 2 * PI;
                            path.push_back({h[0] * std::cos(ang), h[0] * std::sin(ang), t * h[2]});
                        }
                    }
                } else {
                    auto l = evalList(str(a, "pts", ""));
                    for (size_t i = 0; i + 2 < l.size(); i += 3) path.push_back({l[i], l[i + 1], l[i + 2]});
                    if (path.size() < 2) err = "tube needs at least two points";
                }
                if (err.empty()) emit(cadprim::tube(path, rr, seg, color), a, "/* tube */ cube(0.001)");
            } else if (op == "loft") {
                // loft sec=z:x,y,x,y,...|z:x,y,... [n=4] [m=2] [cap=1] [full=1]   (half profiles x >= 0, or full closed sections)
                std::vector<float> zs;
                std::vector<std::vector<Vec2>> half;
                std::string secs = str(a, "sec", "");
                size_t st = 0;
                while (st <= secs.size() && err.empty()) {
                    size_t bar = secs.find('|', st);
                    std::string one = secs.substr(st, bar == std::string::npos ? std::string::npos : bar - st);
                    size_t colon = one.find(':');
                    if (colon == std::string::npos) { err = "loft section needs z:points"; break; }
                    zs.push_back(cadEval(one.substr(0, colon), vars, &err));
                    auto l = evalList(one.substr(colon + 1));
                    std::vector<Vec2> h;
                    for (size_t i = 0; i + 1 < l.size(); i += 2) h.push_back({l[i], l[i + 1]});
                    half.push_back(h);
                    if (bar == std::string::npos) break;
                    st = bar + 1;
                }
                if (err.empty()) {
                    CadMesh c = cadprim::loft(zs, half, int(num(a, "n", 4)), int(num(a, "m", 2)), num(a, "cap", 1) > 0.5f, color,
                                              num(a, "full", 0) > 0.5f);
                    if (c.verts.empty()) err = "loft sections must have matching point counts";
                    else emit(c, a, "/* loft */ cube(0.001)");
                }
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
                    int seg = segs(a, 24);
                    Quat q = ax == "x" ? axisQuat("x") : (ax == "z" ? axisQuat("z") : Quat{});
                    c.append(cadprim::lathe(p, seg, color), Mat4::trs({}, q));
                    emit(c, a, axisScad(ax) + " mb_lathe(" + list + "," + std::to_string(seg) + ")");
                } else {
                    float d = num(a, "depth", 1.f), bv = num(a, "bevel", 0.f);
                    c = cadprim::extrudeBevel(p, d, bv, segs(a, 2, true), color);
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
                for (const char* k : {"_segmul", "_bevelmul"})
                    if (vars.count(k) && !ov.count(k)) ov[k] = vars[k];
                child.vars = ov;
                child.color = color;
                child.material = material;
                child.depth = depth + 1;
                child.indent = indent + "  ";
                child.run(src2, positional[0]);
                if (child.err.empty()) applyDeform(child.mesh, child.deform);
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
    if (in.err.empty()) applyDeform(in.mesh, in.deform);
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
