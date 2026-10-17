#include "input.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace mb {

namespace {
std::unordered_map<std::string, bool> g_keys;
float g_mouseDX = 0, g_mouseDY = 0;
bool g_mouseDown = false;

EM_BOOL onKey(int type, const EmscriptenKeyboardEvent* e, void*) {
    bool down = type == EMSCRIPTEN_EVENT_KEYDOWN;
    g_keys[e->code] = down;
    std::string c = e->code;
    // Swallow game keys so the page doesn't scroll, but let typing in UI inputs through.
    bool game = c.rfind("Arrow", 0) == 0 || c == "Space";
    return game ? EM_TRUE : EM_FALSE;
}
EM_BOOL onMouse(int type, const EmscriptenMouseEvent* e, void*) {
    if (Input::mouseHook) {
        double cw = 1, ch = 1;
        emscripten_get_element_css_size("#canvas", &cw, &ch);
        int pw = 1, ph = 1;
        emscripten_get_canvas_element_size("#canvas", &pw, &ph);
        float x = float(e->targetX * pw / cw), y = float(e->targetY * ph / ch);
        if (type != EMSCRIPTEN_EVENT_MOUSEDOWN) {   // window-level events: targetX is canvas-relative only for canvas
            x = float((e->clientX) * pw / cw);
            y = float((e->clientY) * ph / ch);
        }
        if (Input::mouseHook(type, x, y, e->button)) { g_mouseDown = false; return EM_TRUE; }
    }
    if (type == EMSCRIPTEN_EVENT_MOUSEDOWN) g_mouseDown = true;
    else if (type == EMSCRIPTEN_EVENT_MOUSEUP) g_mouseDown = false;
    else if (type == EMSCRIPTEN_EVENT_MOUSEMOVE && g_mouseDown) {
        g_mouseDX += float(e->movementX);
        g_mouseDY += float(e->movementY);
    }
    return EM_FALSE;
}
EM_BOOL onBlur(int, const EmscriptenFocusEvent*, void*) {
    g_keys.clear();
    g_mouseDown = false;
    return EM_FALSE;
}
bool key(const char* c) {
    auto it = g_keys.find(c);
    return it != g_keys.end() && it->second;
}
// Radial dead zone with rescale, then a response curve for finer control near center.
float shapeAxis(float v, float dead, float expo) {
    float a = std::fabs(v);
    if (a < dead) return 0.f;
    a = (a - dead) / (1.f - dead);
    return signf(v) * std::pow(clampf(a, 0.f, 1.f), expo);
}
}  // namespace

bool (*Input::mouseHook)(int, float, float, int) = nullptr;

bool Input::devTogglePressed() {
    bool k = key("Backquote") || key("F1");
    bool edge = k && !prevDev;
    prevDev = k;
    return edge;
}

void Input::init() {
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onKey);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onKey);
    emscripten_set_mousedown_callback("#canvas", nullptr, EM_TRUE, onMouse);
    emscripten_set_mouseup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onMouse);
    emscripten_set_mousemove_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onMouse);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, onBlur);
}

void Input::rumble(float strong, float weak, int ms) {
    EM_ASM({
        try {
            const pads = navigator.getGamepads ? navigator.getGamepads() : [];
            for (const p of pads) {
                if (p && p.vibrationActuator && p.vibrationActuator.playEffect)
                    p.vibrationActuator.playEffect("dual-rumble", {duration: $2, strongMagnitude: $0, weakMagnitude: $1});
            }
        } catch (e) {}
    }, clampf(strong, 0, 1), clampf(weak, 0, 1), ms);
}

InputState Input::poll(float dt) {
    InputState s;
    if (!enabled) {
        std::memset(prevButtons, 0, sizeof prevButtons);
        return s;
    }
    // ---- keyboard ----
    bool up = key("KeyW") || key("ArrowUp"), down = key("KeyS") || key("ArrowDown");
    bool left = key("KeyA") || key("ArrowLeft"), right = key("KeyD") || key("ArrowRight");
    s.throttle = up ? 1.f : 0.f;
    s.brake = down ? 1.f : 0.f;
    float target = (left ? 1.f : 0.f) - (right ? 1.f : 0.f);
    // Keyboard steering ramps like a real wheel instead of snapping.
    float rate = (target == 0.f || target * kbSteer < 0) ? 6.f : 3.2f;
    kbSteer += clampf(target - kbSteer, -rate * dt, rate * dt);
    s.steer = kbSteer;
    s.handbrake = key("Space");
    s.air = (key("KeyF") ? 1.f : 0.f) - (key("KeyV") ? 1.f : 0.f);
    const char* edgeKeys[] = {"KeyE", "KeyQ", "KeyL", "KeyG", "KeyC", "KeyR", "KeyT", "KeyH", "Escape", "KeyM", "KeyP"};
    bool* targets[] = {&s.shiftUp, &s.shiftDown, &s.locker, &s.lowRange, &s.camera, &s.reset, &s.tow, &s.horn, &s.pause, &s.manualToggle, &s.pause};
    for (int i = 0; i < 11; i++) {
        bool k = key(edgeKeys[i]);
        if (k && !prevKeys[i]) *targets[i] = true;
        prevKeys[i] = k;
    }
    s.horn = s.horn || key("KeyH");
    s.lookX = g_mouseDX * 0.004f;
    s.lookY = g_mouseDY * 0.004f;
    g_mouseDX = g_mouseDY = 0;

    // ---- gamepad ----
    gamepadConnected = false;
    if (emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS) {
        int n = emscripten_get_num_gamepads();
        for (int i = 0; i < n; i++) {
            EmscriptenGamepadEvent g;
            if (emscripten_get_gamepad_status(i, &g) != EMSCRIPTEN_RESULT_SUCCESS || !g.connected) continue;
            gamepadConnected = true;
            std::strncpy(gamepadName, g.id, sizeof gamepadName - 1);
            bool standard = std::strcmp(g.mapping, "standard") == 0;
            auto btn = [&](int b) { return b < g.numButtons && g.digitalButton[b]; };
            auto analog = [&](int b) { return b < g.numButtons ? float(g.analogButton[b]) : 0.f; };
            float lx = g.numAxes > 0 ? float(g.axis[0]) : 0.f;
            float rx = g.numAxes > 2 ? float(g.axis[2]) : 0.f, ry = g.numAxes > 3 ? float(g.axis[3]) : 0.f;
            float rt = standard ? analog(7) : (g.numAxes > 5 ? (float(g.axis[5]) + 1.f) * 0.5f : 0.f);
            float lt = standard ? analog(6) : (g.numAxes > 4 ? (float(g.axis[4]) + 1.f) * 0.5f : 0.f);
            float st = -shapeAxis(lx, 0.1f, 1.35f);
            float thr = shapeAxis(rt, 0.04f, 1.f), brk = shapeAxis(lt, 0.04f, 1.f);
            if (std::fabs(st) > 0.f || thr > 0 || brk > 0) s.gamepad = true;
            if (std::fabs(st) > std::fabs(s.steer)) s.steer = st;
            s.throttle = std::max(s.throttle, thr);
            s.brake = std::max(s.brake, brk);
            s.lookX += shapeAxis(rx, 0.15f, 1.2f) * 2.4f * dt;
            s.lookY += shapeAxis(ry, 0.15f, 1.2f) * 1.6f * dt;
            s.handbrake = s.handbrake || btn(0);
            if (btn(12)) s.air = 1.f;
            if (btn(13)) s.air = -1.f;
            bool* edges[20] = {nullptr, &s.locker, &s.shiftDown, &s.shiftUp, &s.lowRange, &s.camera, nullptr, nullptr,
                               &s.tow, &s.pause, &s.reset, &s.horn, nullptr, nullptr, &s.manualToggle, nullptr};
            for (int b = 0; b < 16; b++) {
                bool d = btn(b);
                if (d && !prevButtons[b] && edges[b]) *edges[b] = true;
                prevButtons[b] = d;
            }
            if (btn(11)) s.horn = true;
            break;  // first connected pad drives
        }
    }
    return s;
}

}  // namespace mb
