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

// Proportions of one car body style. z is forward, y up, the origin is on
// the ground between the axles. World::drawCar uses the same numbers to place
// wheels, lights and the police light bar.
struct CarShape {
    float length = 4.75f, width = 1.86f;
    float wheelBase = 2.85f, axleOffset = 0.0f, wheelRadius = 0.34f;
    float sill = 0.30f;                       // bottom of the doors
    float noseBottom = 0.32f, noseTop = 0.68f, hoodFront = 0.80f;
    float beltY = 0.98f, roofY = 1.45f;       // window line and roof
    float zA = 0.95f, zW = 0.05f;             // windscreen base / top
    float zR = -1.0f, zC = -1.70f;            // rear glass top / base
    float deckY = 0.98f, tailTop = 0.95f;     // boot lid and tail
    bool bed = false;                         // pickup load bed
    float frontAxleZ() const { return wheelBase * 0.5f + axleOffset; }
    float rearAxleZ() const { return -wheelBase * 0.5f + axleOffset; }
    float wheelX() const { return width * 0.5f - 0.17f; }
};

class ResourceManager {
public:
    // Procedural meshes (no external assets needed to boot the game).
    const MeshData& box(float w, float h, float d);
    const MeshData& carBody();      // generic sedan body
    // Car bodies are lofted from a side profile with wheel arches, bevelled
    // edges, a tapered plan and a separate glasshouse, per body style:
    // "sedan", "hatch", "sports", "supercar", "muscle", "suv", "pickup", "van".
    static const CarShape& carShape(const std::string& carClass);
    const MeshData& carBodyFor(const std::string& carClass);    // painted panels + roof
    const MeshData& carGlassFor(const std::string& carClass);   // windows
    const MeshData& carTrimFor(const std::string& carClass);    // grille, skirts, mirrors, diffuser
    const MeshData& carPlatesFor(const std::string& carClass);  // number plates
    const MeshData& carHeadlightsFor(const std::string& carClass);
    const MeshData& carTaillightsFor(const std::string& carClass);
    const MeshData& tyre(float radius);   // tyre + dark inner wheel, axis along +x (outer face)
    const MeshData& rimFor(float radius); // five-spoke rim on the outer face
    const MeshData& wheel();        // tyre of the default size
    const MeshData& person();       // whole low-poly pedestrian (one mesh)
    // Articulated pedestrian parts. Limbs hang from their pivot at y = 0.
    const MeshData& personTorso();  // from the hips (y = 0) up to the shoulders
    const MeshData& personHead();   // neck + head, pivot at the base of the neck
    const MeshData& personHair();
    const MeshData& personLimb(float length, float radius);
    const MeshData& personShoe();
    const MeshData& personHand();
    // Street furniture and nature.
    const MeshData& treeTrunk();
    const MeshData& treeCanopy(int variant);
    const MeshData& lampPost();     // pole + arm reaching along +x
    const MeshData& lampHead();     // the luminaire at the end of the arm
    const MeshData& signalPole();   // traffic signal pole + mast arm along +x
    const MeshData& signalHead();   // housing (lamps drawn separately)
    const MeshData& sphere(float radius);
    const MeshData& cylinder(float radius, float height); // y = 0 .. height
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
    template <class F> const MeshData& cached(const std::string& key, F build) {
        auto it = meshes_.find(key);
        if (it != meshes_.end()) return it->second;
        return meshes_.emplace(key, build()).first->second;
    }
};
} // namespace gc6
