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
    float yaw = 0.0f;      // facing, radians (0 = +z)
    float phase = 0.0f;    // walk cycle, advances with distance walked
    float moving = 0.0f;   // 0 standing .. 1 walking, 2 running (animation)
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
    // Pedestrians walk the pavements: corner to corner along a block, or
    // across the road on the zebra crossings. Offset from the road centre.
    static constexpr float kPavementOffset = 9.3f;
private:
    void nextWaypoint(Pedestrian& p);
    std::vector<Pedestrian> peds_;
    const City* city_ = nullptr;
};
} // namespace gc6
