#include "audio.h"
#include <algorithm>
#include <cmath>
#include "../physics/terramechanics.h"

namespace mb {

namespace {
constexpr float TWO_PI = 6.28318530718f;
inline float decayCoef(float seconds, float sr) { return std::exp(-1.f / (std::max(seconds, 1e-4f) * sr)); }
}  // namespace

// =========================================================================================
// Engine: per-cylinder exhaust pressure pulses (true firing order & bank layout) feeding one
// lossy waveguide per exhaust bank, then muffler/tone shaping, induction roar, mechanical
// noise, diesel injector clatter, blower whine, turbo whistle/blow-off and backfire pops.
void EngineVoice::init(const EngineSpec* s, float sampleRate, uint32_t seed) {
    spec = s;
    sr = sampleRate;
    nz = Noise(seed * 2654435761u + 7);
    cylAmp.assign(size_t(s->cylinders), 1.f);
    lastX.assign(size_t(s->cylinders), 0.f);
    for (auto& w : wg) {
        w.init(sr);
        w.refl = -0.62f - 0.12f * s->openness;
        w.loss.setCutoff(lerpf(1600.f, 4200.f, s->openness), sr);
    }
    tone.lowpass(lerpf(700.f, 5200.f, s->openness), 0.75f, sr);
    body.peak(s->cylinders >= 8 ? 110.f : (s->cylinders == 6 ? 150.f : 220.f), 1.1f, 5.f, sr);
    intake.bandpass(900.f, 1.4f, sr);
    mech.highpass(3200.f, 0.7f, sr);
    whine.bandpass(2600.f, 3.f, sr);
    bfFilter.lowpass(650.f, 0.9f, sr);
    thr.setCutoff(6.f, sr);
    phase = nz.uni() * 720.f;
}

float EngineVoice::render(const VehicleSound& s, float pitchMul) {
    const EngineSpec& e = *spec;
    float rpm = std::max(0.f, s.rpm) * pitchMul;
    float th = thr.lp(s.throttle);
    float rpmN = clampf(rpm / e.redlineRpm, 0.f, 1.2f);

    // Crank cycle (4-stroke: 720 degrees per two revolutions).
    phase += 720.f * (rpm / 120.f) / sr;
    if (phase >= 720.f) phase -= 720.f;

    const float W = 75.f;  // pulse width in crank degrees
    float in[2] = {0, 0};
    float ampBase = (s.fuelCut ? 0.04f : (0.22f + 0.78f * s.load)) * clampf(rpm / 300.f, 0.f, 1.f);
    if (!s.running) ampBase = 0;
    static thread_local float clickEnv = 0;
    for (int c = 0; c < e.cylinders; c++) {
        float x = phase - e.firingDeg[size_t(c)];
        if (x < 0) x += 720.f;
        if (x < lastX[size_t(c)]) {
            // New combustion event: cycle-to-cycle variation, lumpy-cam lope at idle.
            float a = 1.f + 0.13f * nz.white();
            if (e.openness > 0.6f && rpm < 1150.f && nz.uni() < 0.22f) a *= 0.5f;
            if (s.fuelCut && nz.uni() < 0.12f) a = 3.f;  // unburnt fuel igniting in the header
            cylAmp[size_t(c)] = a;
            if (e.diesel) clickEnv = 1.f;
        }
        lastX[size_t(c)] = x;
        if (x < W) in[e.bank[size_t(c)]] += (0.5f - 0.5f * std::cos(TWO_PI * x / W)) * cylAmp[size_t(c)];
    }
    const float cGas = 480.f;  // speed of sound in hot exhaust gas (m/s)
    float y = wg[0].process(in[0] * ampBase, 2.f * e.pipeLen[0] / cGas * sr);
    if (e.bank.size() && *std::max_element(e.bank.begin(), e.bank.end()) > 0)
        y += 0.85f * wg[1].process(in[1] * ampBase, 2.f * e.pipeLen[1] / cGas * sr);
    y = body.process(tone.process(y));
    y *= 0.45f + 0.55f * th;

    // Induction roar and mechanical noise
    float ind = intake.process(nz.white()) * th * rpmN * 0.22f;
    float mec = mech.process(nz.white()) * rpmN * 0.018f;
    float clat = 0;
    if (e.diesel) {
        clat = mech.process(nz.white()) * clickEnv * 0.5f;
        clickEnv *= decayCoef(0.0015f, sr);
    }
    // Roots blower whine (~3.6x crank) and turbo whistle / blow-off.
    float sup = 0, tur = 0;
    if (e.supercharged) {
        scPhase += (rpm / 60.f) * 3.6f / sr;
        scPhase -= std::floor(scPhase);
        sup = (std::sin(TWO_PI * scPhase) + 0.35f * std::sin(2 * TWO_PI * scPhase)) * 0.045f * (0.3f + 0.7f * th) * rpmN;
    }
    if (e.turbo) {
        turbPhase += (1800.f + 7000.f * s.boost) / sr;
        turbPhase -= std::floor(turbPhase);
        tur = std::sin(TWO_PI * turbPhase) * 0.035f * s.boost * s.boost;
        if (prevThrottle - s.throttle > 0.35f && s.boost > 0.4f) blowoff = 1.f;
        tur += whine.process(nz.white()) * blowoff * 0.35f;
        blowoff *= decayCoef(0.3f, sr);
    }
    prevThrottle = s.throttle;
    // Backfire: low thump + crackle.
    float bf = 0;
    if (backfireT < 0.3f) {
        float env = std::exp(-backfireT * 22.f);
        bf = bfFilter.process(nz.white()) * env * 2.2f;
        if (nz.uni() < 0.006f * (1.f - backfireT * 3.f)) bf += nz.white() * 0.8f;
        backfireT += 1.f / sr;
    }
    float drive = 1.4f + 2.6f * e.openness;
    float out = softclip((y + bf) * drive) / drive * 1.6f + ind + mec + clat + sup + tur;
    return dc.process(out) * 0.42f;
}

// =========================================================================================
// Tires / mud / water: granular lug slaps, squelch, Minnaert bubbles, tread hum, patter.
void RoadVoice::init(float sampleRate, uint32_t seed) {
    sr = sampleRate;
    nz = Noise(seed * 7919u + 3);
    grainLp.lowpass(400, 1.2f, sr);
    squelch.bandpass(260, 1.8f, sr);
    wash.highpass(1200, 0.7f, sr);
    roll.lowpass(180, 0.7f, sr);
    squealBp.bandpass(1050, 7.f, sr);
    scrapeBp.bandpass(520, 1.0f, sr);
    patterHp.setCutoff(2500, sr);
}

float RoadVoice::render(const VehicleSound& s) {
    float out = 0;
    // Lug slap grains: each tread lug striking slop is a short low-passed noise burst
    // with a low "thwop" body; rate = wheel angular speed * lugs / 2pi.
    float rate = std::min(s.lugRate, 95.f);
    lugPhase += rate / sr;
    if (lugPhase >= 1.f) {
        lugPhase -= 1.f;
        if (s.mudSlap > 0.02f) {
            grainEnv = s.mudSlap * (0.7f + 0.6f * nz.uni());
            grainCut = 240.f + 520.f * nz.uni();
            grainLp.lowpass(grainCut, 1.3f, sr);
            thumpEnv = grainEnv * 0.9f;
            thumpF = 55.f + 55.f * nz.uni();
            thumpPh = 0;
        }
    }
    out += grainLp.process(nz.white()) * grainEnv * 1.3f;
    grainEnv *= decayCoef(0.016f, sr);
    thumpPh += thumpF / sr;
    out += std::sin(TWO_PI * thumpPh) * thumpEnv * 0.35f;
    thumpEnv *= decayCoef(0.03f, sr);

    // Continuous squelch with a wandering modulator.
    squelchLfo = clampf(squelchLfo + nz.white() * 0.002f, -1.f, 1.f);
    out += squelch.process(nz.brown()) * s.mudRoll * (0.55f + 0.45f * squelchLfo) * 0.9f;

    // Water: broadband wash + Minnaert bubble resonances (van den Doel 2005 chirp/damping).
    float wv = clampf(s.waterSpeed / 8.f, 0.f, 1.f);
    if (wv > 0.01f) {
        out += wash.process(nz.white()) * wv * 0.3f;
        if (nz.uni() < s.waterSpeed * 0.0012f) {
            for (auto& b : bubbles)
                if (b.amp < 1e-3f) {
                    float rad = 0.0012f + 0.007f * nz.uni() * nz.uni();
                    b.f = minnaertFrequency(rad);
                    b.ph = 0;
                    b.amp = 0.06f * (0.3f + 0.7f * nz.uni()) * wv;
                    b.chirp = 0.1f * b.f;                                  // sigma = xi * f0
                    b.decay = 0.13f * b.f + 0.0072f * std::pow(b.f, 1.5f);  // 1/s
                    break;
                }
        }
    }
    for (auto& b : bubbles) {
        if (b.amp < 1e-3f) continue;
        b.f += b.chirp * b.f / sr * 0.0f + b.chirp / sr * 10.f;
        b.ph += b.f / sr;
        out += std::sin(TWO_PI * b.ph) * b.amp;
        b.amp *= std::exp(-b.decay / sr);
    }

    // Firm ground: rolling rumble + mud-terrain tread hum.
    float fs = clampf(s.firmSpeed / 22.f, 0.f, 1.f);
    out += roll.process(nz.brown()) * fs * 0.35f;
    humPh += s.tireLugHz / sr;
    humPh -= std::floor(humPh);
    out += (std::sin(TWO_PI * humPh) + 0.3f * std::sin(2 * TWO_PI * humPh)) * 0.035f * fs * fs;
    out += squealBp.process(nz.white()) * s.squeal * 0.5f;
    out += scrapeBp.process(nz.white()) * s.scrape * 0.6f;

    // Debris patter: mud chunks drumming on fenders (Poisson clicks).
    if (nz.uni() < s.debris / sr) patterEnv = 0.25f + 0.5f * nz.uni();
    out += patterHp.hp(nz.white()) * patterEnv;
    patterEnv *= decayCoef(0.002f, sr);
    return out;
}

// =========================================================================================
void AudioEngine::init(float sampleRate) {
    sr = sampleRate;
    crowdBp.bandpass(650, 0.8f, sr);
    crowdBp2.bandpass(1800, 1.2f, sr);
    windLp.lowpass(400, 0.7f, sr);
    echoL.init(int(sr * 0.3f));
    echoR.init(int(sr * 0.3f));
    echoLpL.setCutoff(2200, sr);
    echoLpR.setCutoff(2200, sr);
    // Crawler dozer idling by the pit: 4-cyl diesel, firing order 1-3-4-2.
    dozerSpec = EngineSpec{"dozer", "4-cyl diesel", 750, 2400, 1500, 1.2f, {0, 2400}, {250, 250}, 4,
                           {0, 540, 180, 360}, {0, 0, 0, 0}, {1.3f, 1.3f}, 0.35f, true, false, false, 0};
    dozerEngine.init(&dozerSpec, sr, 99);
}

void AudioEngine::setListener(Vec3 pos, Vec3 fwd, Vec3 vel) {
    lPos = pos;
    lFwd = normalize(Vec3{fwd.x, 0, fwd.z});
    lVel = vel;
}

void AudioEngine::setVehicle(int id, const VehicleSound& s) {
    auto it = voices.find(id);
    if (it == voices.end() || it->second->engine.spec != s.engine) {
        auto v = std::make_unique<Voice>();
        v->engine.init(s.engine, sr, uint32_t(id + 1));
        v->road.init(sr, uint32_t(id + 11));
        voices[id] = std::move(v);
        it = voices.find(id);
    }
    it->second->s = s;
}

void AudioEngine::removeVehicle(int id) { voices.erase(id); }

void AudioEngine::backfire(int id) {
    auto it = voices.find(id);
    if (it != voices.end()) it->second->engine.backfire();
}

void AudioEngine::spatialize(Vec3 pos, float& gL, float& gR, float& dist) const {
    Vec3 d = pos - lPos;
    dist = length(d);
    float g = 1.f / (1.f + std::max(0.f, dist - 6.f) / 12.f);
    Vec3 right = cross(lFwd, Vec3{0, 1, 0});
    float pan = dist > 0.5f ? clampf(dot(d / dist, right), -1.f, 1.f) * 0.85f : 0.f;
    float a = (pan + 1.f) * PI * 0.25f;
    gL = std::cos(a) * g * 1.2f;
    gR = std::sin(a) * g * 1.2f;
}

void AudioEngine::trigger(SfxType t, Vec3 pos, float strength) {
    if (t == SfxType::Cheer) { cheer = std::min(1.f, cheer + strength); return; }
    for (auto& x : sfx) {
        if (x.active) continue;
        x = Sfx{};
        x.active = true;
        x.type = t;
        x.strength = clampf(strength, 0.f, 2.f);
        float dist;
        spatialize(pos, x.gL, x.gR, dist);
        switch (t) {
            case SfxType::Impact: x.dur = 0.6f; x.f = 75.f; x.bp.bandpass(950, 2.f, sr); x.lp.lowpass(300, 0.8f, sr); break;
            case SfxType::Splash: x.dur = 0.5f + 0.6f * x.strength; x.bp.bandpass(1800, 0.7f, sr); x.lp.lowpass(350, 0.8f, sr); break;
            case SfxType::Barrel: x.dur = 1.4f; x.f = 150.f + 40.f * nz.uni(); break;
            case SfxType::Slurp: x.dur = 0.4f; x.bp.bandpass(900, 4.f, sr); break;
            case SfxType::Fence: x.dur = 0.5f; x.bp.bandpass(1500, 1.5f, sr); break;
            case SfxType::Shift: x.dur = 0.08f; x.f = 120.f; break;
            case SfxType::Horn: x.dur = 0.7f; break;
            default: x.dur = 0.3f; break;
        }
        return;
    }
}

float AudioEngine::renderSfx(Sfx& x, float& outR) {
    float t = x.t, s = x.strength, v = 0;
    switch (x.type) {
        case SfxType::Impact: {
            float f = 40.f + 35.f * std::exp(-t * 8.f);
            x.ph += f / sr;
            v = std::sin(TWO_PI * x.ph) * std::exp(-t * 11.f) * 0.9f + x.lp.process(nz.white()) * std::exp(-t * 35.f) * 0.8f;
            if (s > 0.6f) v += x.bp.process(nz.white()) * std::exp(-t * 28.f) * 0.35f * s;
            break;
        }
        case SfxType::Splash: {
            float att = std::min(1.f, t / 0.012f);
            float d = 1.f / (0.18f + 0.3f * s);
            v = att * (x.bp.process(nz.white()) * std::exp(-t * d) * 0.55f + x.lp.process(nz.white()) * std::exp(-t * d * 0.7f) * 0.7f);
            if (nz.uni() < 0.02f * std::exp(-t * 3.f)) x.ph2 = 1.f;  // droplet ticks
            v += nz.white() * x.ph2 * 0.15f;
            x.ph2 *= decayCoef(0.003f, sr);
            break;
        }
        case SfxType::Barrel: {
            // Modal synthesis of a steel drum shell.
            x.ph += x.f / sr; x.ph2 += x.f * 1.59f / sr; x.ph3 += x.f * 2.3f / sr;
            v = std::sin(TWO_PI * x.ph) * std::exp(-t * 3.f) * 0.5f + std::sin(TWO_PI * x.ph2) * std::exp(-t * 4.5f) * 0.3f +
                std::sin(TWO_PI * x.ph3) * std::exp(-t * 6.f) * 0.2f;
            v += nz.white() * std::exp(-t * 60.f) * 0.3f;
            break;
        }
        case SfxType::Slurp: {
            if (int(t * sr) % 64 == 0) x.bp.bandpass(900.f - 1600.f * t, 4.f, sr);
            v = x.bp.process(nz.white()) * std::sin(PI * t / x.dur) * 1.3f;
            break;
        }
        case SfxType::Fence: {
            if (nz.uni() < 30.f / sr) x.ph = 1.f;
            v = x.bp.process(nz.white()) * x.ph * 0.6f * (1.f - t / x.dur);
            x.ph *= decayCoef(0.01f, sr);
            break;
        }
        case SfxType::Shift: {
            x.ph += x.f / sr;
            v = std::sin(TWO_PI * x.ph) * std::exp(-t * 50.f) * 0.5f + nz.white() * std::exp(-t * 300.f) * 0.2f;
            break;
        }
        case SfxType::Horn: {
            x.ph += 415.f / sr; x.ph2 += 494.f / sr;
            float sq = (std::sin(TWO_PI * x.ph) > 0 ? 1.f : -1.f) + (std::sin(TWO_PI * x.ph2) > 0 ? 1.f : -1.f);
            v = sq * 0.08f * std::min(1.f, t / 0.02f) * std::min(1.f, (x.dur - t) / 0.05f);
            break;
        }
        default: break;
    }
    x.t += 1.f / sr;
    if (x.t >= x.dur) x.active = false;
    v *= s * sfxVolume;
    outR = v * x.gR;
    return v * x.gL;
}

void AudioEngine::render(float* out, int frames) {
    // Per-block spatial parameters (interpolated across the block to avoid zipper noise).
    struct Blk { Voice* v; float gL0, gR0, gL1, gR1; };
    std::vector<Blk> blks;
    blks.reserve(voices.size());
    for (auto& kv : voices) {
        Voice& v = *kv.second;
        float gL, gR, dist;
        spatialize(v.s.pos, gL, gR, dist);
        if (v.s.local) { gL = std::max(gL, 0.85f); gR = std::max(gR, 0.85f); }
        float pitch = 1.f;
        if (!v.s.local && dist > 1.f) {
            Vec3 rel = (v.s.pos - lPos) / dist;
            pitch = clampf((343.f + dot(lVel, rel)) / (343.f + dot(v.s.vel, rel)), 0.75f, 1.35f);
        }
        v.pitch = pitch;
        float cut = 16000.f / (1.f + dist / 25.f);
        v.airL.setCutoff(cut, sr);
        v.airR.setCutoff(cut, sr);
        blks.push_back({&v, v.gainL, v.gainR, gL, gR});
        v.gainL = gL;
        v.gainR = gR;
    }
    float dgL, dgR, ddist;
    spatialize(dozer, dgL, dgR, ddist);
    VehicleSound dz;
    dz.engine = &dozerSpec;
    dz.rpm = 760.f;
    dz.load = 0.15f;
    dz.throttle = 0.1f;

    for (int i = 0; i < frames; i++) {
        float t = frames > 1 ? float(i) / float(frames - 1) : 1.f;
        float L = 0, R = 0;
        for (auto& b : blks) {
            Voice& v = *b.v;
            float e = v.engine.render(v.s, v.pitch) * engineVolume;
            float r = v.road.render(v.s) * sfxVolume;
            float m = e + r;
            L += v.airL.lp(m) * lerpf(b.gL0, b.gL1, t);
            R += v.airR.lp(m) * lerpf(b.gR0, b.gR1, t);
        }
        for (auto& x : sfx)
            if (x.active) {
                float r;
                L += renderSfx(x, r);
                R += r;
            }
        // Ambience: dozer idle, crowd murmur + cheers, wind.
        float dzs = dozerEngine.render(dz, 1.f) * 0.5f * ambienceVolume;
        L += dzs * dgL;
        R += dzs * dgR;
        crowdLfo = clampf(crowdLfo + nz.white() * 0.0004f, -1.f, 1.f);
        float pk = nz.pink();
        float cr = (crowdBp.process(pk) * 0.8f + crowdBp2.process(pk) * 0.4f) * (crowd * (0.75f + 0.25f * crowdLfo) * 0.35f + cheer * 0.9f);
        if (cheer > 0.3f && nz.uni() < 1.5f / sr) { wooEnv = 1.f; wooF = 1500.f + 1500.f * nz.uni(); wooPh = 0; }
        wooPh += wooF / sr;
        wooF *= 1.f + 0.4f / sr;
        cr += std::sin(TWO_PI * wooPh) * wooEnv * 0.05f;
        wooEnv *= decayCoef(0.25f, sr);
        cheer *= decayCoef(1.4f, sr);
        cr *= ambienceVolume;
        L += cr * 0.9f;
        R += cr;
        float ws = 0;
        for (auto& b : blks)
            if (b.v->s.local) ws = length(b.v->s.vel);
        float wind = windLp.process(nz.white()) * clampf(ws / 30.f, 0.f, 1.f) * 0.12f * ambienceVolume;
        L += wind;
        R += wind;
        // Slapback off the tree line (~30-40 m away): ~190/230 ms.
        float eL = echoLpL.lp(echoL.read(sr * 0.19f)), eR = echoLpR.lp(echoR.read(sr * 0.23f));
        echoL.write((L + R) * 0.5f + eR * 0.2f);
        echoR.write((L + R) * 0.5f + eL * 0.2f);
        L += eL * 0.16f;
        R += eR * 0.16f;
        // Master + peak limiter
        L *= masterVolume;
        R *= masterVolume;
        float pk2 = std::max(std::fabs(L), std::fabs(R));
        limEnv = std::max(pk2, limEnv * decayCoef(0.15f, sr));
        float g = limEnv > 0.9f ? 0.9f / limEnv : 1.f;
        if (muted) g = 0;
        out[2 * i] = clampf(L * g, -1.f, 1.f);
        out[2 * i + 1] = clampf(R * g, -1.f, 1.f);
    }
}

}  // namespace mb
