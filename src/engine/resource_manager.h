#pragma once
#include "core/math.h"
#include <string>
#include <unordered_map>
#include <vector>
namespace gc6 {
struct MeshData {
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> uvs;
    std::vector<uint32_t> indices;
};
struct TextureData { int width = 0, height = 0, channels = 4; std::vector<unsigned char> pixels; };
class ResourceManager {
public:
    // Procedural meshes (no external assets needed to boot the game).
    const MeshData& box(float w, float h, float d);
    const MeshData& carBody();      // low-poly car hull (generic sedan)
    // Detailed per-class car bodies: distinct silhouettes per archetype
    // (supercar low/wide + spoiler, muscle long hood, SUV tall, pickup bed).
    // className: "supercar", "sports", "muscle", "sedan", "suv", "pickup".
    const MeshData& carBodyFor(const std::string& carClass);
    const MeshData& wheel();        // cylinder-ish wheel
    const MeshData& person();       // low-poly pedestrian
    const MeshData& groundPlane(float size);
    // Car detail parts, drawn separately so they can move/light up:
    const MeshData& headlight();    // small emissive box (front lights)
    const MeshData& taillight();    // small emissive box (rear lights)
    const MeshData& spoiler();      // rear wing
    const MeshData& mirror();       // side mirror
    const MeshData& rim();          // wheel rim (spins with the wheel)
    const MeshData& dashboard();    // cockpit interior: dash + wheel column
    const MeshData& steeringWheel();// cockpit: wheel that turns with steering
    TextureData checkerboard(int size, unsigned int c0, unsigned int c1);
    void clear();
private:
    std::unordered_map<std::string, MeshData> meshes_;
};
} // namespace gc6
