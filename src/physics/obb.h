#pragma once
// Oriented bounding boxes (yaw-only, for cars) + swept collision.
// Cars are OBBs in the XZ plane; SAT on the two cars' local X/Z axes gives
// an exact overlap test, and swept movement (sub-stepping) stops fast cars
// tunnelling through thin walls. See docs/PHYSICS.md.
#include "core/math.h"
#include "physics/collision.h"
namespace gc6 {
struct OBB {
    Vec3 centre{0, 0, 0};
    Vec3 halfExtents{1.1f, 0.7f, 2.3f};
    float yaw = 0.0f; // rotation about Y
};
Vec3 obbAxisX(const OBB& b); // local +X in world space
Vec3 obbAxisZ(const OBB& b); // local +Z in world space
bool obbOverlap(const OBB& a, const OBB& b);
// Minimum translation vector that separates a from b (zero if no overlap).
Vec3 obbSeparation(const OBB& a, const OBB& b);
// Move `box` along `delta` in sub-steps no larger than maxStep so it cannot
// tunnel through `solid`. Returns the position reached and sets hit=true
// when a step collided.
Vec3 sweptMoveAABB(const AABB& box, Vec3 from, Vec3 delta, const AABB& solid,
                   float maxStep, bool& hit);
} // namespace gc6
