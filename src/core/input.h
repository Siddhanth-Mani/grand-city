#pragma once
#include <array>
#include <cstddef>
namespace gc6 {
enum Key : int {
    Key_W = 0, Key_A, Key_S, Key_D, Key_E, Key_Space, Key_Shift, Key_Escape,
    Key_Up, Key_Down, Key_Left, Key_Right, Key_F, Key_R, Key_M, Key_P, Key_Count
};
// Platform-filled input state, polled once per frame by the game.
struct InputState {
    std::array<bool, Key_Count> down{};
    std::array<bool, Key_Count> pressed{}; // edge-triggered, cleared each frame
    float mouseDX = 0.0f, mouseDY = 0.0f;
    bool isDown(Key k) const { return down[static_cast<size_t>(k)]; }
    bool wasPressed(Key k) const { return pressed[static_cast<size_t>(k)]; }
    void endFrame() { pressed.fill(false); mouseDX = mouseDY = 0.0f; }
};
} // namespace gc6
