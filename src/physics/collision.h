#pragma once
#include "core/math.h"
namespace gc6 {
struct AABB { Vec3 min{0, 0, 0}, max{0, 0, 0}; };
inline bool aabbOverlap(const AABB& a, const AABB& b) {
    return a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y && a.max.y >= b.min.y &&
           a.min.z <= b.max.z && a.max.z >= b.min.z;
}
// Push `box` out of `solid` along the smallest penetration axis. Returns true on overlap.
bool resolveAABB(AABB& box, const AABB& solid);
// Sphere vs AABB test (pedestrians, wheels).
bool sphereHitsAABB(const Vec3& centre, float radius, const AABB& box);
} // namespace gc6
