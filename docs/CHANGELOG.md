# CHANGELOG

## v2 — 2026-11-20

The big "finish the game" upgrade. Everything below is real, working code
verified on the Linux VM (null renderer); see README.md for the honest
limits (D3D12/Vulkan still need Windows, Metal is a stub).

### 1. PBR rendering

- `src/renderer/material.h/.cpp` — PBR material system (albedo, metalness,
  roughness, normal map path, emissive) and `data/materials.json` (12
  materials: car paints, glass, rubber, chrome, asphalt, concrete, lit
  windows, headlight/taillight emissives).
- `shaders/lit_pbr.hlsl` — Cook-Torrance BRDF (GGX + Schlick Fresnel +
  Smith geometry), environment-cubemap IBL approximation with
  roughness-based mip selection, 3×3 PCF shadow mapping, up to 16 dynamic
  point lights. `shaders/glsl/pbr.vert/.frag` — the Vulkan equivalent.
- Old `lit.hlsl` still works and is still used by the legacy path.
- `IRenderer` gained defaulted `drawMeshPBR` / `setLights` /
  `setPostSettings` / `setWireframe` so existing backends keep working.

### 2. Shadows + lighting

- `src/renderer/lights.h` — LightRig: directional sun with shadow-map
  config (2048, 9-tap PCF), 16-light forward list (streetlamps,
  headlights, police light bars).
- D3D12: 2048 shadow map + depth-stencil heap + PBR PSO from
  `lit_pbr.hlsl`. Vulkan: same surface, descriptor pool + pipeline cache.
- Day/night sun + sky update in the World (sun elevation drives the
  directional light and the fog/sky colour).

### 3. Post-processing

- `src/renderer/post.h/.cpp` — PostSettings with toggles and
  `postPassList()`: scene + bright-pass + blur-H + blur-V + bloom-composite
  + ACES + vignette + FXAA = 8 passes with everything on.
- `shaders/post.hlsl` (bright-pass, 9-tap Gaussian blur, ACES, vignette,
  FXAA-lite) and `shaders/glsl/post.vert/.frag`.
- Toggles in `data/settings.ini` `[post]`: bloom, tonemapping, vignette,
  fxaa, exposure. Null renderer logs the pass list.

### 4. Better physics

- `src/physics/tires.h/.cpp` — simplified Pacejka magic formula,
  per-surface parameters (tarmac / wet / gravel), friction circle,
  `surfaceAt(x, z)`.
- `src/physics/drivetrain.h/.cpp` — engine torque curve by RPM, gearbox
  with real gear ratios, shift points, clutch slip on launch and while
  shifting, differential, per-drivetrain behaviour (AWD grips, RWD slides,
  FWD understeers, 4x4 off-road), ABS (~8 Hz pulsing), traction control,
  aero drag + downforce, brake fade (temperature model), damage model.
- `src/physics/obb.h/.cpp` — OBB overlap via separating axis theorem,
  swept sub-step collision (max 0.5 m per step) so fast cars cannot tunnel.
- `src/physics/vehicle.h/.cpp` rewritten — per-wheel raycast suspension
  (spring + damper from ray length), weight transfer (longitudinal and
  lateral), airborne / stunt launch detection, OBB car–car collision.
- New tests: suspension travel, Pacejka grip, swept collision, gear
  shifting, ABS vs no-ABS stopping distance, RWD vs AWD launch,
  downforce at speed, damage affecting handling.
- Formulas documented in `docs/PHYSICS.md`.

### 5. ECS + jobs

- `src/core/ecs.h/.cpp` — Registry with Transform / VehicleComp / PedComp
  / RenderComp / AIComp; entities are `uint32_t`.
- `src/core/jobs.h/.cpp` — fixed thread pool, `parallelFor` / `waitAll`.
  Used by city generation and traffic AI updates.
- Documented in `docs/ARCHITECTURE.md`.

### 6. Smarter AI

- `src/game/pathfinding.h/.cpp` — RoadGraph with A* on the procedural
  road network.
- Traffic: A* routes, lane-changing, yielding at intersections,
  `aiDecide` / `physicsStep` split so decisions can run on jobs.
- Pedestrians: avoidance of cars (car positions / forward vectors).
- Police: A* routing to the player (no longer steering directly),
  roadblocks at wanted level 3+.

### 7. 3D audio

- Vendored miniaudio 0.11.26 (`src/audio/miniaudio.h`, MIT-0) —
  `src/audio/audio.h/.cpp` — positional engine sounds (pitch by RPM and
  car type), sirens, collision thuds, distance attenuation, mute/volume
  from settings. Documented in `docs/AUDIO.md`.
- `src/audio/radio.h/.cpp` — three synthesised radio stations (City FM
  112 BPM, Night Drive 84 BPM, Turbo 140 BPM) with station switching and
  mute.
- Pimpl pitfall fixed: `AudioEngine()` is defined out-of-line so
  `unique_ptr<Impl>` never destructs where Impl is incomplete.

### 8. Save/load + economy

- `src/game/savegame.h/.cpp` — JSON save (position, money, cars owned,
  mission progress, star ratings, upgrades, wanted level, play time).
- `src/game/economy.h/.cpp` — garage: buy cars (6), paint ($500), upgrades
  (Engine / Brakes / Tyres / Suspension, 3 levels each) that actually
  change handling via `upgradedParams`.
- Verified by the in-suite headless playthrough (new game → mission 1 →
  buy car → save → load → save matches).

### 9. More game content

- 6 Lua missions (missions 4–6 new): taxi fares, police-escape time trial
  (120 s at 4 stars), stunt-jump air-time mission.
- 50 hidden packages ($50 each, counter in HUD) and 10 stunt jumps with
  air-time scoring — `src/game/collectibles.h/.cpp`.
- Weather: Clear / Rain (wet-road grip change) / Fog, selectable and used
  by missions — `src/game/weather.h/.cpp`.
- Day/night cycle in the World.

### 10. Developer tools

- `src/renderer/shader_watcher.h/.cpp` — shader hot-reload (mtime polling,
  Debug builds).
- Lua hot-reload for missions (`LuaVM::reloadMission`).
- `src/game/debug_overlay.h` — FPS, draw calls, physics time, entity
  counts, wireframe toggle (Shift+F in-game).
- `src/core/profiler.h/.cpp` — per-system timings to `profile.log`;
  `tools/profiler.py` summarises it.

### 11. Better renderer backends

- D3D12: DescriptorHeapManager, ConstantBufferRing, BarrierTracker
  (transitions only when the state changes), 2048 shadow map, PBR PSO,
  MSAA option.
- Vulkan: descriptor pool, pipeline cache (`pipeline_cache.bin`), MSAA
  sample-count selection.
- Both still Windows-only behind `#ifdef _WIN32` and still not compiled
  here.

### 12. Networking foundation

- `src/net/net.h/.cpp` — UDP snapshot protocol (38-byte binary snapshots,
  magic `0x47433653`) with a localhost loopback test. Documented in
  `docs/NETWORKING.md`. Foundation only: no lobby, no server authority,
  no real network play tested. Live loopback is skipped in the VM sandbox
  (UDP sends blocked with EPERM); the protocol is verified by
  serialize/deserialize round-trip.

### Cars (realistic, original — no real brands)

- 6 cars in `data/cars.json`: Vortex R supercar (640 hp, AWD, 0–100
  2.9 s, $180,000), Falcon GT sports coupe (450 hp, RWD, $85,000),
  Thunderhawk muscle (480 hp, RWD, $65,000), Meridian sedan (300 hp,
  RWD, $0 starter), Bison 4x4 SUV (400 hp, AWD, $55,000), Ranger X
  pickup (350 hp, 4x4, $45,000). Each with horsepower, torque curve,
  weight, weight distribution, drivetrain, top speed, 0–100, gear ratios.
- Procedural detail: distinct silhouettes, turning front wheels,
  headlights/taillights, mirrors, spoilers, rims, cockpit interior with
  a turning steering wheel.
- Distinct engine sound per car (pitch + character by RPM and car type),
  skid / crash / backfire sounds.

### Finished game

- `src/game/menus.h/.cpp` — title menu (New Game, Continue, Garage,
  Missions, Settings, How to Play, Quit), pause menu (Resume, Settings,
  Save & Quit), mission-complete / mission-fail / game-complete screens
  with stats and credits, loading screen with tips. Every button does
  something real.
- `src/game/story.h/.cpp` — original story campaign (Ray, Mara, the
  new driver) across all 6 missions, with brief in-engine cutscenes
  (camera moves + dialogue) and unlocks.
- Mission select with locked / unlocked / complete + star ratings.
- HUD: minimap, speed, gear, RPM, money, health, wanted stars,
  objective, timer, message feed, packages counter, stunt score,
  weather, radio station, damage/smoke, screen shake.
- Photo mode (P): freeze time, free camera, hide HUD. Speed-sensitive
  chase camera with FOV widening, handbrake turns, drift.
- Settings: graphics quality, resolution, fullscreen, volume, mute,
  assists ABS/TC/ESP on/off, controls list, English.

### QA fixes during this upgrade

- Headless mode now uses a fixed 1/60 s dt, so `--frames 600` really
  simulates 10 s of game time (previously it spun with ~0.02 ms real dt
  and ran almost no fixed updates).
- ASan/UBSan leak fix: `World::shutdown()` now closes the Lua VM and the
  audio engine and dumps `profile.log`; `Application` and the test suite
  call it. Before: 84,073 bytes leaked in 1,113 allocations — all
  indirect, all owned by never-closed Lua states, zero direct leaks.
  After: ASan exit 0, no leaks, no UB.
- Net loopback: verified the sandbox blocks UDP sends (EPERM) with a raw
  C socket and Python; the test now verifies the protocol by round-trip
  and reports the live loopback as SKIP with the reason.

## v1 — 2026-11-20

- C++17 CMake project, null renderer, D3D12/Vulkan/Metal (stub) backends,
  HLSL + GLSL shaders, vehicle physics, procedural city (89 buildings,
  100 road nodes), traffic, pedestrians, police + wanted stars, 3 Lua
  missions, day/night, Python tools, 26 tests passing, headless 600 frames.

### QA fixes found by the final verification pass (same day)

- **Audio heap-use-after-free (crash):** `ma_audio_buffer` references the
  caller's PCM instead of copying it, but `AudioEngine::init()` built the
  skid/siren/crash/pickup/radio-tone sounds from block-local vectors that
  died at the end of `init()` — miniaudio's device thread then read freed
  memory and the game segfaulted on any machine with an audio device
  (20/20 runs without `GC6_NO_AUDIO`). Fixed by owning all PCM in
  `AudioEngine::Impl` (`skidPcm`, `sirenPcm`, `crashPcm`, `pickupPcm`,
  `tonePcm`, `enginePcmByCar`), initialising engine sounds in place in
  the maps (a sound keeps a raw pointer to its data source, so building
  from a local buffer and copying it also dangled), and uninitialising
  sounds before their buffers on shutdown. Verified with ASan (heap-use-
  after-free stack) and 30/30 clean runs with audio enabled.
- **Job-system lost wakeup (intermittent hang):** workers decremented
  `pending_` and called `cvDone_.notify_all()` without holding the pool
  mutex, so the notification could land between the main thread's
  predicate check and it blocking in `waitAll()` — the game then slept
  forever with every task finished and `pending_ == 0`. Found with a
  temporary signal-based stack dumper (main in `waitAll`, all 4 workers
  idle, counters showing submitted == completed) and reproduced in
  isolation with a stress test that hung within 20,000 `parallelFor`
  calls. Fixed by decrementing `pending_` and notifying under the mutex.
  The stress test now completes 200,000 iterations (2.8M tasks) and the
  game passes 30/30 headless runs with audio enabled. The temporary
  watchdog code was removed from `src/main.cpp` / `src/engine/application.cpp`
  after the fix.
