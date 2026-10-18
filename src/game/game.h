// Game orchestration: fixed-step simulation, camera, effects, audio feed, net replicas, HUD.
#pragma once
#include <deque>
#include <map>
#include <string>
#include "../audio/audio.h"
#include "../input/input.h"
#include "../net/net.h"
#include "../render/renderer.h"
#include "../physics/collide.h"
#include "ai.h"
#include "modes.h"
#include "particles.h"
#include "challenges.h"
#include "recovery.h"
#include "world.h"

namespace mb {

enum class AppState : int { Attract = 0, Garage = 1, Playing = 2, Paused = 3 };

struct VehicleVisual {
    const GpuMesh* body = nullptr;
    const GpuMesh* wheel = nullptr;
    void build(Renderer& r, const VehicleConfig& c);
};

struct Remote {
    RemoteVehicle net;
    Vehicle geom;          // configured only for geometry (wheel mounts, dims)
    VehicleVisual vis;
    VehicleConfig cfg;
    NetSnapshot cur;
    bool valid = false;
    float spin[4] = {0, 0, 0, 0};
};

class Game {
public:
    void init(int w, int h);
    void frame(double nowMs);
    void setConfig(const std::string& cfg);
    void setState(AppState s);
    void startMode(ModeId m, int lane, double startAt);
    void action(const std::string& a);
    const char* hud();
    double serverNow() const;

    // Networking
    int netSnapshot(uint8_t* out);
    void netReceive(const uint8_t* data, int len);
    void addRemote(int id, const std::string& cfg, const std::string& name);
    void removeRemote(int id);

    // Solo rivals + ghost replay
    void setAiCount(int n) { aiCount = std::max(0, std::min(3, n)); }
    void ghostLoad(const float* data, int count) { ghostTrace.assign(data, data + count); }
    const std::vector<float>& lastRunTrace() const { return runTrace; }

    World world;
    Renderer renderer;
    AudioEngine audio;
    Input input;
    Particles particles;
    ModeRunner mode;
    Vehicle player;
    VehicleVisual playerVis;
    AppState state = AppState::Attract;
    double clockOffset = 0;
    Recovery recovery;
    Challenges challenges;
    std::string coach;            // field-guide coaching line for the HUD
    bool recoverMenu = false;     // tow key asks the UI for the recovery menu
    int localId = 0;
    bool steeringAssist = true;

private:
    void simulate(float dt, const InputState& in);
    float spawnWheelFx(const Vehicle& v, float dt);
    VehicleSound soundFor(const Vehicle& v, bool local, float ejectedVol, float dt);
    std::string standingsJson();
    void updateCamera(float dt, const InputState& in);
    void feedAudio(float dt);
    void buildFrame(RenderFrame& f);
    bool garageSpin = true;
    bool photoCam = false;
    bool freeCam = false;   // asset/photo shots from a fixed point; the player's truck is hidden
    Vec3 freePos, freeTgt;
    float freeFov = 0.87f;
    bool freeShowPlayer = false;
    float photoYaw = 150, photoDist = 7, photoH = 2.5f;  // turntable in the garage; photo-mode camyaw: stops it
    void addVehicleDraws(RenderFrame& f, const Vehicle& geom, const VehicleVisual& vis, Vec3 pos, Quat rot,
                         const float* comp, const float* spin, float steer, float dirt, float alpha);

    double lastMs = 0, accum = 0;
    float time = 0;
    uint32_t netSeq = 0;
    double lastSnap = 0;
    std::map<int, Remote> remotes;
    // Camera
    Vec3 camPos{-60, 8, -20}, camTarget{0, 0, -30};
    float camYaw = 0, camPitch = 0, camShake = 0, orbitT = 0;
    int camMode = 0;  // 0 chase, 1 far chase, 2 hood
    float fov = 1.08f;
    // Per-frame aggregates
    float ejected = 0, splash = 0;
    int barrelsKnocked = 0;
    WorldEvents wev;
    float slurpTimer = 0, hornTimer = 0;
    std::string hudJson, resultsJson;
    int lockerState = 1;
    std::vector<AiDriver> ais;
    std::vector<VehicleVisual> aiVis;
    int aiCount = 3;
    bool aiReleased = false;
    std::vector<float> ghostTrace, runTrace;   // 8 floats per sample: t, pos.xyz, rot.xyzw
    float traceTimer = 0;
    struct LensSplat { float x, y, r, a; };
    std::vector<LensSplat> lens;
    void updateLens(float dt);
    float relaxTimer = 0;
    float diggingTimer = 0, tractionBackTimer = 0;
    // PA announcer: lines queued by events, shown one at a time on the HUD.
    std::deque<std::string> announceQ;
    std::string announce;
    float announceT = 0, washT = 0;
    std::vector<int> aiState;   // last announced state per rival (0 running, 1 finished, 2 gave up)
    void announceLine(const std::string& s) { if (announceQ.size() < 8) announceQ.push_back(s); }
    void updateVenue(float dt);
    // Weather and time of day: 0 dry/1 overcast/2 rain; 0 afternoon/1 midday/2 golden hour.
    int weatherMode = 0, timeOfDay = 0;
    float cloudCover = 0, rainAmt = 0, lightningTimer = 25, flashT = -1;
    Rng skyRng{0x5eedULL};   // lightning schedule; seeded per room so every player sees the same strikes
    void updateWeather(float dt);
    void say(const std::string& m, float t = 2.5f) { if (!m.empty()) { mode.message = m; mode.messageTimer = t; } }
    void updateCoach(float dt);
    bool competitionRun() const;
};

}  // namespace mb
