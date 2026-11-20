#pragma once
// Small ECS: entities are ids; components live in per-type sparse maps.
// Traffic cars and pedestrians are stored here — systems iterate components
// instead of ad-hoc vectors. No archetypes, no reflection: deliberately
// small, readable, and fast enough for a few hundred entities.
#include "core/math.h"
#include "physics/vehicle.h"
#include <cstdint>
#include <unordered_map>
#include <vector>
namespace gc6 {
using Entity = uint32_t;
constexpr Entity kNullEntity = 0;
struct Transform {
    Vec3 position{0, 0, 0};
    float yaw = 0.0f;
};
struct VehicleComp {
    Vehicle vehicle;
    int nodeIndex = 0;
    float targetSpeed = 10.0f;
    float steerDecision = 0.0f; // computed by the AI system, applied by physics
    float throttleDecision = 0.0f;
    float brakeDecision = 0.0f;
    bool isPolice = false;
};
struct PedComp {
    Vec3 waypoint{0, 0, 0};
    float speed = 1.4f;
    bool fleeing = false;
};
struct RenderComp {
    Vec3 colour{0.8f, 0.8f, 0.8f};
    int meshKind = 0; // 0 = car, 1 = person, 2 = police car
};
struct AIComp {
    std::vector<Vec3> path;   // A* path waypoints (traffic / police)
    size_t pathIndex = 0;
    float repathTimer = 0.0f;
    Vec3 destination{0, 0, 0};
    bool hasDestination = false;
};
class Registry {
public:
    Entity create();
    void destroy(Entity e);
    size_t entityCount() const { return alive_.size(); }
    template <typename T> void add(Entity e, T comp) { storage<T>()[e] = std::move(comp); }
    template <typename T> bool has(Entity e) const {
        const auto& m = storage<T>();
        return m.find(e) != m.end();
    }
    template <typename T> T* get(Entity e) {
        auto& m = storage<T>();
        auto it = m.find(e);
        return it == m.end() ? nullptr : &it->second;
    }
    template <typename T> const T* get(Entity e) const {
        const auto& m = storage<T>();
        auto it = m.find(e);
        return it == m.end() ? nullptr : &it->second;
    }
    template <typename T> void remove(Entity e) { storage<T>().erase(e); }
    // All entities that have every listed component.
    template <typename... Ts> std::vector<Entity> view() const {
        std::vector<Entity> out;
        for (Entity e : alive_)
            if ((has<Ts>(e) && ...)) out.push_back(e);
        return out;
    }
    const std::vector<Entity>& alive() const { return alive_; }
private:
    template <typename T> std::unordered_map<Entity, T>& storage();
    template <typename T> const std::unordered_map<Entity, T>& storage() const;
    std::vector<Entity> alive_;
    Entity nextId_ = 1;
    std::unordered_map<Entity, Transform> transforms_;
    std::unordered_map<Entity, VehicleComp> vehicles_;
    std::unordered_map<Entity, PedComp> peds_;
    std::unordered_map<Entity, RenderComp> renders_;
    std::unordered_map<Entity, AIComp> ais_;
};
// Explicit storage specialisations live in ecs.cpp.
} // namespace gc6
