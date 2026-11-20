#include "game/wanted.h"
namespace gc6 {
void Police::init(const City& city) {
    graph_.build(city);
    graphReady_ = true;
}
std::vector<Vec3> Police::roadblockPositions() const {
    std::vector<Vec3> out;
    for (const auto& c : cars_) if (c.roadblock) out.push_back(c.position);
    return out;
}
void Police::update(float dt, const Vec3& playerPos, int stars, const std::vector<AABB>&) {
    size_t want = static_cast<size_t>(stars); // one police car per star, capped
    if (want > 5) want = 5;
    while (cars_.size() < want) {
        PoliceCar c;
        c.position = playerPos + Vec3(30.0f + cars_.size() * 8.0f, 0, -25.0f);
        cars_.push_back(c);
    }
    while (cars_.size() > want) cars_.pop_back();
    bool roadblockMode = stars >= 3;
    for (size_t i = 0; i < cars_.size(); ++i) {
        PoliceCar& car = cars_[i];
        car.repathTimer -= dt;
        // At wanted 3+, the last police car sets up a roadblock ahead of the
        // player instead of chasing: it drives to a point on the road in
        // front of the player and parks across it.
        car.roadblock = roadblockMode && i == cars_.size() - 1 && cars_.size() >= 3;
        Vec3 goal = playerPos;
        if (car.roadblock) {
            // Ahead of the player along their direction of travel: pick the
            // nearest road node in front of the player.
            goal = playerPos + Vec3(std::sin(0.0f), 0, std::cos(0.0f)) * 60.0f;
            if (graphReady_) {
                int n = graph_.nearestNode(goal);
                if (n >= 0) goal = graph_.nodes()[static_cast<size_t>(n)];
            }
        }
        if (graphReady_ && car.repathTimer <= 0.0f) {
            car.route = graph_.findPath(car.position, goal);
            car.routeIndex = 0;
            car.repathTimer = 2.0f; // re-route often: the player keeps moving
        }
        Vec3 target = playerPos;
        if (!car.route.empty() && car.routeIndex < car.route.size()) {
            target = car.route[car.routeIndex];
            if (length(Vec3(target - car.position)) < 4.0f) ++car.routeIndex;
        }
        Vec3 to = target - car.position;
        float dist = length(Vec3(to.x, 0, to.z));
        if (dist < 1.0f) continue;
        float desiredYaw = std::atan2(to.x, to.z);
        float diff = desiredYaw - car.yaw;
        while (diff > 3.14159f) diff -= 6.28318f;
        while (diff < -3.14159f) diff += 6.28318f;
        car.yaw += clampf(diff, -1.5f * dt, 1.5f * dt);
        float targetSpeed = car.roadblock ? (dist > 8.0f ? 22.0f : 0.0f)
                                          : (dist > 40.0f ? 22.0f : 16.0f);
        car.speed += (targetSpeed - car.speed) * std::min(1.0f, dt * 1.5f);
        car.position += Vec3(std::sin(car.yaw), 0, std::cos(car.yaw)) * car.speed * dt;
    }
}
} // namespace gc6
