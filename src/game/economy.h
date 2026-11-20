#pragma once
// Garage + economy: the player earns money from missions, fares, packages
// and stunt scores, then spends it on cars, paint, and upgrades.
// Upgrades really change handling: engine adds power/torque/top speed,
// brakes shorten stopping distance, tyres add grip, suspension stiffens
// the car (less roll, more responsive). Each car has 4 upgrade tracks,
// 3 levels each.
#include "game/car_data.h"
#include <array>
#include <vector>
namespace gc6 {
constexpr int kPaintCost = 200;
enum class UpgradeKind { Engine = 0, Brakes, Tyres, Suspension };
class Garage {
public:
    void init(std::vector<CarDef> catalogue, int startingMoney);
    int money() const { return money_; }
    void addMoney(int amount) { money_ += amount; }
    int carCount() const { return static_cast<int>(catalogue_.size()); }
    const CarDef& car(int index) const { return catalogue_[static_cast<size_t>(index)]; }
    bool owns(int index) const;
    bool buyCar(int index);           // false when already owned / broke
    int currentCar() const { return currentCar_; }
    bool selectCar(int index);        // false when not owned
    int paintIndex() const { return paintIndex_; }
    bool repaint(int paintIndex);     // costs kPaintCost, false when broke
    const std::vector<CarDef>& catalogue() const { return catalogue_; }
    // --- upgrades ---
    static int upgradeCost(UpgradeKind kind, int nextLevel); // level 1..3
    int upgradeLevel(int carIndex, UpgradeKind kind) const;
    bool buyUpgrade(int carIndex, UpgradeKind kind); // false when maxed / broke / not owned
    // Catalogue entry with this car's upgrades applied — what actually spawns.
    CarDef upgradedParams(int carIndex) const;
    void applySave(const std::array<bool, 6>& owned, int current, int paint, int money);
    void applyUpgrades(const std::array<std::array<int, 4>, 6>& upgrades);
    void fillSave(std::array<bool, 6>& owned, int& current, int& paint) const;
    void fillUpgrades(std::array<std::array<int, 4>, 6>& out) const { out = upgrades_; }
private:
    std::vector<CarDef> catalogue_;
    std::array<bool, 6> owned_{false, false, false, false, false, false};
    std::array<std::array<int, 4>, 6> upgrades_{};
    int money_ = 0;
    int currentCar_ = 0;
    int paintIndex_ = 0;
};
} // namespace gc6
