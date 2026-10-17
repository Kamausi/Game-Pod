# Tech stack, algorithms and recommended open-source building blocks

✅ = used in this repo today. ➕ = recommended next step for a full AAA production.

## Languages
| Language | Role |
|---|---|
| ✅ **C++20** | Simulation, terramechanics, rendering, audio DSP, netcode, CAD kernel |
| ✅ **GLSL ES 3.00** | WebGL2 shaders (terrain displacement, PCF shadows, fence pattern, water, ACES) |
| ✅ **JavaScript (ES2022)** | Web shell UI, Web Audio bridge, WebSocket transport, zero-dep Node server |
| ✅ **mbcad DSL** + **OpenSCAD** | Parametric asset source + exported `.scad` for CAD tools |
| ✅ **CMake** | Native + Emscripten builds |
| ➕ **WGSL** | WebGPU path (compute-shader terrain deformation, GPU particles) |
| ➕ **Faust** | Author engine/mud DSP graphically, compile to C++ |
| ➕ **Python** (CadQuery / build123d) | B-rep CAD for hero assets with fillets, STEP export |
| ➕ **Lua** (sol2) | Modding: user tracks, game modes |

## Platform APIs
✅ WebAssembly (Emscripten 4+), ✅ WebGL2 (GLES3), ✅ Web Audio, ✅ W3C Gamepad API (standard mapping + `vibrationActuator` dual-rumble), ✅ WebSocket (RFC 6455), ✅ Fetch, ✅ localStorage · ➕ WebGPU, ➕ AudioWorklet + SharedArrayBuffer (needs COOP/COEP), ➕ WebTransport/WebRTC DataChannels (unreliable UDP-like snapshots), ➕ Pointer Lock, ➕ Web Workers/pthreads for physics.

## Algorithms & formulas in the code
| Area | Algorithm |
|---|---|
| Soil | Bekker pressure–sinkage `p = (k_c/b + k_φ) zⁿ`; viscous sinkage rate; compaction memory per cell |
| Traction | Janosi–Hanamoto shear `F = (Ac + W tanφ)(1 − K/(iL)(1 − e^{−iL/K}))`; grouser factor; tread packing |
| Resistance | Bekker compaction `b k zⁿ⁺¹/(n+1)`; Rankine passive earth pressure (bulldozing); hydrodynamic drag `½ρC_dAv²` |
| Firm tires | Pacejka Magic Formula, friction ellipse, slip-stiffness semi-implicit wheel integration |
| Vehicle | 6-DOF rigid body, Euler's rotation equations, raycast spring–damper with curvature-aware footprint sampling, torque converter, open/locked diffs as velocity constraints |
| Terrain | Multi-layer heightfield (surface, hardpan, water table, moisture, compaction, vegetation, tracks), diffusion re-flow, mass-conserving deposition |
| Rendering | Vertex-texture displaced terrain, skirted chunk LOD, Gribb–Hartmann frustum culling, PCF shadow mapping, Fresnel (Schlick) water, value-noise fBm, ACES filmic tonemap, instancing |
| Audio | Firing-order pulse synthesis, digital waveguides, RBJ biquads, Paul Kellet pink noise, granular synthesis, Minnaert resonance + van den Doel bubble model, modal synthesis, Doppler, equal-power panning, Schroeder-style slapback, peak limiter |
| Net | NTP clock offset, snapshot interpolation (cubic Hermite + slerp), bounded extrapolation, quantization |
| CAD | Recursive-descent expression parser, ear-clipping triangulation, lathe/extrude/CSG-free features, cofactor normal transform |
| Forensics | Radix-2 FFT STFT, harmonic product spectrum, spectral centroid, spectral-flux onsets |

## Open-source projects worth pulling in
**Physics / vehicles**
* [Project Chrono](https://github.com/projectchrono/chrono) — *SCM (Soil Contact Model)* deformable terrain and Bekker–Wong terramechanics; the reference implementation for what `Terrain`/`MudModel` approximate. Also has granular (DEM) mud.
* [Jolt Physics](https://github.com/jrouwe/JoltPhysics) — fast, deterministic, WASM-friendly; has a vehicle constraint (wheels, diffs, anti-roll). Good replacement for hand-rolled chassis collisions.
* [PhysX 5](https://github.com/NVIDIA-Omniverse/PhysX) (vehicle SDK), [Bullet](https://github.com/bulletphysics/bullet3) (raycast vehicle).

**Rendering**
* [bgfx](https://github.com/bkaradzic/bgfx), [sokol](https://github.com/floooh/sokol) (tiny, WASM-first), [Filament](https://github.com/google/filament) (PBR), [Dawn](https://dawn.googlesource.com/dawn) / [wgpu-native](https://github.com/gfx-rs/wgpu-native) for WebGPU.
* [meshoptimizer](https://github.com/zeux/meshoptimizer), [cgltf](https://github.com/jkuhlmann/cgltf) / [tinygltf](https://github.com/syoyo/tinygltf), [Basis Universal](https://github.com/BinomialLLC/basis_universal) (KTX2 textures), [Draco](https://github.com/google/draco).

**Audio**
* [miniaudio](https://github.com/mackron/miniaudio) (native backends), [SoLoud](https://github.com/jarikomppa/soloud), [Faust](https://github.com/grame-cncm/faust), [Steam Audio](https://github.com/ValveSoftware/steam-audio) (occlusion/reverb), [libpd](https://github.com/libpd/libpd).
* Reference: Andy Farnell, *Designing Sound* (engine & water chapters); K. van den Doel, "Physically based models for liquid sounds" (2005).

**Networking / backend**
* [GameNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets), [yojimbo](https://github.com/mas-bandwidth/yojimbo), [ENet](https://github.com/lsalzman/enet), [uWebSockets](https://github.com/uNetworking/uWebSockets) (C++ WS server), [Colyseus](https://github.com/colyseus/colyseus) (rooms), [Nakama](https://github.com/heroiclabs/nakama) (accounts, leaderboards, matchmaking).

**Engine plumbing / tools**
* [EnTT](https://github.com/skypjack/entt) (ECS), [GLM](https://github.com/g-truc/glm), [Dear ImGui](https://github.com/ocornut/imgui) (tuning panels for every coefficient in `MudModel`), [Recast/Detour](https://github.com/recastnavigation/recastnavigation) (AI drivers), [Tracy](https://github.com/wolfpld/tracy) (profiling), [doctest](https://github.com/doctest/doctest)/[Catch2](https://github.com/catchorg/Catch2).

**CAD / DCC**
* [OpenSCAD](https://github.com/openscad/openscad), [CadQuery](https://github.com/CadQuery/cadquery), [build123d](https://github.com/gumyr/build123d), [FreeCAD](https://github.com/FreeCAD/FreeCAD), [Blender](https://www.blender.org/) (imports the OBJ exports with vertex colors), [Git LFS](https://git-lfs.com/) for binary art.

## Key references
* M. G. Bekker, *Introduction to Terrain-Vehicle Systems* (1969)
* J. Y. Wong, *Theory of Ground Vehicles*, 4th ed. (2008) — soil table used in `kWongSoils`
* Z. Janosi, B. Hanamoto, "The analytical determination of drawbar pull as a function of slip…" (1961)
* H. B. Pacejka, *Tire and Vehicle Dynamics*
* M. Minnaert, "On musical air-bubbles and the sounds of running water" (1933)
* R. Bristow-Johnson, *Audio EQ Cookbook*
* G. Gambetta, "Fast-Paced Multiplayer" (client prediction / entity interpolation)
