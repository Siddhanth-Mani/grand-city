#pragma once
// Dynamic lights: the sun (directional, with a shadow map) plus up to 16
// point lights (streetlamps, headlights, police light bars). The World
// builds one LightRig per frame; the backends consume it. The null renderer
// just logs the light count.
#include "core/math.h"
#include <vector>
namespace gc6 {
struct DynamicLight {
    Vec3 position{0, 0, 0};
    Vec3 colour{1, 1, 1};
    float radius = 12.0f;    // metres of useful range
    float intensity = 1.0f;
    bool isHeadlight = false;
    bool isPolice = false;
};
struct ShadowMapConfig {
    int size = 2048;          // shadow map resolution (px)
    float orthoHalf = 120.0f; // metres covered around the camera target
    float nearPlane = 1.0f, farPlane = 300.0f;
    int pcfTaps = 9;          // 3x3 PCF kernel in the shader
};
struct LightRig {
    static constexpr size_t kMaxLights = 16;
    Vec3 sunDir{0, -1, 0}, sunColour{1, 1, 1};
    float sunIntensity = 1.0f;
    ShadowMapConfig shadow;
    std::vector<DynamicLight> lights;
    bool add(const DynamicLight& l) {
        if (lights.size() >= kMaxLights) return false;
        lights.push_back(l);
        return true;
    }
    void clear() { lights.clear(); }
};
} // namespace gc6
