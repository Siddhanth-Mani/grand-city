#pragma once
// Side activities: collectibles scattered through the city (worth $50 each)
// and stunt jumps (ramps that launch the car — air time × speed = points).
// Deterministic positions from the world seed, so every player finds the
// same 20 packages and 4 jumps.
#include "core/math.h"
#include <vector>
namespace gc6 {
struct Collectible { Vec3 position; bool taken = false; int value = 50; };
struct StuntJump {
    Vec3 position;   // ramp base
    Vec3 direction;  // launch direction (unit XZ)
    float rampHeight = 1.2f;
    float launchSpeed = 8.0f; // vertical launch velocity (m/s)
    bool used = false;
};
class SideActivities {
public:
    void init(int seed);
    // Returns money earned this frame from newly collected items.
    int update(const Vec3& playerPos, bool inCar);
    // Checks whether the player vehicle is on a ramp at speed; if so,
    // returns the launch velocity, else 0.
    float checkRamp(const Vec3& carPos, float speedMs);
    void onLanded(float airTimeSeconds, float speedMs); // scores a jump
    int collected() const { return collected_; }
    int stuntScore() const { return stuntScore_; }
    int bestStunt() const { return bestStunt_; }
    const std::vector<Collectible>& items() const { return items_; }
    const std::vector<StuntJump>& jumps() const { return jumps_; }
private:
    std::vector<Collectible> items_;
    std::vector<StuntJump> jumps_;
    int collected_ = 0;
    int stuntScore_ = 0;
    int bestStunt_ = 0;
};
} // namespace gc6
