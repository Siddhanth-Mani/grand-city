#include "game/pedestrian.h"
#include <cmath>
#include <cstdlib>
#include <random>
namespace gc6 {
void Pedestrians::init(const City& city, int count) {
    city_ = &city;
    peds_.clear();
    std::mt19937 rng(777);
    const float o = kPavementOffset;
    for (int i = 0; i < count && !city.roadNodes.empty(); ++i) {
        Pedestrian p;
        Vec3 node = city.roadNodes[rng() % city.roadNodes.size()];
        if (node.z < -city.size / 2 + 40.0f) node.z += 40.0f;          // not on the beach
        float sx = (rng() % 2) ? o : -o, sz = (rng() % 2) ? o : -o;
        float along = static_cast<float>(rng() % 20);                   // part way along a block
        p.position = node + Vec3(sx, 0, sz) + ((rng() % 2) ? Vec3(along * (sx > 0 ? 1.f : -1.f), 0, 0)
                                                             : Vec3(0, 0, along * (sz > 0 ? 1.f : -1.f)));
        p.speed = 1.15f + static_cast<float>(rng() % 50) / 100.0f;
        p.phase = static_cast<float>(rng() % 628) / 100.0f;
        p.waypoint = p.position;
        nextWaypoint(p);
        peds_.push_back(p);
    }
}
// Next corner: snap to the pavement corner we are at, then walk to a
// neighbouring corner - usually along the block, sometimes over a crossing.
void Pedestrians::nextWaypoint(Pedestrian& p) {
    if (!city_) return;
    const float s = city_->roadSpacing(), o = kPavementOffset, half = city_->size / 2;
    Vec3 node(std::round(p.position.x / s) * s, 0, std::round(p.position.z / s) * s);
    Vec3 off = p.position - node;
    float sx = off.x >= 0 ? 1.0f : -1.0f, sz = off.z >= 0 ? 1.0f : -1.0f;
    Vec3 corner = node + Vec3(sx * o, 0, sz * o);
    if (length(Vec3(p.position.x - corner.x, 0, p.position.z - corner.z)) > 1.5f) {
        p.waypoint = corner;  // first get to the corner of this pavement
        return;
    }
    bool alongX = (std::rand() % 2) == 0;
    bool cross = (std::rand() % 4) == 0;
    float dir = alongX ? sx : sz;
    float dist = s - 2.0f * o;               // along the block
    if (cross) { dir = -dir; dist = 2.0f * o; }
    Vec3 step = alongX ? Vec3(dir * dist, 0, 0) : Vec3(0, 0, dir * dist);
    Vec3 target = corner + step;
    if (std::fabs(target.x) > half - 2.0f || target.z > half - 2.0f || target.z < -half + 42.0f)
        target = corner - step;
    p.waypoint = target;
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
            dir = normalize(Vec3(away.x, 0, away.z) + Vec3(1e-4f, 0, 0));
            p.position += dir * 4.5f * dt;
            p.yaw = std::atan2(dir.x, dir.z);
            p.phase += 4.5f * dt * 3.0f;
            p.moving = 2.0f;
            p.waypoint = p.position;
            continue;
        }
        Vec3 to = p.waypoint - p.position;
        if (length(Vec3(to.x, 0, to.z)) < 0.6f && city_ && !city_->roadNodes.empty()) {
            nextWaypoint(p);
            continue;
        }
        dir = normalize(Vec3(to.x, 0, to.z));
        // Avoid cars in the way: blend the avoidance step into the walk.
        Vec3 avoid = avoidCars(p, carPositions, carForward);
        if (length(avoid) > 0.01f) dir = normalize(dir + avoid * 1.5f);
        p.position += dir * p.speed * dt;
        // Turn smoothly towards the walking direction.
        float want = std::atan2(dir.x, dir.z);
        float diff = std::remainder(want - p.yaw, 6.2831853f);
        p.yaw += diff * std::min(1.0f, dt * 8.0f);
        p.phase += p.speed * dt * 3.6f;
        p.moving = 1.0f;
    }
}
} // namespace gc6
