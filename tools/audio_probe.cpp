// audio_probe - forensic analysis of field recordings (phone video audio exported to WAV).
// Use it to calibrate the procedural synth against real mud-bog footage:
//   audio_probe clip.wav [cylinders=8] [csv_out]
// Per 50 ms frame it reports:
//   * fundamental firing frequency (harmonic product spectrum, 20-600 Hz) -> RPM estimate
//     using f_fire = RPM/60 * cylinders/2 (4-stroke)
//   * spectral centroid (brightness: open headers vs muffled, water hiss)
//   * spectral flux onsets -> transient rate (lug slap, debris patter, limiter bounce)
//   * band energies: sub <80 Hz (thumps), low 80-400 (exhaust body), mid 400-2k, high >2k (splash/hiss)
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using cf = std::complex<float>;

static void fft(std::vector<cf>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = -2.f * 3.14159265f / float(len);
        cf wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cf w(1);
            for (size_t j = 0; j < len / 2; j++) {
                cf u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

static bool readWav(const char* path, std::vector<float>& mono, int& sr) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char id[4];
    uint32_t sz;
    if (std::fread(id, 1, 4, f) != 4 || std::memcmp(id, "RIFF", 4)) { std::fclose(f); return false; }
    (void)!std::fread(&sz, 4, 1, f);
    (void)!std::fread(id, 1, 4, f);
    uint16_t fmt = 0, ch = 1, bits = 16;
    uint32_t rate = 44100;
    while (std::fread(id, 1, 4, f) == 4 && std::fread(&sz, 4, 1, f) == 1) {
        if (!std::memcmp(id, "fmt ", 4)) {
            (void)!std::fread(&fmt, 2, 1, f); (void)!std::fread(&ch, 2, 1, f); (void)!std::fread(&rate, 4, 1, f);
            uint32_t br; uint16_t al; (void)!std::fread(&br, 4, 1, f); (void)!std::fread(&al, 2, 1, f); (void)!std::fread(&bits, 2, 1, f);
            std::fseek(f, long(sz) - 16, SEEK_CUR);
        } else if (!std::memcmp(id, "data", 4)) {
            if (fmt != 1 || bits != 16) { std::fclose(f); std::fprintf(stderr, "need 16-bit PCM\n"); return false; }
            std::vector<int16_t> d(sz / 2);
            (void)!std::fread(d.data(), 2, d.size(), f);
            for (size_t i = 0; i + ch - 1 < d.size(); i += ch) {
                float s = 0;
                for (int c = 0; c < ch; c++) s += d[i + size_t(c)];
                mono.push_back(s / (32768.f * ch));
            }
            break;
        } else std::fseek(f, long(sz), SEEK_CUR);
    }
    std::fclose(f);
    sr = int(rate);
    return !mono.empty();
}

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("usage: audio_probe clip.wav [cylinders=8] [csv_out]"); return 2; }
    int cyl = argc > 2 ? std::atoi(argv[2]) : 8;
    std::vector<float> x;
    int sr = 0;
    if (!readWav(argv[1], x, sr)) { std::fprintf(stderr, "could not read %s (16-bit PCM WAV)\n", argv[1]); return 1; }
    const size_t N = 8192, hop = size_t(sr * 0.05);
    FILE* csv = argc > 3 ? std::fopen(argv[3], "w") : nullptr;
    if (csv) std::fprintf(csv, "t,f_fire,rpm,centroid,flux,sub,low,mid,high\n");
    std::vector<float> prevMag(N / 2, 0.f), fluxes;
    double sumRpm = 0, sumCent = 0;
    int nRpm = 0, frames = 0, onsets = 0;
    float fluxMean = 0;
    for (size_t pos = 0; pos + N < x.size(); pos += hop) {
        std::vector<cf> a(N);
        for (size_t i = 0; i < N; i++) a[i] = x[pos + i] * (0.5f - 0.5f * std::cos(2 * 3.14159265f * i / (N - 1)));
        fft(a);
        std::vector<float> mag(N / 2);
        for (size_t i = 0; i < N / 2; i++) mag[i] = std::abs(a[i]);
        auto bin = [&](float hz) { return size_t(hz * N / sr); };
        // Harmonic product spectrum over the firing band.
        float bestF = 0, best = 0;
        for (size_t b = bin(20); b < bin(600); b++) {
            float p = std::log(1e-6f + mag[b]);
            for (int h = 2; h <= 4; h++) if (b * h < N / 2) p += std::log(1e-6f + mag[b * size_t(h)]);
            if (p > best || bestF == 0) { best = p; bestF = float(b) * sr / N; }
        }
        float num = 0, den = 0, flux = 0, e[4] = {0, 0, 0, 0};
        for (size_t i = 1; i < N / 2; i++) {
            float hz = float(i) * sr / N, m2 = mag[i] * mag[i];
            num += hz * mag[i]; den += mag[i];
            flux += std::max(0.f, mag[i] - prevMag[i]);
            e[hz < 80 ? 0 : hz < 400 ? 1 : hz < 2000 ? 2 : 3] += m2;
        }
        prevMag = mag;
        float cent = den > 0 ? num / den : 0;
        float rpm = bestF * 120.f / cyl;
        fluxMean = fluxMean * 0.95f + flux * 0.05f;
        if (flux > fluxMean * 1.8f && frames > 5) onsets++;
        float tot = e[0] + e[1] + e[2] + e[3] + 1e-9f;
        if (csv) std::fprintf(csv, "%.2f,%.1f,%.0f,%.0f,%.2f,%.3f,%.3f,%.3f,%.3f\n", pos / float(sr), bestF, rpm, cent, flux,
                              e[0] / tot, e[1] / tot, e[2] / tot, e[3] / tot);
        if (rpm > 400 && rpm < 9000) { sumRpm += rpm; nRpm++; }
        sumCent += cent;
        frames++;
    }
    if (csv) std::fclose(csv);
    float dur = x.size() / float(sr);
    std::printf("%s: %.1f s @ %d Hz\n", argv[1], dur, sr);
    std::printf("  mean firing-derived RPM (%d cyl): %.0f\n", cyl, nRpm ? sumRpm / nRpm : 0.0);
    std::printf("  mean spectral centroid: %.0f Hz\n", frames ? sumCent / frames : 0.0);
    std::printf("  transient onsets: %d (%.1f / s)  <- lug slap / debris / limiter\n", onsets, onsets / std::max(dur, 0.1f));
    return 0;
}
