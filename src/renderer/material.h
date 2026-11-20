#pragma once
// PBR materials: albedo, metalness, roughness, normal strength, emissive.
// Defined in data/materials.json and loaded by loadMaterials(). The
// Cook-Torrance BRDF itself lives in shaders/lit_pbr.hlsl (and the GLSL
// equivalent); this file is the CPU-side data + loader.
#include "core/math.h"
#include <string>
#include <vector>
namespace gc6 {
// Shading models the backends understand. Standard is plain PBR; the rest
// add procedural detail in the pixel shader so the city needs no textures.
enum Shading : int {
    kShadeStandard = 0,
    kShadeFacade = 1,   // building walls: floors, windows (lit at night), roofs
    kShadeGlass = 2,    // car and shop glass: dark, sharp sky reflections
    kShadeGround = 3,   // roads, kerbs, pavements, beach and sea
    kShadeFoliage = 4,  // tree canopies
    kShadeCarPaint = 5, // base coat + clear coat
    kShadeEmissive = 6, // lamps and lights: colour only, no lighting
};
struct Material {
    std::string name = "default";
    Vec3 albedo{0.8f, 0.8f, 0.8f};
    float metalness = 0.0f;   // 0 = dielectric, 1 = metal
    float roughness = 0.6f;   // 0 = mirror, 1 = matte
    float normalStrength = 1.0f;
    Vec3 emissive{0, 0, 0};   // glowing parts (lamps, brake lights)
    // How the backend shades the surface (see Shading). variant picks a
    // style inside a shading model, e.g. which facade a building uses.
    int shading = 0;
    float variant = 0.0f;
    bool valid() const {
        return metalness >= 0.0f && metalness <= 1.0f && roughness >= 0.0f && roughness <= 1.0f;
    }
};
// Minimal JSON field scan (no JSON dependency in the engine): finds
// "key": number / "key": [r, g, b] inside each {...} object of an array.
std::vector<Material> loadMaterials(const std::string& path);
const Material* findMaterial(const std::vector<Material>& mats, const std::string& name);
} // namespace gc6
