// Mud Buddies - WASM entry point and the C API used by web/app.js.
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include <string>
#include <vector>
#include "devtools/devui.h"
#include "game/game.h"

using namespace mb;

static Game* g_game = nullptr;
static DevUI g_dev;
static std::vector<float> g_audioBuf;
static std::string g_catalog;

static void loop() {
    if (!g_game) return;
    g_game->frame(emscripten_get_now());
    g_dev.frame(*g_game);
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void mb_resize(int w, int h) {
    if (!g_game) return;
    emscripten_set_canvas_element_size("#canvas", w, h);
    g_game->renderer.resize(w, h);
}

EMSCRIPTEN_KEEPALIVE void mb_set_config(const char* cfg) { if (g_game) g_game->setConfig(cfg); }
EMSCRIPTEN_KEEPALIVE void mb_set_state(int s) { if (g_game) g_game->setState(AppState(s)); }
EMSCRIPTEN_KEEPALIVE void mb_start_mode(int mode, int lane, double startAt) { if (g_game) g_game->startMode(ModeId(mode), lane, startAt); }
EMSCRIPTEN_KEEPALIVE void mb_action(const char* a) { if (g_game) g_game->action(a); }
EMSCRIPTEN_KEEPALIVE const char* mb_hud() { return g_game ? g_game->hud() : "{}"; }
EMSCRIPTEN_KEEPALIVE void mb_set_quality(int q) { if (g_game) g_game->renderer.quality = q; }
EMSCRIPTEN_KEEPALIVE void mb_set_assist(int steer) { if (g_game) g_game->steeringAssist = steer != 0; }

EMSCRIPTEN_KEEPALIVE void mb_audio_init(float sr) { if (g_game) g_game->audio.init(sr); }
EMSCRIPTEN_KEEPALIVE float* mb_audio_render(int frames) {
    g_audioBuf.resize(size_t(frames) * 2);
    if (g_game) g_game->audio.render(g_audioBuf.data(), frames);
    return g_audioBuf.data();
}
EMSCRIPTEN_KEEPALIVE void mb_set_volume(float master, float engine, float sfx, float amb) {
    if (!g_game) return;
    g_game->audio.masterVolume = master;
    g_game->audio.engineVolume = engine;
    g_game->audio.sfxVolume = sfx;
    g_game->audio.ambienceVolume = amb;
}

EMSCRIPTEN_KEEPALIVE int mb_net_snapshot(uint8_t* out) { return g_game ? g_game->netSnapshot(out) : 0; }
EMSCRIPTEN_KEEPALIVE void mb_net_receive(const uint8_t* data, int len) { if (g_game) g_game->netReceive(data, len); }
EMSCRIPTEN_KEEPALIVE void mb_remote_add(int id, const char* cfg, const char* name) { if (g_game) g_game->addRemote(id, cfg, name); }
EMSCRIPTEN_KEEPALIVE void mb_remote_remove(int id) { if (g_game) g_game->removeRemote(id); }
EMSCRIPTEN_KEEPALIVE void mb_set_clock_offset(double off) { if (g_game) g_game->clockOffset = off; }
EMSCRIPTEN_KEEPALIVE void mb_set_local_id(int id) { if (g_game) { g_game->audio.removeVehicle(g_game->localId); g_game->localId = id; } }
EMSCRIPTEN_KEEPALIVE double mb_server_now() { return g_game ? g_game->serverNow() : 0; }

EMSCRIPTEN_KEEPALIVE void mb_touch(float thr, float brk, float steer, int hb, float air, float lookX, float lookY) {
    if (g_game) g_game->input.setTouch(thr, brk, steer, hb != 0, air, lookX, lookY);
}
EMSCRIPTEN_KEEPALIVE void mb_press(const char* name) { if (g_game) g_game->input.pressVirtual(name); }
EMSCRIPTEN_KEEPALIVE void mb_set_post(int on) { if (g_game) g_game->renderer.postEnabled = on != 0; }
EMSCRIPTEN_KEEPALIVE void mb_set_ai_count(int n) { if (g_game) g_game->setAiCount(n); }
EMSCRIPTEN_KEEPALIVE void mb_ghost_load(const float* data, int count) { if (g_game) g_game->ghostLoad(data, count); }
EMSCRIPTEN_KEEPALIVE const float* mb_ghost_trace_ptr() { return g_game ? g_game->lastRunTrace().data() : nullptr; }
EMSCRIPTEN_KEEPALIVE int mb_ghost_trace_len() { return g_game ? int(g_game->lastRunTrace().size()) : 0; }

// Catalog for the garage UI (bodies, engines) as JSON.
// Minimap overview: RGBA image of the whole map (north = -z at the top), and the map's size in m.
EMSCRIPTEN_KEEPALIVE const uint8_t* mb_map_image() { return g_game ? g_game->world.overview.data() : nullptr; }
EMSCRIPTEN_KEEPALIVE int mb_map_res() { return g_game ? g_game->world.overviewN : 0; }
EMSCRIPTEN_KEEPALIVE float mb_map_size() { return g_game ? g_game->world.terrain.size() : 0.f; }
EMSCRIPTEN_KEEPALIVE const char* mb_catalog() {
    g_catalog = "{\"bodies\":[";
    char b[512];
    bool first = true;
    for (auto& x : bodyCatalog()) {
        std::snprintf(b, sizeof b, "%s{\"id\":\"%s\",\"name\":\"%s\",\"engine\":\"%s\",\"tire\":%.0f,\"awd\":%s,\"paint\":\"%02x%02x%02x\"}",
                      first ? "" : ",", x.id.c_str(), x.name.c_str(), x.defaultEngine.c_str(), x.defaultTireIn, x.default4wd ? "true" : "false",
                      int(x.paint[0] * 255), int(x.paint[1] * 255), int(x.paint[2] * 255));
        g_catalog += b;
        first = false;
    }
    g_catalog += "],\"engines\":[";
    first = true;
    for (auto& e : engineCatalog()) {
        float hp = 0;
        for (size_t i = 0; i < e.rpmPts.size(); i++) hp = std::max(hp, e.torquePts[i] * e.rpmPts[i] / 7121.f);
        std::snprintf(b, sizeof b, "%s{\"id\":\"%s\",\"name\":\"%s\",\"hp\":%.0f,\"tq\":%.0f,\"redline\":%.0f}", first ? "" : ",",
                      e.id.c_str(), e.name.c_str(), hp, e.peakTorque(), e.redlineRpm);
        g_catalog += b;
        first = false;
    }
    g_catalog += "]}";
    return g_catalog.c_str();
}

}  // extern "C"

int main() {
    double w = 1280, h = 720;
    emscripten_get_element_css_size("#canvas", &w, &h);
    emscripten_set_canvas_element_size("#canvas", int(w), int(h));
    EmscriptenWebGLContextAttributes attr;
    emscripten_webgl_init_context_attributes(&attr);
    attr.majorVersion = 2;
    attr.minorVersion = 0;
    attr.antialias = true;
    attr.depth = true;
    attr.alpha = false;
    attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attr);
    if (ctx <= 0) {
        EM_ASM({ if (Module.onFatal) Module.onFatal("WebGL2 is not available in this browser."); });
        return 1;
    }
    emscripten_webgl_make_context_current(ctx);
    g_game = new Game();
    g_game->init(int(w), int(h));
    g_dev.init();
    EM_ASM({ if (Module.onGameReady) Module.onGameReady(); });
    emscripten_set_main_loop(loop, 0, false);
    return 0;
}
