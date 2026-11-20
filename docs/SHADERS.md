# Shaders

## HLSL (Direct3D 12, Shader Model 6.0)

| File | What it does |
|---|---|
| `shaders/lit.hlsl` | Main lit shader. Textured, directional sun + ambient, distance fog. `b0` = camera (viewProj, sun, fog), `b1` = model matrix + albedo, `t0` = albedo texture. |
| `shaders/sky.hlsl` | Sky dome: gradient horizon-to-zenith, sun disc (`pow(dot, 350)`) and haze. Drawn at depth = far plane. |
| `shaders/unlit.hlsl` | Flat colour for HUD and debug geometry. |

Compile with dxc (ships with the Windows SDK):

```
dxc -T vs_6_0 -E VSMain shaders/lit.hlsl -Fo build/shaders/lit_vs.cso
dxc -T ps_6_0 -E PSMain shaders/lit.hlsl -Fo build/shaders/lit_ps.cso
```

Or run `python tools/compile_shaders.py`, which also handles the GLSL side
and skips gracefully when a compiler is missing.

## GLSL (Vulkan, SPIR-V)

`shaders/glsl/lit.vert` / `lit.frag` are the Vulkan equivalents of `lit.hlsl`,
with the same bindings (0 = camera, 1 = model, 2 = albedo texture). Compile:

```
glslangValidator -V shaders/glsl/lit.vert -o build/shaders/lit.vert.spv
glslangValidator -V shaders/glsl/lit.frag -o build/shaders/lit.frag.spv
```

## Lighting model

One directional sun + a constant ambient term (0.25). Fog is exponential
squared, sky-coloured, so distant buildings fade into the horizon. Night
lowers sun intensity to 0.25 and the engine is expected to add streetlight /
headlight point lights next — the constant-buffer layout already reserves
room for them.

## PBR (v2)

| File | What it does |
|---|---|
| `shaders/lit_pbr.hlsl` | Cook-Torrance BRDF: GGX distribution, Schlick Fresnel, Smith geometry. Albedo / metalness / roughness per material, IBL approximated from an environment cubemap (roughness selects the mip — labelled as an approximation, not a full split-sum IBL), one sun with a 3×3 PCF shadow map, up to 16 dynamic point lights (forward list: streetlamps, headlights, police bars). |
| `shaders/glsl/pbr.vert` / `pbr.frag` | Vulkan equivalents of the above. |

Compile: `dxc -T vs_6_0 -E VSMain shaders/lit_pbr.hlsl`, `dxc -T ps_6_0 -E PSMain shaders/lit_pbr.hlsl`; GLSL with `glslangValidator -V`.

Materials live in `data/materials.json` (12 entries: car paints, glass,
rubber, chrome, asphalt, concrete, lit windows, headlight/taillight
emissives) and load through `src/renderer/material.h`.

## Post-processing (v2)

| File | What it does |
|---|---|
| `shaders/post.hlsl` | Fullscreen-triangle chain: bright-pass (bloom source) → 9-tap Gaussian blur (H+V) → bloom composite → ACES tone mapping (Narkowicz fit) → vignette → FXAA-lite (luma-edge blend; not the full FXAA algorithm). Each pass is a separate entry point. |
| `shaders/glsl/post.vert` / `post.frag` | Vulkan equivalents; one shader, push-constant `mode` selects composite / bright-pass / FXAA. |

Toggles live in `data/settings.ini` `[post]` (`bloom`, `tonemapping`,
`vignette`, `fxaa`, `exposure`) and `[renderer]` (`msaa`, `msaaSamples`).
The null renderer logs the pass list instead of running it
(`postPassList()` in `src/renderer/post.cpp`: 8 passes with everything on).
