#pragma once
#include "renderer/renderer.h"
namespace gc6 {
class World;
// Everything the 2D overlay needs that lives in the application loop.
struct OverlayState {
    int menuSelection = 0;
    int garageSelection = 0;
    bool saveExists = false;
    float timeSeconds = 0.0f; // wall-clock, for toasts and blinking
};
// Draws the menus, HUD and minimap for the current game state on top of the
// 3D frame. Backends without an overlay (null renderer) ignore the calls.
void drawOverlay(IRenderer& renderer, World& world, const OverlayState& state);
} // namespace gc6
