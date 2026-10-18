// CPU particle system for roost (mud chunks + spray), water splash, mist, exhaust smoke and
// grass clippings. Mud chunks carry real volume: when they land they are deposited back into
// the heightfield, so roost builds berms behind spinning tires.
#pragma once
#include <vector>
#include "../core/math.h"
#include "../core/noise.h"
#include "../terrain/terrain.h"

namespace mb {

enum class PType : uint8_t { MudChunk, MudSpray, Water, Mist, Smoke, Grass, Rain };

struct Particle {
    Vec3 p, v;
    float life = 0, maxLife = 1, size = 0.1f, volume = 0;
    float r = 0.3f, g = 0.25f, b = 0.2f, a = 1;
    PType type = PType::MudChunk;
};

class Particles {
public:
    static constexpr size_t MAX = 9000;
    void emit(PType t, Vec3 p, Vec3 v, float size, float life, Vec3 color, float volume = 0);
    // Returns how much mud volume hit the given sphere (for dirtying vehicles / splat score).
    void update(float dt, Terrain& terrain);
    void clear() { ps.clear(); }
    const std::vector<Particle>& all() const { return ps; }
    Rng rng{0xBADC0DEu};
    float depositedVolume = 0;

private:
    std::vector<Particle> ps;
};

}  // namespace mb
