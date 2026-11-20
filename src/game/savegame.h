#pragma once
// Save / load: one JSON file (savegame.json) holding the player's progress.
// Hand-rolled writer + minimal parser (the engine has no JSON dependency).
// Fields are versioned (formatVersion) so old saves fail loudly, not silently.
#include "core/math.h"
#include <array>
#include <string>
namespace gc6 {
constexpr int kSaveFormatVersion = 1;
constexpr int kMaxCars = 6;
struct SaveData {
    int formatVersion = kSaveFormatVersion;
    Vec3 playerPos{0, 0, 0};
    int money = 0;
    std::array<bool, kMaxCars> carsOwned{false, false, false, false, false, false};
    int currentCar = 0;
    int paintIndex = 0;
    std::array<bool, 6> missionsCompleted{false, false, false, false, false, false};
    float wantedHeat = 0.0f;
    float timeOfDay = 10.0f;
    int collectiblesFound = 0;
    int bestStuntScore = 0;
    std::array<int, 6> starRatings{0, 0, 0, 0, 0, 0}; // 0 = not done, 1..3 stars
    // upgrades[car][kind] level 0..3 (kind order: Engine, Brakes, Tyres, Suspension)
    std::array<std::array<int, 4>, 6> upgrades{};
    float playTimeMinutes = 0.0f;
};
bool saveGame(const std::string& path, const SaveData& data);
bool loadGame(const std::string& path, SaveData& out);
} // namespace gc6
