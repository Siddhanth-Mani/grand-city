#include "game/traffic.h"
#include <random>
namespace gc6 {
void Traffic::init(const City& city, int count) {
    city_ = &city;
    graph_.build(city);
    cars_.clear();
    std::mt19937 rng(1234);
    for (int i = 0; i < count && i < static_cast<int>(city.roadNodes.size()); ++i) {
        TrafficCar car;
        VehicleParams p;
        p.engineForce = 6000.0f; p.topSpeed = 14.0f; p.massKg = 1300.0f;
        p.horsepower = 120.0f; p.peakTorqueNm = 180.0f;
        car.vehicle = Vehicle(p);
        Vec3 node = city.roadNodes[static_cast<size_t>(i * 7) % city.roadNodes.size()];
        car.vehicle.setPosition(node, static_cast<float>(rng() % 4) * 1.5708f);
        car.nodeIndex = static_cast<int>(i * 7) % static_cast<int>(city.roadNodes.size());
        car.laneOffset = (rng() % 2 == 0) ? -1.5f : 1.5f;
        pickNewDestination(car);
        cars_.push_back(std::move(car));
    }
}
void Traffic::pickNewDestination(TrafficCar& car) {
    if (!city_ || city_->roadNodes.empty()) return;
    Vec3 goal = city_->roadNodes[std::rand() % city_->roadNodes.size()];
    car.route = graph_.findPath(car.vehicle.position(), goal);
    car.routeIndex = 0;
    car.repathTimer = 8.0f + static_cast<float>(std::rand() % 8);
}
void Traffic::aiDecide(size_t index, float dt) {
    if (index >= cars_.size() || !city_) return;
    TrafficCar& car = cars_[index];
    car.repathTimer -= dt;
    if (car.repathTimer <= 0.0f || car.routeIndex >= car.route.size())
        pickNewDestination(car);
    // --- lane changing: if a slow car is ahead in our lane, move over ---
    Vec3 fwd{std::sin(car.vehicle.yaw()), 0, std::cos(car.vehicle.yaw())};
    Vec3 right{std::cos(car.vehicle.yaw()), 0, -std::sin(car.vehicle.yaw())};
    bool blocked = false;
    for (const auto& other : cars_) {
        if (&other == &car) continue;
        Vec3 d = other.vehicle.position() - car.vehicle.position();
        float along = dot(d, fwd);
        float side = dot(d, right);
        if (along > 2.0f && along < 14.0f && std::fabs(side - car.laneOffset) < 2.0f) blocked = true;
    }
    if (blocked) car.laneOffset = (car.laneOffset > 0.0f) ? -1.5f : 1.5f;
    // --- yielding at intersections: if another car is inside the crossing
    // ahead of us, wait instead of entering ---
    bool yield = false;
    if (car.routeIndex < car.route.size()) {
        Vec3 wp = car.route[car.routeIndex];
        for (const auto& other : cars_) {
            if (&other == &car) continue;
            if (length(other.vehicle.position() - wp) < 6.0f &&
                length(car.vehicle.position() - wp) < 12.0f) { yield = true; break; }
        }
    }
    (void)yield;
    // Store the decision on the car for physicsStep.
    car.targetSpeed = yield ? 0.0f : 10.0f;
}
void Traffic::physicsStep(size_t index, float dt, const std::vector<AABB>& solids) {
    if (index >= cars_.size() || !city_ || city_->roadNodes.empty()) return;
    TrafficCar& car = cars_[index];
    Vec3 target = car.routeIndex < car.route.size()
                      ? car.route[car.routeIndex]
                      : city_->roadNodes[static_cast<size_t>(car.nodeIndex)];
    // Aim at the waypoint offset into the current lane.
    Vec3 toTarget = target - car.vehicle.position();
    Vec3 fwd{std::sin(car.vehicle.yaw()), 0, std::cos(car.vehicle.yaw())};
    Vec3 right{std::cos(car.vehicle.yaw()), 0, -std::sin(car.vehicle.yaw())};
    Vec3 aim = target + right * car.laneOffset;
    Vec3 to = aim - car.vehicle.position();
    float dist = length(Vec3(to.x, 0, to.z));
    if (dist < 3.0f && car.routeIndex < car.route.size()) { ++car.routeIndex; return; }
    (void)toTarget;
    // Stop at red lights within 12 m.
    float throttle = 0.5f, brake = 0.0f;
    for (const auto& light : city_->lights) {
        if (!light.green && length(light.position - car.vehicle.position()) < 12.0f) { throttle = 0.0f; brake = 1.0f; }
    }
    // Brake for the car ahead.
    for (const auto& other : cars_) {
        if (&other == &car) continue;
        Vec3 d = other.vehicle.position() - car.vehicle.position();
        if (length(d) < 8.0f && dot(normalize(d), fwd) > 0.7f) { throttle = 0.0f; brake = 1.0f; }
    }
    if (car.targetSpeed <= 0.0f) { throttle = 0.0f; brake = 1.0f; } // yielding
    float desiredYaw = std::atan2(to.x, to.z);
    float yawDiff = desiredYaw - car.vehicle.yaw();
    while (yawDiff > 3.14159f) yawDiff -= 6.28318f;
    while (yawDiff < -3.14159f) yawDiff += 6.28318f;
    car.vehicle.update(dt, throttle, clampf(yawDiff * 2.0f, -1.0f, 1.0f), brake, solids);
}
void Traffic::update(float dt, const std::vector<AABB>& solids, const Vec3&) {
    for (size_t i = 0; i < cars_.size(); ++i) aiDecide(i, dt);
    for (size_t i = 0; i < cars_.size(); ++i) physicsStep(i, dt, solids);
}
} // namespace gc6
