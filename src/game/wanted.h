#pragma once
#include "core/math.h"
#include "game/city.h"
#include "game/pathfinding.h"
#include "physics/collision.h"
#include <vector>
namespace gc6 {
// Wanted system: heat 0..100, stars = heat/20. Police chase while stars > 0;
// heat decays when the player stays out of sight (no police nearby).
class Wanted {
public:
    void addHeat(float amount) { heat_ = std::min(100.0f, heat_ + amount); }
    void setHeat(float h) { heat_ = std::max(0.0f, std::min(100.0f, h)); }
    void update(float dt, bool policeNearby, bool causingChaos) {
        if (!policeNearby && !causingChaos) heat_ = std::max(0.0f, heat_ - 4.0f * dt);
        if (causingChaos) heat_ = std::min(100.0f, heat_ + 6.0f * dt);
    }
    int stars() const { return static_cast<int>(heat_ / 20.0f); }
    float heat() const { return heat_; }
    bool isWanted() const { return heat_ > 1.0f; }
private:
    float heat_ = 0.0f;
};
struct PoliceCar {
    Vec3 position{0, 0, 0};
    float yaw = 0.0f, speed = 0.0f;
    std::vector<Vec3> route; // A* route to the player (recomputed often)
    size_t routeIndex = 0;
    float repathTimer = 0.0f;
    bool roadblock = false;   // parked across the road ahead of the player
};
class Police {
public:
    void init(const City& city);
    void update(float dt, const Vec3& playerPos, int stars, const std::vector<AABB>& solids);
    const std::vector<PoliceCar>& cars() const { return cars_; }
    bool anyNearby(const Vec3& p, float radius) const {
        for (const auto& c : cars_) if (length(c.position - p) < radius) return true;
        return false;
    }
    // Roadblock positions (for the HUD minimap + collision): parked police
    // cars block the road ahead of the player at wanted 3+.
    std::vector<Vec3> roadblockPositions() const;
private:
    std::vector<PoliceCar> cars_;
    RoadGraph graph_;
    bool graphReady_ = false;
};
} // namespace gc6
