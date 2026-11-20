#pragma once
#include "core/math.h"
#include "physics/collision.h"
#include <string>
#include <vector>
namespace gc6 {
// style: 0 concrete offices, 1 glass tower, 2 brick, 3 stone (see the facade
// shader); podium > 0 gives a tall building a wider base and a set-back tower.
struct Building {
    AABB bounds;
    float height = 10.0f;
    Vec3 colour{0.5f, 0.5f, 0.55f};
    int style = 0;
    float podium = 0.0f;
    float seed = 0.0f;   // 0..1, varies rooftop details and lit windows
};
struct StreetLamp { Vec3 position; float yaw = 0.0f; };   // arm points along yaw
struct Tree { Vec3 position; int variant = 0; float scale = 1.0f; };
struct TrafficLight { Vec3 position; float timer = 0.0f; bool green = true; };
struct City {
    std::vector<Building> buildings;
    std::vector<AABB> solids;      // building bounds, for collision
    std::vector<Vec3> lamps;       // lamp head positions (lights)
    std::vector<StreetLamp> streetLamps;
    std::vector<Tree> trees;
    std::vector<TrafficLight> lights;
    std::vector<Vec3> roadNodes;   // grid intersections cars drive between
    float size = 400.0f;           // city is size x size metres, beach at -Z edge
    // Deterministic generator: same seed -> same city.
    void generate(int seed);
    bool isOnRoad(const Vec3& p) const;
    // Height of the walking surface: pavements are a kerb above the road.
    float groundHeight(const Vec3& p) const;
    static constexpr float kKerb = 0.12f;
    float roadSpacing() const { return 40.0f; }
};
} // namespace gc6
