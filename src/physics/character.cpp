#include "physics/character.h"
#include <cmath>
namespace gc6 {
void Character::update(float dt, const Vec3& moveDir, bool run, bool jump,
                       const std::vector<AABB>& solids) {
    float speed = run ? 7.0f : 3.5f;
    Vec3 wish = moveDir * speed;
    velocity_.x = wish.x;
    velocity_.z = wish.z;
    if (!onGround_) velocity_.y -= 22.0f * dt; // gravity
    if (jump && onGround_) { velocity_.y = 7.5f; onGround_ = false; }
    position_ += velocity_ * dt;
    if (position_.y <= 0.0f) { position_.y = 0.0f; velocity_.y = 0.0f; onGround_ = true; }
    if (wish.x != 0.0f || wish.z != 0.0f) yaw_ = std::atan2(wish.x, wish.z);
    AABB box{position_ + Vec3(-0.3f, 0, -0.3f), position_ + Vec3(0.3f, 1.8f, 0.3f)};
    for (const auto& solid : solids) {
        AABB moved = box;
        if (resolveAABB(moved, solid)) {
            position_ += moved.min - box.min;
            box = moved;
        }
    }
}
} // namespace gc6
