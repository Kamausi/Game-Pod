#include "devui.h"
#ifdef MB_DEVTOOLS
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>
#include "../game/game.h"
#include "../physics/tuning.h"
#include "imgui.h"
#include "imgui_impl_opengl3.h"

namespace mb {

namespace {
DevUI* g_dev = nullptr;

bool mouseHook(int type, float x, float y, int button) {
    if (!g_dev || !g_dev->visible) return false;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(x, y);
    int b = button == 2 ? 1 : (button == 1 ? 2 : 0);
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN) io.AddMouseButtonEvent(b, true);
    if (type == EMSCRIPTEN_EVENT_MOUSEUP) io.AddMouseButtonEvent(b, false);
    return io.WantCaptureMouse;
}
EM_BOOL onWheel(int, const EmscriptenWheelEvent* e, void*) {
    if (!g_dev || !g_dev->visible) return EM_FALSE;
    ImGuiIO& io = ImGui::GetIO();
    io.AddMouseWheelEvent(0.f, float(-e->deltaY / 100.0));
    return io.WantCaptureMouse ? EM_TRUE : EM_FALSE;
}

struct Ring {
    float v[300] = {0};
    int i = 0;
    void push(float x) { v[i] = x; i = (i + 1) % 300; }
    void plot(const char* label, float lo, float hi) {
        char ov[32];
        std::snprintf(ov, sizeof ov, "%.2f", v[(i + 299) % 300]);
        ImGui::PlotLines(label, v, 300, i, ov, lo, hi, ImVec2(0, 46));
    }
};
Ring rSpeed, rRpm, rSlip, rBelly, rTemp, rFps;

void soilRegion(Game& g, float dMoist, float dWater) {
    World& w = g.world;
    Terrain& t = w.terrain;
    for (int j = 0; j < Terrain::N; j++)
        for (int i = 0; i < Terrain::N; i++) {
            Vec3 p = t.cellToWorld(i, j);
            if (p.x < w.pitX0 || p.x > w.pitX1 || std::fabs(p.z - w.pitZ) > w.pitHalfW) continue;
            int k = t.idx(i, j);
            t.moist[k] = clampf(t.moist[k] + dMoist, 0.f, 1.f);
            if (t.water[k] > Terrain::NO_WATER + 1) t.water[k] += dWater;
            else if (dWater > 0) t.water[k] = t.h[k] - 0.15f + dWater;
            t.dirty.add(i, j);
        }
}
}  // namespace

void DevUI::init() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = 6;
    st.FrameRounding = 4;
    st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.55f, 0.24f, 0.05f, 1);
    st.Colors[ImGuiCol_SliderGrab] = ImVec4(1.f, 0.35f, 0.08f, 1);
    st.Colors[ImGuiCol_Header] = ImVec4(0.35f, 0.25f, 0.16f, 1);
    st.Colors[ImGuiCol_PlotLines] = ImVec4(0.95f, 0.77f, 0.1f, 1);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_dev = this;
    Input::mouseHook = mouseHook;
    emscripten_set_wheel_callback("#canvas", nullptr, EM_TRUE, onWheel);
    ready = true;
    lastMs = emscripten_get_now();
}

void DevUI::frame(Game& g) {
    if (!ready) return;
    if (g.input.devTogglePressed()) visible = !visible;
    double now = emscripten_get_now();
    float dt = float((now - lastMs) / 1000.0);
    lastMs = now;
    Vehicle& v = g.player;
    rSpeed.push(v.speed() * 2.237f);
    rRpm.push(v.engineRpm);
    rSlip.push(v.avgSlip);
    rBelly.push(v.bellyLoad);
    rTemp.push(v.engineTemp);
    rFps.push(dt > 0 ? 1.f / dt : 0.f);
    if (!visible) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(g.renderer.width), float(g.renderer.height));
    io.DeltaTime = dt > 0 ? dt : 1.f / 60.f;
    float scale = std::max(1.f, g.renderer.height / 900.f);
    io.FontGlobalScale = scale;
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(12, 160 * scale), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420 * scale, 640 * scale), ImGuiCond_FirstUseEver);
    ImGui::Begin("Mud Buddies - dev panel (F1)");
    ImGui::Text("%.0f fps | %zu particles | mode %s", rFps.v[(rFps.i + 299) % 300], g.particles.all().size(), modeName(g.mode.id));

    if (ImGui::CollapsingHeader("Soil / terramechanics", ImGuiTreeNodeFlags_DefaultOpen)) {
        const ImGuiSliderFlags lg = ImGuiSliderFlags_Logarithmic;
        ImGui::SliderFloat("Bekker stiffness k_eq", &g_tuning.soilStiffness, 0.1f, 10.f, "x%.2f", lg);
        ImGui::SliderFloat("Sinkage viscosity", &g_tuning.soilViscosity, 0.1f, 10.f, "x%.2f", lg);
        ImGui::SliderFloat("Cohesion c", &g_tuning.cohesion, 0.1f, 10.f, "x%.2f", lg);
        ImGui::SliderFloat("Friction tan(phi)", &g_tuning.friction, 0.1f, 5.f, "x%.2f", lg);
        ImGui::SliderFloat("Dig rate", &g_tuning.digRate, 0.f, 5.f, "x%.2f");
        ImGui::SliderFloat("Churn drag", &g_tuning.churn, 0.f, 5.f, "x%.2f");
        ImGui::SliderFloat("Suction", &g_tuning.suction, 0.f, 5.f, "x%.2f");
        ImGui::SliderFloat("Re-flow (soup fills ruts)", &g_tuning.reflow, 0.f, 5.f, "x%.2f");
    }
    if (ImGui::CollapsingHeader("Vehicle")) {
        ImGui::SliderFloat("Springs", &g_tuning.spring, 0.3f, 3.f, "x%.2f");
        ImGui::SliderFloat("Dampers", &g_tuning.damper, 0.3f, 3.f, "x%.2f");
        ImGui::SliderFloat("Engine torque", &g_tuning.engineTorque, 0.3f, 4.f, "x%.2f");
        ImGui::SliderFloat("Firm-ground grip", &g_tuning.firmGrip, 0.3f, 2.f, "x%.2f");
        ImGui::SliderFloat("Tire pressure (psi)", &v.psi, 5.f, 35.f, "%.1f");
        ImGui::Checkbox("Rear locker", &v.lockRear);
        ImGui::SameLine();
        ImGui::Checkbox("Front locker", &v.lockFront);
        ImGui::SameLine();
        ImGui::Checkbox("4WD", &v.fourWD);
        ImGui::Checkbox("Low range", &v.lowRange);
        ImGui::SameLine();
        ImGui::Checkbox("Manual", &v.manual);
        ImGui::SliderFloat("Radiator clog", &v.radiatorClog, 0.f, 1.f);
        ImGui::SliderFloat("Engine temp C", &v.engineTemp, 20.f, 140.f);
        ImGui::SliderFloat("Mud on body", &v.dirt, 0.f, 1.f);
    }
    if (ImGui::CollapsingHeader("Telemetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        rSpeed.plot("mph", 0, 80);
        rRpm.plot("rpm", 0, v.engine->redlineRpm * 1.1f);
        rSlip.plot("avg slip", 0, 1.2f);
        rBelly.plot("belly load", 0, 1.5f);
        rTemp.plot("temp C", 60, 140);
        if (ImGui::BeginTable("wheels", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableSetupColumn("");
            ImGui::TableSetupColumn("FL");
            ImGui::TableSetupColumn("FR");
            ImGui::TableSetupColumn("RL");
            ImGui::TableSetupColumn("RR");
            ImGui::TableHeadersRow();
            auto row = [&](const char* name, auto fn, const char* fmt) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(name);
                for (auto& w : v.wheels) { ImGui::TableNextColumn(); ImGui::Text(fmt, fn(w)); }
            };
            row("load N", [](const WheelState& w) { return w.load; }, "%.0f");
            row("Fx N", [](const WheelState& w) { return w.fx; }, "%.0f");
            row("slip", [](const WheelState& w) { return w.slipRatio; }, "%.2f");
            row("mud w", [](const WheelState& w) { return w.mudW; }, "%.2f");
            row("rut m", [](const WheelState& w) { return w.rutWall; }, "%.2f");
            row("packed", [](const WheelState& w) { return w.pack; }, "%.2f");
            row("water m", [](const WheelState& w) { return w.waterDepth; }, "%.2f");
            ImGui::EndTable();
        }
        TerrainSample ts = g.world.terrain.sample(v.pos.x, v.pos.z);
        ImGui::Text("under truck: moist %.2f  mud %.2f m  water %.2f m  comp %.3f", ts.moisture, ts.mudDepth(), ts.waterDepth, ts.compaction);
    }
    if (ImGui::CollapsingHeader("Venue & weather")) {
        if (ImGui::Button("Wet the pit (+rain)")) soilRegion(g, 0.08f, 0.05f);
        ImGui::SameLine();
        if (ImGui::Button("Dry the pit")) soilRegion(g, -0.08f, -0.05f);
        if (ImGui::Button("Reset barrels & fence")) {
            g.world.resetDynamics();
            for (auto& p : g.world.posts) p.lean = 0;
        }
        static float az = 225, el = 38;
        if (ImGui::SliderFloat("Sun azimuth", &az, 0, 360) | ImGui::SliderFloat("Sun elevation", &el, 5, 85)) g.renderer.setSun(az, el);
        ImGui::Checkbox("Post FX + lens mud", &g.renderer.postEnabled);
        ImGui::SliderInt("Quality", &g.renderer.quality, 0, 2);
    }
    if (ImGui::CollapsingHeader("Audio mix")) {
        ImGui::SliderFloat("Master", &g.audio.masterVolume, 0, 1.5f);
        ImGui::SliderFloat("Engines", &g.audio.engineVolume, 0, 2);
        ImGui::SliderFloat("Mud / FX", &g.audio.sfxVolume, 0, 2);
        ImGui::SliderFloat("Ambience", &g.audio.ambienceVolume, 0, 2);
    }
    if (ImGui::Button("Reset all tuning")) g_tuning = Tuning{};
    ImGui::End();
    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g.renderer.width, g.renderer.height);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace mb
#else
namespace mb {
void DevUI::init() {}
void DevUI::frame(Game&) {}
}  // namespace mb
#endif
