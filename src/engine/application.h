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
};
} // namespace gc6
