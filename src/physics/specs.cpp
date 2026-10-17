#include "specs.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mb {

float EngineSpec::torqueAt(float rpm) const {
    if (rpm <= rpmPts.front()) return torquePts.front();
    for (size_t i = 1; i < rpmPts.size(); i++)
        if (rpm <= rpmPts[i]) {
            float t = (rpm - rpmPts[i - 1]) / (rpmPts[i] - rpmPts[i - 1]);
            return lerpf(torquePts[i - 1], torquePts[i], t);
        }
    return torquePts.back();
}

float EngineSpec::peakTorque() const {
    float m = 0;
    for (float t : torquePts) m = std::max(m, t);
    return m;
}

static std::vector<EngineSpec> buildEngines() {
    std::vector<EngineSpec> e;
    // Cross-plane V8, GM firing order 1-8-4-3-6-5-7-2, odd cylinders on bank 0.
    const std::vector<float> v8Fire{0, 630, 270, 180, 450, 360, 540, 90};  // cylinders 1..8
    const std::vector<int> v8Bank{0, 1, 0, 1, 0, 1, 0, 1};
    // Inline six 1-5-3-6-2-4, 120 deg even intervals, single manifold.
    const std::vector<float> i6Fire{0, 480, 240, 600, 120, 360};
    const std::vector<int> i6Bank{0, 0, 0, 0, 0, 0};

    e.push_back({"i6_40", "4.0L Inline-6 (Jeep 'High Output')", 700, 5300, 2000, 0.16f,
                 {0, 1000, 2000, 3000, 3500, 4500, 5300}, {200, 270, 300, 312, 305, 270, 210},
                 6, i6Fire, i6Bank, {1.6f, 1.6f}, 0.45f, false, false, false, 230});
    e.push_back({"v8_350", "350 Small-Block V8 (open headers)", 750, 5800, 2300, 0.21f,
                 {0, 1000, 2000, 3000, 3600, 4500, 5800}, {290, 360, 410, 440, 445, 410, 320},
                 8, v8Fire, v8Bank, {0.9f, 1.05f}, 0.85f, false, false, false, 260});
    e.push_back({"v8_454", "454 Big-Block V8", 700, 5400, 2200, 0.30f,
                 {0, 1000, 2000, 3000, 3400, 4400, 5400}, {430, 560, 650, 690, 680, 610, 480},
                 8, v8Fire, v8Bank, {1.1f, 1.2f}, 0.8f, false, false, false, 320});
    e.push_back({"ls53_sc", "5.3L LS V8, Roots Supercharged", 800, 6500, 2600, 0.18f,
                 {0, 1000, 2000, 3000, 4000, 5200, 6500}, {300, 420, 560, 640, 660, 630, 520},
                 8, {0, 630, 270, 180, 450, 360, 540, 90}, v8Bank, {0.8f, 0.85f}, 0.9f, false, false, true, 250});
    e.push_back({"duramax", "6.6L Turbo-Diesel V8", 680, 3300, 1800, 0.45f,
                 {0, 800, 1400, 1800, 2400, 3000, 3300}, {420, 620, 820, 880, 840, 690, 560},
                 8, {0, 630, 270, 180, 450, 360, 540, 90}, v8Bank, {2.4f, 2.4f}, 0.35f, true, true, false, 380});
    e.push_back({"twin_1000t", "1.0L Turbo Parallel-Twin (UTV)", 1300, 8500, 3800, 0.05f,
                 {0, 2000, 4000, 6000, 7000, 8000, 8500}, {60, 95, 140, 160, 158, 145, 130},
                 2, {0, 270}, {0, 0}, {0.7f, 0.7f}, 0.6f, false, true, false, 70});
    return e;
}

static std::vector<BodySpec> buildBodies() {
    std::vector<BodySpec> b;
    // '86-'92 Jeep Comanche MJ (photo 1/2): 113" wb short bed, 58" track.
    b.push_back({"comanche", "'88 Comanche MJ", "vehicles/comanche.mbcad", 1180, 2.87f, 1.47f, -0.42f, 0.05f,
                 {1.8f, 1.2f, 4.5f}, 1.05f, 0.35f, 1.25f, -0.18f, 0.24f, 35, "i6_40", true, 0.55f,
                 {0.48f, 0.5f, 0.48f}});
    // 1976 full-size sedan bog car (photo 3, "1976" on the window).
    b.push_back({"bogcar76", "'76 Full-Size Bog Car", "vehicles/bogcar76.mbcad", 1650, 3.09f, 1.62f, -0.36f, 0.0f,
                 {2.0f, 1.0f, 5.6f}, 1.1f, 0.3f, 1.55f, -0.18f, 0.22f, 33, "v8_454", false, 0.56f,
                 {0.88f, 0.88f, 0.86f}});
    // GMT800 crew-cab mud truck with light bar (photo 3, hood up = overheating).
    b.push_back({"silverado", "'05 Crew-Cab Mud Truck", "vehicles/silverado.mbcad", 2250, 3.9f, 1.73f, -0.46f, 0.1f,
                 {2.0f, 1.4f, 5.9f}, 1.35f, 0.4f, 1.9f, -0.2f, 0.28f, 38, "duramax", true, 0.6f,
                 {0.62f, 0.55f, 0.42f}});
    // Sport side-by-side (photo 3, RZR-style UTV).
    b.push_back({"utv", "Sport UTV", "vehicles/utv.mbcad", 650, 2.29f, 1.63f, -0.3f, 0.0f,
                 {1.6f, 1.2f, 3.4f}, 0.9f, 0.55f, -0.2f, -0.1f, 0.38f, 32, "twin_1000t", true, 0.45f,
                 {0.92f, 0.92f, 0.9f}});
    return b;
}

const std::vector<EngineSpec>& engineCatalog() {
    static std::vector<EngineSpec> e = buildEngines();
    return e;
}
const std::vector<BodySpec>& bodyCatalog() {
    static std::vector<BodySpec> b = buildBodies();
    return b;
}
const EngineSpec& findEngine(const std::string& id) {
    for (auto& e : engineCatalog())
        if (e.id == id) return e;
    return engineCatalog().front();
}
const BodySpec& findBody(const std::string& id) {
    for (auto& b : bodyCatalog())
        if (b.id == id) return b;
    return bodyCatalog().front();
}
const char* treadName(Tread t) {
    switch (t) {
        case Tread::AllTerrain: return "at";
        case Tread::MudTerrain: return "mt";
        case Tread::Paddle: return "paddle";
    }
    return "mt";
}

std::string VehicleConfig::serialize() const {
    char buf[512];
    unsigned r = unsigned(clampf(paint[0], 0, 1) * 255), g = unsigned(clampf(paint[1], 0, 1) * 255),
             bl = unsigned(clampf(paint[2], 0, 1) * 255);
    std::snprintf(buf, sizeof buf,
                  "name=%s;body=%s;engine=%s;tire=%.1f;width=%.1f;tread=%s;lift=%.1f;axle=%.2f;lockF=%d;lockR=%d;"
                  "awd=%d;snorkel=%d;psi=%.1f;paint=%02x%02x%02x",
                  name.c_str(), body.c_str(), engine.c_str(), tireIn, widthIn, treadName(tread), liftIn, axleRatio,
                  lockFront, lockRear, fourWD, snorkel, psi, r, g, bl);
    return buf;
}

VehicleConfig VehicleConfig::parse(const std::string& s) {
    VehicleConfig c;
    std::stringstream ss(s);
    std::string kv;
    while (std::getline(ss, kv, ';')) {
        auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        float f = float(std::atof(v.c_str()));
        if (k == "name") c.name = v.substr(0, 20);
        else if (k == "body") c.body = findBody(v).id;
        else if (k == "engine") c.engine = findEngine(v).id;
        else if (k == "tire") c.tireIn = clampf(f, 29, 44);
        else if (k == "width") c.widthIn = clampf(f, 9.5f, 18.5f);
        else if (k == "tread") c.tread = v == "at" ? Tread::AllTerrain : (v == "paddle" ? Tread::Paddle : Tread::MudTerrain);
        else if (k == "lift") c.liftIn = clampf(f, 0, 10);
        else if (k == "axle") c.axleRatio = clampf(f, 3.55f, 6.5f);
        else if (k == "lockF") c.lockFront = f != 0;
        else if (k == "lockR") c.lockRear = f != 0;
        else if (k == "awd") c.fourWD = f != 0;
        else if (k == "snorkel") c.snorkel = f != 0;
        else if (k == "psi") c.psi = clampf(f, 5, 35);
        else if (k == "paint" && v.size() >= 6) {
            unsigned col = unsigned(std::strtoul(v.c_str(), nullptr, 16));
            c.paint[0] = ((col >> 16) & 255) / 255.f;
            c.paint[1] = ((col >> 8) & 255) / 255.f;
            c.paint[2] = (col & 255) / 255.f;
        }
    }
    return c;
}

}  // namespace mb
