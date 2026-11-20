# Capturing a frame: RenderDoc and PIX

## RenderDoc (works with D3D12 and Vulkan)

1. Install RenderDoc from renderdoc.org and launch Grand City 6 **from
   RenderDoc**: File → Launch Application, set the executable to
   `build/Release/grand-city-6.exe` and the working directory to the project
   root (so `data/` and `scripts/` resolve).
2. Play until the scene you want to inspect is on screen, then press the
   capture key (Print Screen by default) or use the in-engine markers:
   `RenderDoc::startFrameCapture()` / `endFrameCapture()` wrap a frame when
   the RenderDoc DLL is present; without it they are safe no-ops.
3. In the capture, check:
   - **Texture Viewer**: is the albedo texture bound? Is the sky drawn first?
   - **Mesh Viewer**: draw-call count vs `drawsThisFrame()` — they must agree.
   - **Pipeline State**: is the lit PSO bound with the right root signature?
   - **Event Browser**: find the most expensive draw; a single building
     draw that costs more than the whole car is a red flag.

## PIX (Windows, Direct3D 12 only)

1. Install PIX from the Microsoft Store (it needs the Windows SDK).
2. Launch the game from PIX with "GPU captures" enabled.
3. Take a GPU capture (F11 or the PIX UI). Look at:
   - **PIX markers**: frames are wrapped in `PIXBeginEvent("Frame")` and
     each mesh batch should be wrapped in its own event (add more markers
     around the car/traffic draws when profiling).
   - **Timing**: the GPU timeline shows how long the lit pass takes vs
     present. If present dominates, you are vsync-bound, not GPU-bound.
   - **Debug layer**: run a Debug build first — D3D12 debug layer messages
     (enabled in `D3D12Renderer::init` under `_DEBUG`) catch resource
     lifetime mistakes before they become GPU crashes.

## What "good" looks like

A 60 fps frame at 1280x720 should show: one sky draw, one ground draw, one
draw per building batch, one per car, a handful for pedestrians and lamps,
and a present under 2 ms. If the draw count in RenderDoc is 10x the count
the null renderer logs for the same scene, something is being drawn twice.
