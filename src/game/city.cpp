#include "game/city.h"
#include <cmath>
#include <random>
namespace gc6 {
void City::generate(int seed) {
    buildings.clear(); solids.clear(); lamps.clear(); lights.clear(); roadNodes.clear();
    streetLamps.clear(); trees.clear();
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
            // Taller towers towards the centre, low-rise towards the edges.
            float centreDist = std::sqrt(static_cast<float>(gx * gx + gz * gz)) / static_cast<float>(half);
            h *= 1.25f - 0.55f * std::min(centreDist, 1.0f);
            Vec3 centre(gx + spacing / 2, 0, gz + spacing / 2);
            // Footprints stop at the back of the 4 m pavement.
            float wx = 16.0f + static_cast<float>(rng() % 3);
            float wz = 16.0f + static_cast<float>(rng() % 3);
            Building b;
            b.bounds = {centre + Vec3(-wx / 2, 0, -wz / 2), centre + Vec3(wx / 2, h, wz / 2)};
            b.height = h;
            float c0 = cDist(rng), c1 = cDist(rng), c2 = cDist(rng);
            b.colour = {c0, c1, c2};
            b.seed = (c0 + c1 * 3.0f + c2 * 7.0f) - std::floor(c0 + c1 * 3.0f + c2 * 7.0f);
            if (h > 34.0f) b.style = b.seed < 0.62f ? 1 : 0;
            else if (h > 18.0f) b.style = b.seed < 0.4f ? 0 : (b.seed < 0.7f ? 3 : 2);
            else b.style = b.seed < 0.55f ? 2 : 3;
            b.podium = h > 30.0f ? 8.6f : 0.0f;
            buildings.push_back(b);
            solids.push_back(b.bounds);
        }
    }
    // Streetlamps at the kerb in the middle of every block side, arms over the
    // road; street trees either side of them. The beach strip has neither.
    for (int gx = -half; gx < half; gx += static_cast<int>(spacing)) {
        for (int gz = -half + 40; gz < half; gz += static_cast<int>(spacing)) {
            float x0 = static_cast<float>(gx), z0 = static_cast<float>(gz);
            const float kerb = 7.7f;
            StreetLamp sides[4] = {{Vec3(x0 + 20, 0, z0 + kerb), glm::pi<float>() / 2},
                                   {Vec3(x0 + 20, 0, z0 + spacing - kerb), -glm::pi<float>() / 2},
                                   {Vec3(x0 + kerb, 0, z0 + 20), glm::pi<float>()},
                                   {Vec3(x0 + spacing - kerb, 0, z0 + 20), 0.0f}};
            for (const auto& l : sides) {
                streetLamps.push_back(l);
                Vec3 arm{std::cos(l.yaw), 0, -std::sin(l.yaw)};
                lamps.push_back(l.position + arm * 1.6f + Vec3(0, 7.25f, 0));
            }
            for (int k = 0; k < 4; ++k) {
                float along[2] = {12.5f, 27.5f};
                for (float a : along) {
                    Vec3 p = k == 0 ? Vec3(x0 + a, 0, z0 + 8.1f)
                           : k == 1 ? Vec3(x0 + a, 0, z0 + spacing - 8.1f)
                           : k == 2 ? Vec3(x0 + 8.1f, 0, z0 + a)
                                    : Vec3(x0 + spacing - 8.1f, 0, z0 + a);
                    unsigned r = rng();
                    trees.push_back({p, static_cast<int>(r % 2), 0.85f + static_cast<float>(r % 7) * 0.05f});
                }
            }
        }
    }
    for (size_t i = 0; i < roadNodes.size(); i += 4) {
        TrafficLight tl; tl.position = roadNodes[i]; tl.timer = static_cast<float>(i % 8);
        lights.push_back(tl);
    }
}
float City::groundHeight(const Vec3& p) const {
    float half = size / 2;
    if (p.z < -half + 40.0f || std::fabs(p.x) > half || p.z > half) return 0.0f;
    return isOnRoad(p) ? 0.0f : kKerb;
}
bool City::isOnRoad(const Vec3& p) const {
    float s = roadSpacing();
    float mx = std::fmod(std::fabs(p.x), s);
    float mz = std::fmod(std::fabs(p.z), s);
    return mx < 7.0f || mx > s - 7.0f || mz < 7.0f || mz > s - 7.0f;
}
} // namespace gc6
