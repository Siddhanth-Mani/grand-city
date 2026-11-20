#include "physics/obb.h"
#include <algorithm>
#include <cmath>
namespace gc6 {
Vec3 obbAxisX(const OBB& b) { return Vec3(std::cos(b.yaw), 0, -std::sin(b.yaw)); }
Vec3 obbAxisZ(const OBB& b) { return Vec3(std::sin(b.yaw), 0, std::cos(b.yaw)); }
namespace {
// Project an OBB onto an axis, returning [min, max].
void projectOBB(const OBB& b, const Vec3& axis, float& outMin, float& outMax) {
    Vec3 ax = obbAxisX(b), az = obbAxisZ(b);
    float r = b.halfExtents.x * std::fabs(dot(axis, ax)) + b.halfExtents.z * std::fabs(dot(axis, az));
    float c = dot(axis, b.centre);
    outMin = c - r; outMax = c + r;
}
} // namespace
bool obbOverlap(const OBB& a, const OBB& b) {
    // Y overlap first (cheap), then SAT on the 4 XZ axes.
    if (a.centre.y + a.halfExtents.y < b.centre.y - b.halfExtents.y ||
        b.centre.y + b.halfExtents.y < a.centre.y - a.halfExtents.y) return false;
    Vec3 axes[4] = {obbAxisX(a), obbAxisZ(a), obbAxisX(b), obbAxisZ(b)};
    for (const Vec3& axis : axes) {
        float aMin, aMax, bMin, bMax;
        projectOBB(a, axis, aMin, aMax);
        projectOBB(b, axis, bMin, bMax);
        if (aMax < bMin || bMax < aMin) return false;
    }
    return true;
}
Vec3 obbSeparation(const OBB& a, const OBB& b) {
    if (!obbOverlap(a, b)) return Vec3(0, 0, 0);
    Vec3 axes[4] = {obbAxisX(a), obbAxisZ(a), obbAxisX(b), obbAxisZ(b)};
    float bestOverlap = 1e9f;
    Vec3 bestAxis{1, 0, 0};
    for (const Vec3& axis : axes) {
        float aMin, aMax, bMin, bMax;
        projectOBB(a, axis, aMin, aMax);
        projectOBB(b, axis, bMin, bMax);
        float overlap = std::min(aMax - bMin, bMax - aMin);
        if (overlap < bestOverlap) { bestOverlap = overlap; bestAxis = axis; }
    }
    // Push a away from b along the smallest-penetration axis.
    float dir = dot(b.centre - a.centre, bestAxis) > 0.0f ? -1.0f : 1.0f;
    return bestAxis * (bestOverlap * dir);
}
Vec3 sweptMoveAABB(const AABB& box, Vec3 from, Vec3 delta, const AABB& solid,
                   float maxStep, bool& hit) {
    hit = false;
    Vec3 half = (box.max - box.min) * 0.5f;
    Vec3 centre = (box.min + box.max) * 0.5f + from;
    float dist = length(delta);
    int steps = std::max(1, static_cast<int>(std::ceil(dist / maxStep)));
    Vec3 step = delta / static_cast<float>(steps);
    Vec3 pos = centre;
    for (int i = 0; i < steps; ++i) {
        Vec3 next = pos + step;
        AABB test{next - half, next + half};
        if (aabbOverlap(test, solid)) { hit = true; break; }
        pos = next;
    }
    return pos;
}
} // namespace gc6
