#pragma once
#include "core/math.h"
#include "game/city.h"
#include "game/pathfinding.h"
#include "physics/vehicle.h"
#include <vector>
namespace gc6 {
// Traffic cars: route with A* on the road graph, stop at red lights, brake
// for the car ahead, change lanes to overtake slow cars, and yield at
// intersections when another car is already crossing.
struct TrafficCar {
    Vehicle vehicle;
    int nodeIndex = 0;
    float targetSpeed = 10.0f;
    std::vector<Vec3> route;   // A* waypoints to the current destination
    size_t routeIndex = 0;
    float laneOffset = 0.0f;   // -1.5 = left lane, +1.5 = right lane
    float repathTimer = 0.0f;
};
class Traffic {
public:
    void init(const City& city, int count);
    void update(float dt, const std::vector<AABB>& solids, const Vec3& playerPos);
    const std::vector<TrafficCar>& cars() const { return cars_; }
    // Jobs split: the AI system computes steering decisions for one car;
    // the physics system applies them. World runs the AI part in parallel.
    void aiDecide(size_t index, float dt);
    void physicsStep(size_t index, float dt, const std::vector<AABB>& solids);
private:
    std::vector<TrafficCar> cars_;
    const City* city_ = nullptr;
    RoadGraph graph_;
    void pickNewDestination(TrafficCar& car);
};
} // namespace gc6
