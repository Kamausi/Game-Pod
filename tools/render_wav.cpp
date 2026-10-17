// render_wav - offline render of the procedural audio for listening / A-B against field
// recordings. Writes a 16-bit stereo WAV:
//   render_wav <engine_id> out.wav [seconds]
// Scenario: idle -> rev on the line (neutral, limiter bounce) -> launch into the pit with lug
// slap and water -> backfire on lift-off -> splash + barrel hit.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include "audio/audio.h"

using namespace mb;

static void writeWav(const char* path, const std::vector<float>& lr, int sr) {
    FILE* f = std::fopen(path, "wb");
    if (!f) { std::perror(path); return; }
    uint32_t dataBytes = uint32_t(lr.size() * 2), riff = 36 + dataBytes, fmtLen = 16, rate = uint32_t(sr), byteRate = rate * 4;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    std::fwrite("RIFF", 1, 4, f); std::fwrite(&riff, 4, 1, f); std::fwrite("WAVEfmt ", 1, 8, f);
    std::fwrite(&fmtLen, 4, 1, f); std::fwrite(&pcm, 2, 1, f); std::fwrite(&ch, 2, 1, f); std::fwrite(&rate, 4, 1, f);
    std::fwrite(&byteRate, 4, 1, f); std::fwrite(&align, 2, 1, f); std::fwrite(&bits, 2, 1, f);
    std::fwrite("data", 1, 4, f); std::fwrite(&dataBytes, 4, 1, f);
    for (float x : lr) { int16_t s = int16_t(std::lround(clampf(x, -1, 1) * 32767)); std::fwrite(&s, 2, 1, f); }
    std::fclose(f);
}

int main(int argc, char** argv) {
    std::string id = argc > 1 ? argv[1] : "v8_350";
    const char* out = argc > 2 ? argv[2] : "mudbuddies.wav";
    float secs = argc > 3 ? float(std::atof(argv[3])) : 14.f;
    const int sr = 48000, block = 512;
    AudioEngine a;
    a.init(float(sr));
    a.setListener({0, 2, -6}, {0, 0, 1}, {});
    a.setAmbient({30, 0, 10}, 0.6f);
    const EngineSpec& e = findEngine(id);
    std::vector<float> all, buf(block * 2);
    float rpm = e.idleRpm, t = 0;
    bool backfired = false, splashed = false;
    while (t < secs) {
        VehicleSound s;
        s.engine = &e;
        s.local = true;
        float thr = 0;
        if (t < 2) thr = 0;
        else if (t < 5) thr = std::fmod(t, 1.f) < 0.6f ? 1.f : 0.f;           // blipping on the line
        else if (t < 10) thr = 1.f;                                            // launch
        else thr = 0;
        float target = thr > 0 ? (t < 5 ? e.redlineRpm * 1.02f : lerpf(e.stallRpm, e.redlineRpm * 1.02f, std::fmod(t - 5, 2.5f) / 2.5f)) : e.idleRpm;
        rpm += (target - rpm) * (thr > 0 ? 6.f : 3.f) * block / sr;
        s.rpm = rpm; s.throttle = thr; s.load = thr; s.fuelCut = rpm > e.redlineRpm; s.boost = thr * 0.8f;
        if (t > 5 && t < 10) { s.mudSlap = 0.9f; s.lugRate = 18 * 25 / 6.283f; s.mudRoll = 0.6f; s.waterSpeed = t > 7.5f ? 6.f : 0.f; s.debris = 40; }
        if (t > 10 && !backfired) { a.backfire(1); backfired = true; }
        if (t > 7.5f && !splashed) { a.trigger(SfxType::Splash, {0, 0, 2}, 1.2f); a.trigger(SfxType::Barrel, {3, 0, 4}, 0.9f); splashed = true; }
        a.setVehicle(1, s);
        a.render(buf.data(), block);
        all.insert(all.end(), buf.begin(), buf.end());
        t += float(block) / sr;
    }
    writeWav(out, all, sr);
    std::printf("wrote %s (%s, %.1f s)\n", out, e.name.c_str(), secs);
    return 0;
}
