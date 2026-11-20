#include "game/collectibles.h"
#include <cmath>
#include <random>
namespace gc6 {
void SideActivities::init(int seed) {
    items_.clear();
    jumps_.clear();
    collected_ = 0;
    stuntScore_ = 0;
    bestStunt_ = 0;
    std::mt19937 rng(static_cast<unsigned>(seed * 31 + 7));
    // 50 hidden packages: on road intersections, offset onto the sidewalk.
    // Positions are unique (no two packages stacked on the same cell).
    std::vector<std::pair<int, int>> used;
    for (int i = 0; i < 50; ++i) {
        int gx = 0, gz = 0;
        bool unique = false;
        while (!unique) {
            gx = static_cast<int>(rng() % 10);
            gz = static_cast<int>(rng() % 10);
            unique = true;
            for (const auto& u : used)
                if (u.first == gx && u.second == gz) { unique = false; break; }
        }
        used.push_back({gx, gz});
        float x = -180.0f + static_cast<float>(gx) * 40.0f;
        float z = -180.0f + static_cast<float>(gz) * 40.0f;
        items_.push_back({Vec3(x + 6.0f, 0.5f, z + 6.0f), false, 50});
    }
    // 10 stunt jumps: ramps on straight roads, pointing down the road.
    const Vec3 spots[10] = {{-80, 0, -40}, {80, 0, 40}, {0, 0, -120}, {120, 0, -80},
                            {-120, 0, 80}, {40, 0, 120}, {-40, 0, -160}, {160, 0, 0},
                            {0, 0, 160}, {-160, 0, -120}};
    const Vec3 dirs[10] = {{0, 0, 1}, {1, 0, 0}, {0, 0, 1}, {-1, 0, 0},
                           {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1},
                           {0, 0, -1}, {1, 0, 0}};
    for (int i = 0; i < 10; ++i) {
        StuntJump j;
        j.position = spots[i];
        j.direction = dirs[i];
        jumps_.push_back(j);
    }
}
int SideActivities::update(const Vec3& playerPos, bool) {
    int earned = 0;
    for (auto& item : items_) {
        if (item.taken) continue;
        if (length(Vec3(item.position.x - playerPos.x, 0, item.position.z - playerPos.z)) < 3.0f) {
            item.taken = true;
            ++collected_;
            earned += item.value;
        }
    }
    return earned;
}
float SideActivities::checkRamp(const Vec3& carPos, float speedMs) {
    if (speedMs < 18.0f) return 0.0f;
    for (auto& j : jumps_) {
        if (j.used) continue;
        if (length(Vec3(j.position.x - carPos.x, 0, j.position.z - carPos.z)) < 5.0f) {
            j.used = true;
            return j.launchSpeed;
        }
    }
    return 0.0f;
}
void SideActivities::onLanded(float airTimeSeconds, float speedMs) {
    if (airTimeSeconds < 0.6f) return;
    int points = static_cast<int>(airTimeSeconds * speedMs * 10.0f);
    stuntScore_ += points;
    if (points > bestStunt_) bestStunt_ = points;
}
} // namespace gc6
