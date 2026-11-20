#include "core/ecs.h"
#include <algorithm>
namespace gc6 {
template <> std::unordered_map<Entity, Transform>& Registry::storage<Transform>() { return transforms_; }
template <> const std::unordered_map<Entity, Transform>& Registry::storage<Transform>() const { return transforms_; }
template <> std::unordered_map<Entity, VehicleComp>& Registry::storage<VehicleComp>() { return vehicles_; }
template <> const std::unordered_map<Entity, VehicleComp>& Registry::storage<VehicleComp>() const { return vehicles_; }
template <> std::unordered_map<Entity, PedComp>& Registry::storage<PedComp>() { return peds_; }
template <> const std::unordered_map<Entity, PedComp>& Registry::storage<PedComp>() const { return peds_; }
template <> std::unordered_map<Entity, RenderComp>& Registry::storage<RenderComp>() { return renders_; }
template <> const std::unordered_map<Entity, RenderComp>& Registry::storage<RenderComp>() const { return renders_; }
template <> std::unordered_map<Entity, AIComp>& Registry::storage<AIComp>() { return ais_; }
template <> const std::unordered_map<Entity, AIComp>& Registry::storage<AIComp>() const { return ais_; }
Entity Registry::create() {
    Entity e = nextId_++;
    alive_.push_back(e);
    return e;
}
void Registry::destroy(Entity e) {
    transforms_.erase(e); vehicles_.erase(e); peds_.erase(e); renders_.erase(e); ais_.erase(e);
    alive_.erase(std::remove(alive_.begin(), alive_.end(), e), alive_.end());
}
} // namespace gc6
