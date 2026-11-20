#pragma once
#include "core/math.h"
#include "physics/collision.h"
#include <vector>
namespace gc6 {
// On-foot controller: walk/run, gravity, jump, ground + building collision.
class Character {
public:
    void setPosition(const Vec3& p) { position_ = p; }
    void update(float dt, const Vec3& moveDir, bool run, bool jump,
                const std::vector<AABB>& solids);
    Vec3 position() const { return position_; }
    float yaw() const { return yaw_; }
    bool onGround() const { return onGround_; }
private:
    Vec3 position_{0, 0, 0}, velocity_{0, 0, 0};
    float yaw_ = 0.0f;
    bool onGround_ = true;
};
} // namespace gc6
