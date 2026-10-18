// Procedural audio engine. Everything is synthesized from simulation state - no samples.
// See docs/AUDIO.md for the forensic sound breakdown each voice models.
#pragma once
#include <array>
#include <map>
#include <memory>
#include <vector>
#include "../core/math.h"
#include "../physics/specs.h"
#include "synth.h"

namespace mb {

// Per-vehicle state the game hands the audio engine each frame.
struct VehicleSound {
    const EngineSpec* engine = nullptr;
    Vec3 pos, vel;
    float rpm = 800, throttle = 0, load = 0, boost = 0;
    bool fuelCut = false, running = true;
    float mudSlap = 0;      // 0..1 intensity of lugs slapping slop
    float lugRate = 0;      // lug impacts per second (avg driven wheel)
    float mudRoll = 0;      // 0..1 squelch from rolling through mud
    float waterSpeed = 0;   // m/s through standing water
    float firmSpeed = 0;    // m/s rolling on firm ground
    float squeal = 0;       // 0..1 lateral/long slip on firm ground
    float scrape = 0;       // 0..1 belly drag
    float debris = 0;       // mud chunks per second hitting bodywork
    float tireLugHz = 0;    // tread passing frequency on firm (MT hum)
    bool local = false;
};

enum class SfxType { Impact, Splash, Barrel, Backfire, Slurp, Fence, Cheer, Shift, Horn };

class EngineVoice {
public:
    void init(const EngineSpec* spec, float sr, uint32_t seed);
    float render(const VehicleSound& s, float pitchMul);
    void backfire() { backfireT = 0.f; }
    const EngineSpec* spec = nullptr;

private:
    float sr = 48000;
    float phase = 0;                // crank cycle angle 0..720
    std::vector<float> cylAmp;      // per-cycle randomized pulse amplitude
    std::vector<float> lastX;
    Waveguide wg[2];
    Biquad tone, body, intake, mech, whine;
    OnePole thr;
    DCBlock dc;
    Noise nz{1234};
    float turbPhase = 0, scPhase = 0, blowoff = 0, prevThrottle = 0, backfireT = 10.f;
    Biquad bfFilter;
    float level = 0;
};

class RoadVoice {
public:
    void init(float sr, uint32_t seed);
    float render(const VehicleSound& s);

private:
    float sr = 48000;
    Noise nz{777};
    float lugPhase = 0, grainEnv = 0, grainCut = 400, thumpPh = 0, thumpEnv = 0, thumpF = 90;
    Biquad grainLp, squelch, wash, roll, squealBp, scrapeBp, hum;
    OnePole patterHp;
    float humPh = 0, patterEnv = 0;
    struct Bubble { float f = 0, ph = 0, amp = 0, chirp = 0, decay = 0; };
    std::array<Bubble, 12> bubbles{};
    float squelchLfo = 0;
};

class AudioEngine {
public:
    void init(float sampleRate);
    float sampleRate() const { return sr; }
    void setListener(Vec3 pos, Vec3 fwd, Vec3 vel);
    void setVehicle(int id, const VehicleSound& s);
    void removeVehicle(int id);
    void trigger(SfxType t, Vec3 pos, float strength);
    void backfire(int id);
    void setAmbient(Vec3 dozerPos, float crowdLevel) { dozer = dozerPos; crowd = crowdLevel; }
    void setRain(float r) { rain = r; }
    // Thunder from a strike `dist` metres away: arrives after dist/343 s, closer = sharper crack.
    void thunder(float dist) { thunderDelay = dist / 343.f; thunderDist = dist; thunderT = -1; }
    void render(float* interleavedStereo, int frames);
    float masterVolume = 0.8f, engineVolume = 1.f, sfxVolume = 1.f, ambienceVolume = 0.7f;
    bool muted = false;

private:
    struct Voice {
        VehicleSound s;
        EngineVoice engine;
        RoadVoice road;
        float gainL = 0, gainR = 0, pitch = 1, lpCut = 1;
        OnePole airL, airR;
        bool alive = true;
    };
    struct Sfx {
        SfxType type;
        float t = 0, dur = 0.5f, strength = 1, gL = 1, gR = 1, f = 100, ph = 0, ph2 = 0, ph3 = 0;
        Biquad bp, lp;
        bool active = false;
    };
    void spatialize(Vec3 pos, float& gL, float& gR, float& dist) const;
    float renderSfx(Sfx& x, float& outR);

    float sr = 48000;
    Vec3 lPos, lFwd{0, 0, 1}, lVel;
    std::map<int, std::unique_ptr<Voice>> voices;
    std::array<Sfx, 32> sfx;
    Noise nz{4242};
    // Ambience
    Vec3 dozer;
    float crowd = 0.3f, cheer = 0;
    EngineVoice dozerEngine;
    EngineSpec dozerSpec;
    Biquad crowdBp, crowdBp2, windLp;
    // Weather
    float rain = 0;
    Noise nzR{777};
    Biquad rainHpL, rainHpR, rainBp, patterBp, thunderLp, thunderLp2, crackHp;
    float patterEnv = 0, patterPan = 0.5f;
    float thunderDelay = -1, thunderDist = 1000, thunderT = -1, thunderDur = 6;
    float crowdLfo = 0, wooPh = 0, wooEnv = 0, wooF = 1800;
    DelayLine echoL, echoR;
    OnePole echoLpL, echoLpR;
    float limEnv = 0;
};

}  // namespace mb
