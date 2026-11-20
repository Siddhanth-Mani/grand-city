#pragma once
#include "core/math.h"
#include "game/city.h"
#include <vector>
namespace gc6 {
struct Pedestrian {
    Vec3 position{0, 0, 0};
    Vec3 waypoint{0, 0, 0};
    float speed = 1.4f;
    bool fleeing = false;
};
class Pedestrians {
public:
    void init(const City& city, int count);
    // chaos 0..1: high chaos makes nearby pedestrians flee from the player.
    void update(float dt, const Vec3& playerPos, float chaos);
    // With car positions + forward vectors: pedestrians step aside from cars
    // about to cross their path instead of walking into them.
    void update(float dt, const Vec3& playerPos, float chaos,
                const std::vector<Vec3>& carPositions,
                const std::vector<Vec3>& carForward);
    const std::vector<Pedestrian>& all() const { return peds_; }
private:
    std::vector<Pedestrian> peds_;
    const City* city_ = nullptr;
};
} // namespace gc6
