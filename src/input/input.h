// Unified input: W3C Gamepad API (standard mapping) via Emscripten, keyboard and mouse.
//
// Gamepad (Xbox / PlayStation layout, "standard" mapping):
//   RT throttle . LT brake / reverse . Left stick steer . Right stick look
//   A handbrake . B cycle lockers . X / Y shift down / up (manual) . LB low range
//   RB camera . View/Back dozer tow . Menu/Start pause . L3 reset . R3 horn
//   D-pad up/down on-board air up/down . D-pad left auto/manual . D-pad right winch
//   (View now opens the recovery menu: scout / shovel / boards / winch / dozer)
#pragma once
#include "../core/math.h"

namespace mb {

struct InputState {
    float throttle = 0, brake = 0, steer = 0, air = 0;
    float lookX = 0, lookY = 0;
    bool handbrake = false;
    bool shiftUp = false, shiftDown = false, locker = false, lowRange = false, camera = false;
    bool reset = false, tow = false, horn = false, pause = false, manualToggle = false;
    bool scout = false, shovel = false, boards = false, winch = false;   // recovery kit
    bool gamepad = false;
};

class Input {
public:
    void init();
    InputState poll(float dt);
    void rumble(float strong, float weak, int ms);
    bool enabled = true;
    // Optional UI overlay (Dear ImGui) gets first look at mouse events; return true to capture.
    static bool (*mouseHook)(int type, float x, float y, int button);
    bool devTogglePressed();   // F1 / backtick edge, independent of `enabled`
    // On-screen touch controls (web/app.js): analog axes merged like a gamepad, buttons
    // delivered as one-shot presses through the same edge handling as keys/pad buttons.
    void setTouch(float throttle, float brake, float steer, bool handbrake, float air, float lookX, float lookY);
    void pressVirtual(const char* name);
    bool gamepadConnected = false;
    char gamepadName[64] = {0};

private:
    float kbSteer = 0;
    bool prevButtons[20] = {false};
    bool prevKeys[20] = {false};
    bool prevDev = false;
    struct Touch { float throttle = 0, brake = 0, steer = 0, air = 0, lookX = 0, lookY = 0; bool handbrake = false; } touch;
    unsigned virtualPresses = 0;
};

}  // namespace mb
