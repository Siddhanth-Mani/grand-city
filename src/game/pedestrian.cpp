#include "game/pedestrian.h"
#include <cstdlib>
#include <random>
namespace gc6 {
void Pedestrians::init(const City& city, int count) {
    city_ = &city;
    peds_.clear();
    std::mt19937 rng(777);
    for (int i = 0; i < count && !city.roadNodes.empty(); ++i) {
        Pedestrian p;
        Vec3 node = city.roadNodes[rng() % city.roadNodes.size()];
        p.position = node + Vec3(6, 0, 6);
        p.waypoint = city.roadNodes[rng() % city.roadNodes.size()] + Vec3(6, 0, 6);
        peds_.push_back(p);
    }
}
// Steering avoidance: a pedestrian about to cross a car's path steps aside
// (perpendicular to the car's motion) instead of walking into it.
static Vec3 avoidCars(const Pedestrian& p, const std::vector<Vec3>& carPositions,
                      const std::vector<Vec3>& carForward) {
    Vec3 steer{0, 0, 0};
    for (size_t i = 0; i < carPositions.size(); ++i) {
        Vec3 to = p.position - carPositions[i];
        float dist = length(Vec3(to.x, 0, to.z));
        if (dist > 8.0f) continue;
        Vec3 fwd = carForward[i];
        float ahead = dot(Vec3(-to.x, 0, -to.z), fwd); // car heading toward us?
        if (ahead > 0.0f) {
            // Step sideways, away from the car's line.
            Vec3 side{-fwd.z, 0, fwd.x};
            float s = dot(to, side) >= 0.0f ? 1.0f : -1.0f;
            steer += side * s * (8.0f - dist) / 8.0f;
        }
    }
    return steer;
}
void Pedestrians::update(float dt, const Vec3& playerPos, float chaos) {
    update(dt, playerPos, chaos, {}, {});
}
void Pedestrians::update(float dt, const Vec3& playerPos, float chaos,
                         const std::vector<Vec3>& carPositions,
                         const std::vector<Vec3>& carForward) {
    for (auto& p : peds_) {
        Vec3 away = p.position - playerPos;
        float dist = length(away);
        p.fleeing = chaos > 0.4f && dist < 25.0f;
        Vec3 dir;
        if (p.fleeing) {
            dir = normalize(Vec3(away.x, 0, away.z));
            p.position += dir * 4.5f * dt;
            continue;
        }
        Vec3 to = p.waypoint - p.position;
        if (length(Vec3(to.x, 0, to.z)) < 2.0f && city_ && !city_->roadNodes.empty()) {
            p.waypoint = city_->roadNodes[std::rand() % city_->roadNodes.size()] + Vec3(6, 0, 6);
            continue;
        }
        dir = normalize(Vec3(to.x, 0, to.z));
        // Avoid cars in the way: blend the avoidance step into the walk.
        Vec3 avoid = avoidCars(p, carPositions, carForward);
        if (length(avoid) > 0.01f) dir = normalize(dir + avoid * 1.5f);
        p.position += dir * p.speed * dt;
    }
}
} // namespace gc6
