// Game orchestration: fixed-step simulation, camera, effects, audio feed, net replicas, HUD.
#pragma once
#include <map>
#include <string>
#include "../audio/audio.h"
#include "../input/input.h"
#include "../net/net.h"
#include "../render/renderer.h"
#include "modes.h"
#include "particles.h"
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
    int localId = 0;
    bool steeringAssist = true;

private:
    void simulate(float dt, const InputState& in);
    void spawnWheelFx(const Vehicle& v, float dt);
    void updateCamera(float dt, const InputState& in);
    void feedAudio(float dt);
    void buildFrame(RenderFrame& f);
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
    float relaxTimer = 0;
};

}  // namespace mb
