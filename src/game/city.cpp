#include "game/city.h"
#include <cmath>
#include <random>
namespace gc6 {
void City::generate(int seed) {
    buildings.clear(); solids.clear(); lamps.clear(); lights.clear(); roadNodes.clear();
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_real_distribution<float> hDist(8.0f, 55.0f);
    std::uniform_real_distribution<float> cDist(0.25f, 0.75f);
    float spacing = roadSpacing();
    int half = static_cast<int>(size / 2);
    // Road grid every 40 m; blocks between roads hold one building each.
    for (int gx = -half; gx < half; gx += static_cast<int>(spacing)) {
        for (int gz = -half; gz < half; gz += static_cast<int>(spacing)) {
            roadNodes.push_back(Vec3(gx, 0, gz));
            // Leave the beach strip (-Z edge) and the central plaza open.
            if (gz < -half + 40) continue;
            if (std::abs(gx) < 25 && std::abs(gz) < 25) continue;
            float h = hDist(rng);
            Vec3 centre(gx + spacing / 2, 0, gz + spacing / 2);
            float w = 18.0f + (rng() % 10);
            Building b;
            b.bounds = {centre + Vec3(-w / 2, 0, -w / 2), centre + Vec3(w / 2, h, w / 2)};
            b.height = h;
            b.colour = {cDist(rng), cDist(rng), cDist(rng)};
            buildings.push_back(b);
            solids.push_back(b.bounds);
        }
    }
    // Lamps along roads, traffic lights at every other intersection.
    for (size_t i = 0; i < roadNodes.size(); i += 2)
        lamps.push_back(roadNodes[i] + Vec3(4, 0, 4));
    for (size_t i = 0; i < roadNodes.size(); i += 4) {
        TrafficLight tl; tl.position = roadNodes[i]; tl.timer = static_cast<float>(i % 8);
        lights.push_back(tl);
    }
}
bool City::isOnRoad(const Vec3& p) const {
    float s = roadSpacing();
    float mx = std::fmod(std::fabs(p.x), s);
    float mz = std::fmod(std::fabs(p.z), s);
    return mx < 7.0f || mx > s - 7.0f || mz < 7.0f || mz > s - 7.0f;
}
} // namespace gc6
