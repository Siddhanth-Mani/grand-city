#pragma once
#include "core/config.h"
#include "core/input.h"
#include "game/world.h"
#include <memory>
namespace gc6 {
class Application {
public:
    Application();
    // Returns process exit code. --headless runs N frames and exits (CI/tests).
    int run(int argc, char** argv);
    World& world() { return world_; }
private:
    Config config_;
    InputState input_;
    World world_;
    bool headless_ = false;
    int headlessFrames_ = 600;
    // --shot FILE: scripted capture for screenshots/trailers. Starts a new
    // game, optionally drives (--drive S, --steer -1..1, --steer-at S), picks a
    // camera (--camera 0..2 or --photo YAW PITCH DIST), then saves one frame.
    struct ShotPlan {
        std::string file;
        float hour = 10.0f, drive = 0.0f, steer = 0.0f, steerAt = 0.0f, settle = 0.6f, wait = 0.0f;
        int camera = 0;
        bool photo = false, hud = true;
        float photoYaw = 0.0f, photoPitch = 0.3f, photoDist = 9.0f;
        std::string weather;
    } shot_;
};
} // namespace gc6
