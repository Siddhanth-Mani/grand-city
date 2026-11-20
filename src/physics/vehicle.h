#pragma once
#include "core/math.h"
#include "physics/collision.h"
#include "physics/drivetrain.h"
#include "physics/obb.h"
#include "physics/tires.h"
#include <array>
#include <string>
#include <vector>
namespace gc6 {
struct VehicleParams {
    std::string name = "Sedan";
    std::string carClass = "sedan";
    float massKg = 1400.0f;        // kg
    float engineForce = 9000.0f;   // N at full throttle (legacy fallback, before curve)
    float brakeForce = 14000.0f;   // N
    float maxSteerDeg = 32.0f;     // at standstill; falls with speed
    float grip = 8.0f;             // lateral velocity decay rate (1/s)
    float dragCoeff = 0.35f;       // quadratic drag (Cd)
    float rollingResistance = 250.0f;
    float topSpeed = 55.0f;        // m/s (~198 km/h)
    float wheelBase = 2.8f;        // m
    float suspensionStiffness = 22000.0f; // N/m per wheel
    float suspensionDamping = 2600.0f;    // Ns/m per wheel
    // --- realistic specs (loaded from data/cars.json) ---
    float horsepower = 300.0f;
    float peakTorqueNm = 400.0f;
    float peakTorqueRpm = 4500.0f;
    float redlineRpm = 6500.0f;
    float idleRpm = 900.0f;
    std::array<float, 6> gearRatios = {3.5f, 2.2f, 1.6f, 1.2f, 0.9f, 0.7f};
    int gearCount = 5;
    float finalDrive = 3.4f;
    Drivetrain drivetrain = Drivetrain::RWD;
    float weightDistFront = 0.52f; // fraction of weight on the front axle
    float downforceCoeff = 0.0f;   // N per (m/s)^2
    float frontalArea = 2.2f;      // m^2
    float zeroToHundred = 7.0f;    // s (catalogue figure)
    float wheelRadius = 0.35f;
    float trackWidth = 1.6f;
    float cgHeight = 0.55f;        // centre-of-gravity height (m)
    int price = 20000;
};
// Full drivetrain + Pacejka tire + raycast suspension vehicle.
// Public API is the same as v1 (update/throttle/steer/brake/solids) so old
// tests keep passing; the new model lives underneath. See docs/PHYSICS.md.
class Vehicle {
public:
    explicit Vehicle(VehicleParams params = VehicleParams()) : params_(params) {}
    void setPosition(const Vec3& p, float yaw) { position_ = p; yaw_ = yaw; }
    void update(float dt, float throttle, float steerInput, float brake,
                const std::vector<AABB>& solids);
    Vec3 position() const { return position_; }
    float yaw() const { return yaw_; }
    float speed() const { return speed_; }          // forward speed m/s
    float speedKmh() const { return speed_ * 3.6f; }
    float steerAngle() const { return steer_; }
    float bodyRoll() const { return bodyRoll_; }
    float bodyPitch() const { return bodyPitch_; }
    const std::array<float, 4>& suspensionTravel() const { return suspension_; }
    AABB bounds() const;
    const VehicleParams& params() const { return params_; }
    bool crashed() const { return crashed_; }
    // --- new state (v2) ---
    float rpm() const { return rpm_; }
    int gear() const { return gearbox_.gear; }
    float damage() const { return damage_; }
    bool isSmoking() const { return damage_ > 0.6f; }
    bool isAirborne() const { return airborne_; }
    float airTime() const { return airTime_; }
    float lastAirTime() const { return lastAirTime_; }
    float wheelSpin() const { return wheelSpin_; }
    float downforceNow() const { return downforceNow_; }
    Surface surface() const { return surface_; }
    float frontWheelAngle() const { return steer_; }
    void setWet(bool wet) { wet_ = wet; }
    void setAssists(const Assists& a) { assists_ = a; }
    void setHandbrake(bool on) { handbrake_ = on; }
    void setSurfaceOverride(Surface s) { surfaceOverride_ = s; surfaceOverrideSet_ = true; }
    void launch(float verticalVelocity); // stunt-ramp launch
    void addDamage(float amount);
    // OBB-vs-OBB collision with another car: separates both, exchanges
    // speed along the impact normal, flags crashes, adds damage.
    void collideWith(Vehicle& other);
    OBB obb() const;
private:
    VehicleParams params_;
    Vec3 position_{0, 0, 0};
    float yaw_ = 0.0f, speed_ = 0.0f, steer_ = 0.0f;
    float bodyRoll_ = 0.0f, bodyPitch_ = 0.0f;
    float lateralVel_ = 0.0f;
    std::array<float, 4> suspension_{0.1f, 0.1f, 0.1f, 0.1f};
    std::array<float, 4> prevCompression_{0.0f, 0.0f, 0.0f, 0.0f};
    bool crashed_ = false;
    float roadHeight(float x, float z) const;
    // --- v2 state ---
    GearboxState gearbox_;
    Assists assists_;
    float rpm_ = 900.0f;
    float damage_ = 0.0f;
    float brakeTempC_ = 60.0f;
    float wheelSpin_ = 0.0f;
    float downforceNow_ = 0.0f;
    float time_ = 0.0f;
    float vy_ = 0.0f;
    bool airborne_ = false;
    float airTime_ = 0.0f, lastAirTime_ = 0.0f;
    bool wet_ = false, handbrake_ = false;
    Surface surface_ = Surface::Tarmac;
    bool surfaceOverrideSet_ = false;
    Surface surfaceOverride_ = Surface::Tarmac;
};
} // namespace gc6
