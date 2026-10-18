#include "particles.h"

namespace mb {

void Particles::emit(PType t, Vec3 p, Vec3 v, float size, float life, Vec3 c, float volume) {
    if (ps.size() >= MAX) {
        // Recycle the oldest-looking slot (cheap: overwrite a random one).
        ps[rng.next() % ps.size()] = Particle{p, v, 0, life, size, volume, c.x, c.y, c.z, 1, t};
        return;
    }
    ps.push_back(Particle{p, v, 0, life, size, volume, c.x, c.y, c.z, 1, t});
}

void Particles::update(float dt, Terrain& terrain) {
    depositedVolume = 0;
    size_t w = 0;
    for (size_t i = 0; i < ps.size(); i++) {
        Particle& q = ps[i];
        q.life += dt;
        if (q.life >= q.maxLife) continue;
        float drag = 0.f, grav = 9.81f;
        switch (q.type) {
            case PType::MudChunk: drag = 0.15f; break;
            case PType::MudSpray: drag = 0.9f; break;
            case PType::Water: drag = 0.6f; break;
            case PType::Mist: drag = 2.5f; grav = 0.6f; q.size += dt * 0.9f; break;
            case PType::Smoke: drag = 1.6f; grav = -0.7f; q.size += dt * 1.4f; break;
            case PType::Grass: drag = 2.0f; grav = 3.f; break;
            case PType::Rain: drag = 0.f; grav = 0.f; break;   // already at terminal velocity
        }
        q.v.y -= grav * dt;
        q.v *= std::exp(-drag * dt);
        q.p += q.v * dt;
        float t = q.life / q.maxLife;
        if (q.type == PType::Mist || q.type == PType::Smoke) q.a = (1.f - t) * 0.5f;
        else q.a = t > 0.85f ? (1.f - t) / 0.15f : 1.f;
        if (q.type != PType::Mist && q.type != PType::Smoke && q.v.y < 0) {
            float g = terrain.heightAt(q.p.x, q.p.z);
            if (q.p.y <= g) {
                if (q.type == PType::MudChunk && q.volume > 0) {
                    terrain.deposit(q.p.x, q.p.z, q.volume, 0.35f + q.size);
                    depositedVolume += q.volume;
                    continue;
                }
                if (q.type == PType::MudChunk || q.type == PType::MudSpray) {
                    // Splat: stick briefly on the ground then fade.
                    q.p.y = g + 0.01f;
                    q.v = {};
                    q.life = std::max(q.life, q.maxLife * 0.8f);
                } else {
                    continue;  // water/grass disappear on contact
                }
            }
        }
        ps[w++] = q;
    }
    ps.resize(w);
}

}  // namespace mb
