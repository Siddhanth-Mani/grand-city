#pragma once
// PBR materials: albedo, metalness, roughness, normal strength, emissive.
// Defined in data/materials.json and loaded by loadMaterials(). The
// Cook-Torrance BRDF itself lives in shaders/lit_pbr.hlsl (and the GLSL
// equivalent); this file is the CPU-side data + loader.
#include "core/math.h"
#include <string>
#include <vector>
namespace gc6 {
struct Material {
    std::string name = "default";
    Vec3 albedo{0.8f, 0.8f, 0.8f};
    float metalness = 0.0f;   // 0 = dielectric, 1 = metal
    float roughness = 0.6f;   // 0 = mirror, 1 = matte
    float normalStrength = 1.0f;
    Vec3 emissive{0, 0, 0};   // glowing parts (lamps, brake lights)
    bool valid() const {
        return metalness >= 0.0f && metalness <= 1.0f && roughness >= 0.0f && roughness <= 1.0f;
    }
};
// Minimal JSON field scan (no JSON dependency in the engine): finds
// "key": number / "key": [r, g, b] inside each {...} object of an array.
std::vector<Material> loadMaterials(const std::string& path);
const Material* findMaterial(const std::vector<Material>& mats, const std::string& name);
} // namespace gc6
