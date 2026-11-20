#pragma once
namespace gc6 {
// RenderDoc in-engine capture: loads renderdoc_app.h API at runtime if the
// RenderDoc DLL is injected (or renderdoc.dll sits next to the exe). If it is
// absent, every call is a safe no-op — the game never depends on RenderDoc.
// See docs/RENDERDOC_PIX.md for the capture workflow.
class RenderDoc {
public:
    static void init();
    static void startFrameCapture(); // call before a frame you want to inspect
    static void endFrameCapture();
    static void endFrame();          // per-frame hook (currently a no-op)
    static bool isAvailable();
};
} // namespace gc6
