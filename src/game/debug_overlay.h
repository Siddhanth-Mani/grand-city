#pragma once
// In-game debug overlay + profiler hookup: FPS, frame/physics/render times,
// draw calls, entity counts, wireframe toggle. The overlay is data only —
// the renderer draws it on Windows; headless runs print it to the log.
// tools/profiler.py summarises the profile.log this writes.
#include <string>
namespace gc6 {
struct DebugOverlay {
    bool enabled = false;
    bool wireframe = false;
    float fps = 0.0f;
    float frameMs = 0.0f;
    float physicsMs = 0.0f;
    float renderMs = 0.0f;
    size_t draws = 0;
    size_t entityCount = 0;
    size_t trafficCount = 0;
    size_t pedestrianCount = 0;
    std::string summary() const;
};
} // namespace gc6
