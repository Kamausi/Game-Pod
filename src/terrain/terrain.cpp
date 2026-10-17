#include "terrain.h"
#include <cstring>
#include "../physics/terramechanics.h"

namespace mb {

Terrain::Terrain() {
    const size_t n = size_t(N) * N;
    h.assign(n, 0.f);
    base.assign(n, -0.05f);
    water.assign(n, NO_WATER);
    moist.assign(n, 0.2f);
    comp.assign(n, 0.05f);
    grass.assign(n, 255);
    track.assign(n, 0);
}

static inline void bilinearSetup(const Terrain& t, float x, float z, int& i, int& j, float& fx, float& fz) {
    float fi, fj;
    t.worldToCell(x, z, fi, fj);
    fi = clampf(fi, 0.f, Terrain::N - 1.001f);
    fj = clampf(fj, 0.f, Terrain::N - 1.001f);
    i = int(fi); j = int(fj);
    fx = fi - i; fz = fj - j;
}

static inline float bilerp(const std::vector<float>& v, int i, int j, float fx, float fz) {
    const int N = Terrain::N;
    float a = v[j * N + i], b = v[j * N + i + 1], c = v[(j + 1) * N + i], d = v[(j + 1) * N + i + 1];
    return lerpf(lerpf(a, b, fx), lerpf(c, d, fx), fz);
}

float Terrain::heightAt(float x, float z) const {
    int i, j; float fx, fz;
    bilinearSetup(*this, x, z, i, j, fx, fz);
    return bilerp(h, i, j, fx, fz);
}

float Terrain::baseAt(float x, float z) const {
    int i, j; float fx, fz;
    bilinearSetup(*this, x, z, i, j, fx, fz);
    return bilerp(base, i, j, fx, fz);
}

Vec3 Terrain::normalAt(float x, float z) const {
    const float e = CELL;
    float hl = heightAt(x - e, z), hr = heightAt(x + e, z);
    float hd = heightAt(x, z - e), hu = heightAt(x, z + e);
    return normalize(Vec3{hl - hr, 2.f * e, hd - hu});
}

TerrainSample Terrain::sample(float x, float z) const {
    int i, j; float fx, fz;
    bilinearSetup(*this, x, z, i, j, fx, fz);
    TerrainSample s;
    s.height = bilerp(h, i, j, fx, fz);
    s.base = bilerp(base, i, j, fx, fz);
    s.moisture = bilerp(moist, i, j, fx, fz);
    s.compaction = bilerp(comp, i, j, fx, fz);
    int ni = fx < 0.5f ? i : i + 1, nj = fz < 0.5f ? j : j + 1;
    float wl = water[idx(ni, nj)];
    s.waterDepth = wl > NO_WATER + 1 ? std::max(0.f, wl - s.height) : 0.f;
    s.grass = grass[idx(ni, nj)] / 255.f;
    if (s.waterDepth > 0.f) s.moisture = 1.f;
    return s;
}

namespace {
struct PatchIter {
    int i0, i1, j0, j1;
};
PatchIter patchBounds(const Terrain& t, Vec3 c, float r) {
    float fi, fj;
    t.worldToCell(c.x, c.z, fi, fj);
    int rr = int(std::ceil(r / Terrain::CELL)) + 1;
    PatchIter p;
    p.i0 = std::max(0, int(fi) - rr); p.i1 = std::min(Terrain::N - 1, int(fi) + rr);
    p.j0 = std::max(0, int(fj) - rr); p.j1 = std::min(Terrain::N - 1, int(fj) + rr);
    return p;
}
}  // namespace

float Terrain::pressPatch(Vec3 center, Vec3 fwd, float halfLen, float halfWidth, float pressure, float dt) {
    Vec3 f = normalize(Vec3{fwd.x, 0, fwd.z});
    Vec3 side{f.z, 0, -f.x};
    PatchIter b = patchBounds(*this, center, std::max(halfLen, halfWidth));
    float total = 0, displaced = 0;
    int count = 0;
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) {
            Vec3 w = cellToWorld(i, j);
            float dx = w.x - center.x, dz = w.z - center.z;
            float lf = dx * f.x + dz * f.z, ls = dx * side.x + dz * side.z;
            if (std::fabs(lf) > halfLen || std::fabs(ls) > halfWidth) continue;
            int k = idx(i, j);
            count++;
            float avail = h[k] - base[k];
            if (avail <= 1e-4f) continue;
            float m = water[k] > h[k] ? 1.f : moist[k];
            float n = MudModel::n(m), kEq = MudModel::kEq(m);
            float pBear = bekkerPressure(kEq, n, comp[k]);
            float excess = pressure - pBear;
            if (excess <= 0) continue;
            float low = std::min(avail, excess / MudModel::viscosity(m) * dt);
            h[k] -= low;
            comp[k] += low * 0.75f;
            total += low;
            displaced += low * 0.25f;
            dirty.add(i, j);
        }
    // Push displaced material into side berms (rut walls).
    if (displaced > 0) {
        for (int s = -1; s <= 1; s += 2) {
            Vec3 p = center + side * (s * (halfWidth + CELL * 1.5f));
            float fi, fj;
            worldToCell(p.x, p.z, fi, fj);
            int steps = std::max(1, int(halfLen * 2 / CELL));
            for (int q = 0; q < steps; q++) {
                Vec3 pp = p + f * (-halfLen + (q + 0.5f) * (2 * halfLen / steps));
                worldToCell(pp.x, pp.z, fi, fj);
                int ii = int(fi + 0.5f), jj = int(fj + 0.5f);
                if (!inBounds(ii, jj)) continue;
                h[idx(ii, jj)] += displaced * 0.5f / steps;
                dirty.add(ii, jj);
            }
        }
    }
    return count ? total / count : 0.f;
}

float Terrain::digPatch(Vec3 center, Vec3 fwd, float halfLen, float halfWidth, float depth) {
    if (depth <= 0) return 0;
    Vec3 f = normalize(Vec3{fwd.x, 0, fwd.z});
    Vec3 side{f.z, 0, -f.x};
    PatchIter b = patchBounds(*this, center, std::max(halfLen, halfWidth));
    float vol = 0;
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) {
            Vec3 w = cellToWorld(i, j);
            float dx = w.x - center.x, dz = w.z - center.z;
            float lf = dx * f.x + dz * f.z, ls = dx * side.x + dz * side.z;
            if (std::fabs(lf) > halfLen || std::fabs(ls) > halfWidth) continue;
            int k = idx(i, j);
            float d = std::min(depth, h[k] - base[k]);
            if (d <= 0) continue;
            h[k] -= d;
            comp[k] *= 0.6f;  // churned
            vol += d * CELL * CELL;
            dirty.add(i, j);
        }
    return vol;
}

void Terrain::deposit(float x, float z, float volume, float radius) {
    if (volume <= 0) return;
    Vec3 c{x, 0, z};
    PatchIter b = patchBounds(*this, c, radius);
    float wsum = 0;
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) {
            Vec3 w = cellToWorld(i, j);
            float d2 = sq(w.x - x) + sq(w.z - z);
            if (d2 < radius * radius) wsum += 1.f - d2 / (radius * radius);
        }
    if (wsum <= 0) return;
    float scale = volume / (wsum * CELL * CELL);
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) {
            Vec3 w = cellToWorld(i, j);
            float d2 = sq(w.x - x) + sq(w.z - z);
            if (d2 >= radius * radius) continue;
            int k = idx(i, j);
            h[k] += scale * (1.f - d2 / (radius * radius));
            comp[k] = std::min(comp[k], 0.02f);
            moist[k] = std::max(moist[k], 0.55f);
            if (grass[k] > 40) grass[k] -= 40;
            dirty.add(i, j);
        }
}

void Terrain::scuff(Vec3 center, float radius, float grassLoss, float trackAdd) {
    PatchIter b = patchBounds(*this, center, radius);
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) {
            Vec3 w = cellToWorld(i, j);
            if (sq(w.x - center.x) + sq(w.z - center.z) > radius * radius) continue;
            int k = idx(i, j);
            int g = int(grass[k]) - int(grassLoss * 255.f + 0.5f);
            int t = int(track[k]) + int(trackAdd * 255.f + 0.5f);
            uint8_t ng = uint8_t(std::max(0, g)), nt = uint8_t(std::min(255, t));
            if (ng != grass[k] || nt != track[k]) {
                grass[k] = ng; track[k] = nt;
                dirty.add(i, j);
            }
        }
}

void Terrain::relax(Vec3 center, float radius, float dt) {
    PatchIter b = patchBounds(*this, center, radius);
    int w = b.i1 - b.i0 + 1, hgt = b.j1 - b.j0 + 1;
    if (w < 3 || hgt < 3) return;
    static std::vector<float> tmp;
    tmp.resize(size_t(w) * hgt);
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) tmp[(j - b.j0) * w + (i - b.i0)] = h[idx(i, j)];
    for (int j = b.j0 + 1; j < b.j1; j++)
        for (int i = b.i0 + 1; i < b.i1; i++) {
            int k = idx(i, j);
            float m = water[k] > h[k] ? 1.f : moist[k];
            if (m < 0.45f) continue;
            int lx = i - b.i0, lz = j - b.j0;
            float avg = 0.25f * (tmp[lz * w + lx - 1] + tmp[lz * w + lx + 1] + tmp[(lz - 1) * w + lx] + tmp[(lz + 1) * w + lx]);
            float rate = clampf(sq((m - 0.45f) / 0.55f) * 1.6f * g_tuning.reflow * dt, 0.f, 0.25f);
            float nh = tmp[lz * w + lx] + (avg - tmp[lz * w + lx]) * rate;
            nh = std::max(nh, base[k]);
            if (std::fabs(nh - h[k]) > 1e-5f) {
                h[k] = nh;
                dirty.add(i, j);
            }
            comp[k] = std::max(0.01f, comp[k] * std::exp(-m * 0.05f * dt));
        }
}

void Terrain::grade(Vec3 center, float radius, float target, float rate, float dt) {
    PatchIter b = patchBounds(*this, center, radius);
    for (int j = b.j0; j <= b.j1; j++)
        for (int i = b.i0; i <= b.i1; i++) {
            Vec3 w = cellToWorld(i, j);
            if (sq(w.x - center.x) + sq(w.z - center.z) > radius * radius) continue;
            int k = idx(i, j);
            float nh = approach(h[k], std::max(target, base[k]), rate, dt);
            if (std::fabs(nh - h[k]) > 1e-5f) {
                h[k] = nh;
                comp[k] = 0.03f;
                dirty.add(i, j);
            }
        }
}

}  // namespace mb
