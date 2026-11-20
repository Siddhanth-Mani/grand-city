#pragma once
#include "core/math.h"
#include "physics/collision.h"
#include <string>
#include <vector>
namespace gc6 {
struct Building { AABB bounds; float height = 10.0f; Vec3 colour{0.5f, 0.5f, 0.55f}; };
struct TrafficLight { Vec3 position; float timer = 0.0f; bool green = true; };
struct City {
    std::vector<Building> buildings;
    std::vector<AABB> solids;      // building bounds, for collision
    std::vector<Vec3> lamps;
    std::vector<TrafficLight> lights;
    std::vector<Vec3> roadNodes;   // grid intersections cars drive between
    float size = 400.0f;           // city is size x size metres, beach at -Z edge
    // Deterministic generator: same seed -> same city.
    void generate(int seed);
    bool isOnRoad(const Vec3& p) const;
    float roadSpacing() const { return 40.0f; }
};
} // namespace gc6
