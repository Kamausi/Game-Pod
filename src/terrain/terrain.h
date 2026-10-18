// Deformable multi-layer heightfield.
// Layers per cell: surface height, hardpan (base) height, standing-water level, moisture,
// compaction depth, vegetation cover and a "track" (tire mark) intensity.
#pragma once
#include <cstdint>
#include <vector>
#include "../core/math.h"

namespace mb {

struct TerrainSample {
    float height = 0, base = 0, moisture = 0, waterDepth = 0, grass = 0, compaction = 0;
    uint8_t soil = 0;      // Soil enum (loam, clay, silt, sand, peat)
    float disturb = 0;     // traffic/churn 0..1 (weakens remoldable soils)
    float mudDepth() const { return std::max(0.f, height - base); }
};

struct DirtyRect {
    int x0 = 1 << 30, z0 = 1 << 30, x1 = -1, z1 = -1;
    bool empty() const { return x1 < x0; }
    void add(int x, int z) {
        x0 = std::min(x0, x); z0 = std::min(z0, z);
        x1 = std::max(x1, x); z1 = std::max(z1, z);
    }
    void clear() { *this = DirtyRect{}; }
};

class Terrain {
public:
    static constexpr int N = 1536;          // cells per side
    static constexpr float CELL = 0.25f;    // meters per cell -> 384 m world
    static constexpr float NO_WATER = -1000.f;

    Terrain();
    float originX() const { return -N * CELL * 0.5f; }
    float originZ() const { return -N * CELL * 0.5f; }
    float size() const { return N * CELL; }

    int idx(int i, int j) const { return j * N + i; }
    bool inBounds(int i, int j) const { return i >= 0 && j >= 0 && i < N && j < N; }
    void worldToCell(float x, float z, float& fi, float& fj) const {
        fi = (x - originX()) / CELL;
        fj = (z - originZ()) / CELL;
    }
    Vec3 cellToWorld(int i, int j) const { return {originX() + i * CELL, h[idx(i, j)], originZ() + j * CELL}; }

    float heightAt(float x, float z) const;
    float baseAt(float x, float z) const;
    Vec3 normalAt(float x, float z) const;
    TerrainSample sample(float x, float z) const;

    // --- deformation API (used by tires, particles, dozer) ---
    // Press a rectangular patch (oriented by fwd) with contact pressure p (Pa).
    // Returns average lowering (m) applied this step.
    float pressPatch(Vec3 center, Vec3 fwd, float halfLen, float halfWidth, float pressure, float dt);
    // Slip excavation; returns excavated volume (m^3). Material is removed (thrown as particles).
    float digPatch(Vec3 center, Vec3 fwd, float halfLen, float halfWidth, float depth);
    // Deposit a volume of mud as a soft splat.
    void deposit(float x, float z, float volume, float radius);
    // Wear vegetation / add tire track marks.
    // heading (radians, world XZ) records the tire's direction so the renderer can press tread
    // bars across the rut; the default leaves it unchanged.
    void scuff(Vec3 center, float radius, float grassLoss, float trackAdd, float heading = 1e9f);
    // Mud re-flow / re-liquefaction near a point (soupy mud fills ruts back in).
    void relax(Vec3 center, float radius, float dt);
    // Bulldoze/grade a region flat to a target height (the dozer).
    void grade(Vec3 center, float radius, float target, float rate, float dt);

    DirtyRect dirty;  // consumed by renderer each frame

    std::vector<float> h, base, water, moist, comp;
    std::vector<uint8_t> grass, track;
    std::vector<uint8_t> trackDir;  // tire heading mod pi, 0..255
    std::vector<uint8_t> soil;      // Soil enum per cell
};

}  // namespace mb
