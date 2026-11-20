#pragma once
#include "core/input.h"
#include "core/math.h"
#include "game/city.h"
#include "physics/character.h"
#include "physics/vehicle.h"
#include <memory>
#include <vector>
namespace gc6 {
// The player: on foot (Character) or driving (Vehicle). E enters/exits a car.
class Player {
public:
    void init(const City& city);
    void update(float dt, const InputState& input, const std::vector<AABB>& solids,
                const std::vector<Vehicle*>& nearbyCars);
    // Swap the player's own car (garage selection / upgrades): keeps the
    // current position and yaw when already driving.
    void setVehicleParams(const VehicleParams& p);
    Vehicle* ownedVehicle() { return ownedVehicle_.get(); }
    bool onFoot() const { return onFoot_; }
    Vec3 position() const;
    float yaw() const;
    float speedKmh() const;
    int health() const { return health_; }
    void damage(int amount) { health_ = std::max(0, health_ - amount); }
    Vehicle* currentVehicle() { return currentVehicle_; }
private:
    Character character_;
    std::unique_ptr<Vehicle> ownedVehicle_;
    Vehicle* currentVehicle_ = nullptr;
    bool onFoot_ = true;
    int health_ = 100;
};
} // namespace gc6
