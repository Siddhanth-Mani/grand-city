# Architecture

## Layers (dependencies flow down only)

```
Game (World, Missions, Traffic, Police)
  -> Physics (Vehicle, Character, Collision)
  -> Scripting (LuaVM)
  -> Engine (Application, Window, Scene, Resources)
  -> Renderer (IRenderer: D3D12 / Vulkan / Metal / Null)
  -> Core (Math/glm, Logger, Config, Clock, Events, Arena, Input)
```

Game code never includes a graphics API header. It talks to `IRenderer`
only: `beginFrame`, `drawMesh`, `setCamera`, `setSun`, `present`.

## The loop

`Application::run` is the classic loop: poll window events, run fixed
1/60 s physics steps (max 5 per frame, then time is dropped), run the
variable `World::update` for timers and HUD, then render. The null renderer
validates every draw (no NaN matrices, non-empty meshes) and logs a summary
every 600 frames — that is how the Linux CI path tests the game.

## Renderer backends

- **D3D12** (`src/renderer/d3d12/`): device, command queue, flip-discard swap
  chain, RTV heap, 2 frames in flight, fence sync, debug layer + PIX markers
  in Debug builds. Per-mesh uploads go through a default-heap vertex/index
  buffer cache (design documented in the .cpp; the upload cache is the first
  thing to finish on the first Windows build).
- **Vulkan** (`src/renderer/vulkan/`): instance, physical device, logical
  device, swapchain extensions, validation layers in Debug, SPIR-V modules
  from `shaders/spirv/`. Queue family 0 is used in this version; the full
  design picks the graphics family by capability flags.
- **Metal** (`src/renderer/metal/`): minimal stub, macOS only, clearly labelled.
- **Null** (`src/renderer/null_renderer.cpp`): headless, validates draws.

## What is verified, and what is not

| Item | Status |
|---|---|
| Linux build, null renderer, `gc6_tests` | Verified on the build machine (see test output) |
| Headless run: 600 frames, world + traffic + police | Verified |
| Lua mission loading + `onStart` | Verified |
| D3D12 backend compile/run | NOT verified — needs Windows + Windows SDK |
| Vulkan backend compile/run | NOT verified — needs Windows + Vulkan SDK |
| Metal backend | Stub only — needs macOS + MetalKit |
| Shaders compile (dxc / glslangValidator) | NOT verified — needs those tools installed |

## D3D12 upload cache (to finish on Windows)

`drawMesh` currently counts the draw; the production path caches, per mesh
pointer, an upload: create a default-heap vertex buffer + index buffer,
upload via an upload heap + copy command list, then `DrawIndexedInstanced`.
Constant buffers are per-frame upload heaps (256-byte aligned) holding the
model matrix and albedo, bound through the root signature in
`shaders/lit.hlsl` (`b0` camera, `b1` model).

## v2 systems (2026-11-20)

### ECS + jobs

`src/core/ecs.h` — a small Registry (entities + Transform / VehicleComp /
PedComp / RenderComp / AIComp). The World creates one entity per traffic car,
pedestrian and the player, and syncs transforms every fixed update; systems
iterate components instead of ad-hoc vectors. `src/core/jobs.h` — a fixed
thread pool with `submit` / `parallelFor` / `waitAll`. City generation runs
the road-graph build, side-activity placement and police routing as parallel
jobs; traffic AI decisions run through `parallelFor` (each task writes only
its own car's decisions, so there are no data races).

### Game flow

`src/game/menus.h` — `GameFlow`, a state machine: Title, Settings, Garage,
MissionSelect, HowToPlay, Loading, Playing, Paused, MissionComplete,
MissionFailed, GameComplete. Every button maps to a real action; the
Application drives it with Up/Down + E (the input enum has no digit keys).
`src/game/story.h` — the 6-mission campaign with cutscenes, unlocks and
star ratings. `src/game/weather.h` — Clear/Rain/Fog with a 5-second fade;
rain cuts grip via `gripMultiplier()`.

### PBR, shadows, post

`IRenderer` gained defaulted virtuals (`drawMeshPBR`, `setLights`,
`setPostSettings`, `setWireframe`) so old backends keep working. The null
renderer overrides them to validate materials and log the post passes.
D3D12 gained a descriptor-heap manager, a constant-buffer ring, a barrier
tracker (only records transitions when the state changes), a 2048 sun
shadow map, and a PBR PSO compiled from `lit_pbr.hlsl`. Vulkan gained a
descriptor pool, a pipeline cache (persisted to `pipeline_cache.bin`),
and MSAA sample-count selection.

### Audio

`src/audio/audio.h` — miniaudio pimpl. Note the pimpl rule this codebase
learned the hard way: a class holding `unique_ptr<Impl>` must define its
constructor out-of-line (`AudioEngine::AudioEngine() = default;` in the
.cpp), or GCC instantiates `unique_ptr<Impl>`'s destructor where Impl is
incomplete. `src/audio/radio.h` — three synthesised stations.

### Networking

`src/net/` — UDP snapshot protocol (38-byte binary snapshots, magic
`0x47433653`). Foundation only: no lobby, no server authority, no real
network play tested. The live loopback test is skipped in sandboxes that
block UDP sends (see `docs/NETWORKING.md`).

### Shutdown and leaks

`World::shutdown()` closes the Lua VM and the audio engine and dumps
`profile.log`. The ASan+UBSan run of the test suite is leak-free after
this fix (before it, 84 KB in 1113 allocations leaked — all indirect,
all owned by Lua states that were never closed; zero direct leaks).

## Verification (2026-11-20)

| Item | Status |
|---|---|
| Linux configure + build, `-Wall -Wextra -Werror` | Exit 0, zero warnings |
| `gc6_tests`: 147 checks | 147 passed, 0 failed (`ctest` 100%) |
| Headless playthrough (new game → mission 1 → buy car → save → load) | Passed, in the test suite |
| Headless run 600 frames = 600 fixed physics steps | Exit 0 (headless mode now uses a fixed 1/60 s dt, so frames really simulate) |
| ASan + UBSan test run | No errors, no UB, no leaks (after the shutdown fix) |
| Asset check | All files present, JSON valid, 6 cars, 6 missions, Lua parses |
| TODO/FIXME/XXX in game code | None (only in vendored `miniaudio.h`) |
| D3D12 / Vulkan compile/run | NOT verified — needs Windows + SDKs |
| Metal | Stub only |
| Live UDP loopback | SKIPPED in this VM sandbox (UDP sends blocked); protocol round-trip tested |
