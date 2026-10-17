// In-game developer panel built on Dear ImGui (toggle: F1 or `).
// Live-tunes the terramechanics/vehicle multipliers, plots telemetry, edits the venue
// (wet / dry the pit, regenerate), the sun, and the audio mix.
#pragma once

namespace mb {
class Game;

class DevUI {
public:
    void init();
    void frame(Game& g);
    bool visible = false;

private:
    bool ready = false;
    double lastMs = 0;
};

}  // namespace mb
