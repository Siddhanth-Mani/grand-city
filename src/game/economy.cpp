#include "game/economy.h"
namespace gc6 {
void Garage::init(std::vector<CarDef> catalogue, int startingMoney) {
    catalogue_ = std::move(catalogue);
    money_ = startingMoney;
    owned_.fill(false);
    // Starter car: the free one (price 0 — the Meridian sedan).
    int starter = 0;
    for (size_t i = 0; i < catalogue_.size(); ++i)
        if (catalogue_[i].price == 0) { starter = static_cast<int>(i); break; }
    if (!catalogue_.empty()) owned_[static_cast<size_t>(starter)] = true;
    currentCar_ = starter;
    paintIndex_ = 0;
    for (auto& u : upgrades_) u.fill(0);
}
bool Garage::owns(int index) const {
    if (index < 0 || index >= 6) return false;
    return owned_[static_cast<size_t>(index)];
}
bool Garage::buyCar(int index) {
    if (index < 0 || index >= carCount()) return false;
    if (owns(index)) return false;
    int price = catalogue_[static_cast<size_t>(index)].price;
    if (money_ < price) return false;
    money_ -= price;
    owned_[static_cast<size_t>(index)] = true;
    return true;
}
bool Garage::selectCar(int index) {
    if (!owns(index)) return false;
    currentCar_ = index;
    return true;
}
bool Garage::repaint(int paint) {
    if (money_ < kPaintCost) return false;
    money_ -= kPaintCost;
    paintIndex_ = paint;
    return true;
}
int Garage::upgradeCost(UpgradeKind kind, int nextLevel) {
    // nextLevel 1..3; cost doubles per level.
    int base = 0;
    switch (kind) {
        case UpgradeKind::Engine: base = 1500; break;
        case UpgradeKind::Brakes: base = 800; break;
        case UpgradeKind::Tyres: base = 600; break;
        case UpgradeKind::Suspension: base = 700; break;
    }
    return base * (1 << (nextLevel - 1));
}
int Garage::upgradeLevel(int carIndex, UpgradeKind kind) const {
    if (carIndex < 0 || carIndex >= 6) return 0;
    return upgrades_[static_cast<size_t>(carIndex)][static_cast<size_t>(kind)];
}
bool Garage::buyUpgrade(int carIndex, UpgradeKind kind) {
    if (!owns(carIndex)) return false;
    int level = upgradeLevel(carIndex, kind);
    if (level >= 3) return false;
    int cost = upgradeCost(kind, level + 1);
    if (money_ < cost) return false;
    money_ -= cost;
    upgrades_[static_cast<size_t>(carIndex)][static_cast<size_t>(kind)] = level + 1;
    return true;
}
CarDef Garage::upgradedParams(int carIndex) const {
    CarDef out = catalogue_[static_cast<size_t>(carIndex)];
    int engine = upgradeLevel(carIndex, UpgradeKind::Engine);
    int brakes = upgradeLevel(carIndex, UpgradeKind::Brakes);
    int tyres = upgradeLevel(carIndex, UpgradeKind::Tyres);
    int suspension = upgradeLevel(carIndex, UpgradeKind::Suspension);
    if (engine > 0) {
        float f = 1.0f + 0.12f * static_cast<float>(engine);
        out.params.horsepower *= f;
        out.params.peakTorqueNm *= f;
        out.params.engineForce *= f;
        out.params.topSpeed *= 1.0f + 0.03f * static_cast<float>(engine);
    }
    if (brakes > 0) out.params.brakeForce *= 1.0f + 0.20f * static_cast<float>(brakes);
    if (tyres > 0) out.params.grip *= 1.0f + 0.08f * static_cast<float>(tyres);
    if (suspension > 0) {
        out.params.suspensionStiffness *= 1.0f + 0.15f * static_cast<float>(suspension);
        out.params.suspensionDamping *= 1.0f + 0.15f * static_cast<float>(suspension);
    }
    return out;
}
void Garage::applySave(const std::array<bool, 6>& owned, int current, int paint, int money) {
    owned_ = owned;
    if (!catalogue_.empty() && !owned_[0]) owned_[0] = true;
    currentCar_ = (current >= 0 && current < carCount()) ? current : 0;
    paintIndex_ = paint;
    money_ = money;
}
void Garage::applyUpgrades(const std::array<std::array<int, 4>, 6>& upgrades) {
    upgrades_ = upgrades;
}
void Garage::fillSave(std::array<bool, 6>& owned, int& current, int& paint) const {
    owned = owned_;
    current = currentCar_;
    paint = paintIndex_;
}
} // namespace gc6
