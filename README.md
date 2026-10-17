# Mud Buddies

An online multiplayer **mud bogging sim-cade** — C++20 compiled to WebAssembly, WebGL2
rendering, fully procedural audio, gamepad-first controls, competitive modes, leaderboards and a
parametric CAD pipeline for every asset. The venue, vehicles and failure modes are modeled on the
reference photos (see [docs/FORENSICS.md](docs/FORENSICS.md)).

> Bog it. Sling it. Get stuck with your buddies.

## Play
```bash
node server/server.js          # serves web/ + multiplayer + leaderboards on :8787
open http://localhost:8787
```
The prebuilt WASM is committed in `web/build/`, so no toolchain is needed to play. Any static
host works for single-player (`web/` folder); multiplayer and global leaderboards need the Node
server (zero npm dependencies, Node ≥ 18).

## What's in it
| | |
|---|---|
| **Terramechanics** | Bekker pressure–sinkage per terrain cell with viscous sinkage rate (momentum matters), Janosi–Hanamoto shear traction, Rankine bulldozing, suction, slurry/water drag. Tires dig holes, roost re-deposits where it lands, ruts fill with water, soupy mud flows back. [docs/PHYSICS.md](docs/PHYSICS.md) |
| **Getting stuck** | High-centering on diff pumpkins and frame rails, tread packing with clay (wheel speed flings it out), open diffs vs lockers, hydrolock, radiator clogging → overheating. Ways out: rock it, air down, lockers, 4-LO, or the dozer tow. |
| **Vehicles** | '88 Comanche MJ, '76 full-size bog car, crew-cab diesel mud truck, sport UTV; 6 engines; tire size 31–44", width, A/T / M/T / paddles, lift, axle gears, lockers, 4WD, snorkel, on-board air, paint. |
| **Audio** | Per-cylinder firing-order pulses into exhaust waveguides (real cross-plane V8 burble, limiter bounce, pops), diesel clatter, turbo/blower, lug-slap granular mud, Minnaert water bubbles, debris patter, barrel modal synthesis, crowd, dozer idle, tree-line slapback, Doppler. [docs/AUDIO.md](docs/AUDIO.md) |
| **Modes** | **Mud Bog** (4-lane drag pit: full pass = time, else distance), **Hill & Hole** (3-lap circuit), **Mud Slinger** (90 s freestyle combo scoring), **King of the Pit** (120 s zone control), Free Roam. |
| **Leaderboards** | Fastest pass, farthest distance, best lap, race time, freestyle score, crown time — server-validated, with local fallback. |
| **Multiplayer** | Rooms / quick match (8 players), lane assignment, clock-synced countdowns, 20 Hz snapshots with Hermite interpolation, chat. [docs/NETCODE.md](docs/NETCODE.md) |
| **CAD repo** | `cad/` — parametric `.mbcad` part files are the single source of truth; the game builds meshes from them at runtime; `mbcad` exports OBJ + OpenSCAD. [cad/README.md](cad/README.md) |
| **Input** | W3C Gamepad API (Xbox/PlayStation standard mapping) with rumble, full menu navigation by pad; keyboard + mouse. |

## Controls
| Action | Gamepad | Keyboard |
|---|---|---|
| Throttle / brake-reverse | RT / LT | W / S |
| Steer · look | Left stick · right stick | A / D · mouse drag |
| Handbrake | A | Space |
| Lockers (off → rear → F+R) | B | L |
| Shift down / up (manual) · auto/manual | X / Y · D-pad ← | Q / E · M |
| Low range | LB | G |
| Air up / down | D-pad ↑ / ↓ | F / V |
| Camera | RB | C |
| Dozer tow · reset · horn · pause | View · L3 · R3 · Menu | T · R · H · Esc |

## Build from source
```bash
# Native: tests + tools (mbcad, render_wav, audio_probe)
cmake -B build-native -DCMAKE_BUILD_TYPE=Release && cmake --build build-native -j
./build-native/mb_tests && node tests/server_test.mjs

# WebAssembly (requires emsdk)
./build.sh            # = emcmake cmake -B build-wasm && cmake --build build-wasm  → web/build/
```

## Layout
```
src/core      math, noise            src/terrain  deformable multi-layer heightfield
src/physics   terramechanics, vehicle, catalog
src/cad       mbcad kernel           src/audio    DSP primitives + game audio engine
src/net       snapshots, interpolation
src/game      world builder, modes, particles, orchestration
src/render    WebGL2 renderer        src/input    gamepad / keyboard / mouse
src/main.cpp  WASM C API             web/         HTML/CSS/JS shell
server/       Node WebSocket + leaderboards      cad/  parametric asset repo
tools/        mbcad, render_wav, audio_probe     tests/  native + server tests
docs/         FORENSICS, PHYSICS, AUDIO, NETCODE, TECH_STACK
```

## Notes
* Only still photos were provided as references; the sound design is modeled from the hardware
  in them and known mud-bog acoustics. `tools/audio_probe` exists to calibrate against real clips.
* Sponsor logos seen in the photos are intentionally not reproduced in assets.
* Open-source libraries, APIs, algorithms and references that would take this further are
  catalogued in [docs/TECH_STACK.md](docs/TECH_STACK.md).
