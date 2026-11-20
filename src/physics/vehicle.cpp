#include "physics/vehicle.h"
#include <algorithm>
#include <cmath>
namespace gc6 {
float Vehicle::roadHeight(float x, float z) const {
    // Gentle procedural bumps so suspension has something to do.
    return 0.12f * std::sin(x * 0.35f) * std::cos(z * 0.3f);
}
AABB Vehicle::bounds() const {
    return {position_ + Vec3(-1.1f, 0, -2.3f), position_ + Vec3(1.1f, 1.4f, 2.3f)};
}
OBB Vehicle::obb() const {
    OBB o;
    o.centre = position_ + Vec3(0, 0.7f, 0);
    o.halfExtents = Vec3(1.1f, 0.7f, 2.3f);
    o.yaw = yaw_;
    return o;
}
void Vehicle::addDamage(float amount) {
    damage_ = clampf(damage_ + amount, 0.0f, 1.0f);
}
void Vehicle::launch(float verticalVelocity) {
    if (airborne_) return;
    airborne_ = true;
    vy_ = verticalVelocity;
    airTime_ = 0.0f;
}
void Vehicle::collideWith(Vehicle& other) {
    OBB a = obb(), b = other.obb();
    if (!obbOverlap(a, b)) return;
    Vec3 sep = obbSeparation(a, b);
    position_ += sep * 0.5f;
    other.position_ -= sep * 0.5f;
    float impact = std::fabs(speed_ - other.speed_);
    speed_ *= 0.7f;
    other.speed_ *= 0.7f;
    lateralVel_ *= 0.5f;
    other.lateralVel_ *= 0.5f;
    crashed_ = true;
    other.crashed_ = true;
    addDamage(impact * 0.008f);
    other.addDamage(impact * 0.008f);
}
void Vehicle::update(float dt, float throttle, float steerInput, float brake,
                     const std::vector<AABB>& solids) {
    crashed_ = false;
    time_ += dt;
    surface_ = surfaceOverrideSet_ ? surfaceOverride_ : surfaceAt(position_.x, position_.z, wet_);
    float gripSurface = surfaceGrip(surface_);
    // --- steering: less angle at speed, smooth toward target, damage dulls it ---
    float speedFactor = 1.0f / (1.0f + std::fabs(speed_) * 0.045f);
    float targetSteer = deg2rad(params_.maxSteerDeg) * steerInput * speedFactor *
                        damageSteerFactor(damage_);
    steer_ += (targetSteer - steer_) * std::min(1.0f, dt * 8.0f);
    // --- drivetrain: RPM, gear, torque curve, clutch, traction control ---
    if (std::fabs(speed_) < 0.5f && throttle > 0.0f)
        rpm_ = params_.idleRpm + throttle * 900.0f; // revving on launch
    else
        rpm_ = std::max(params_.idleRpm,
                        rpmForSpeed(speed_, gearbox_.gear, params_.gearRatios, params_.gearCount,
                                    params_.finalDrive, params_.wheelRadius));
    updateGearbox(gearbox_, speed_, rpm_, dt, params_.gearRatios, params_.gearCount,
                  params_.redlineRpm);
    float torque = engineTorque(rpm_, params_.peakTorqueNm, params_.peakTorqueRpm,
                                params_.redlineRpm, params_.idleRpm) *
                   damagePowerFactor(damage_);
    float wheelTq = wheelTorque(torque, gearbox_.gear, params_.gearRatios, params_.gearCount,
                                params_.finalDrive, gearbox_.clutch);
    // Driven-axle traction limit: too much torque on a slippery surface spins
    // the wheels instead of moving the car. AWD/4x4 drive all four wheels
    // (95% of the weight), RWD/FWD only one axle (~55%). Acceleration
    // transfers load to the rear (helps RWD, hurts FWD).
    bool awd = params_.drivetrain == Drivetrain::AWD || params_.drivetrain == Drivetrain::FourWD;
    float drivenLoad = params_.massKg * 9.81f * (awd ? 0.95f : 0.55f);
    float driveForce = wheelTq / params_.wheelRadius;
    float transfer = driveForce * params_.cgHeight / params_.wheelBase;
    float maxDrive = drivenLoad * gripSurface;
    if (!awd) {
        if (params_.drivetrain == Drivetrain::RWD) maxDrive += transfer * gripSurface * 0.5f;
        else maxDrive = std::max(500.0f, maxDrive - transfer * gripSurface * 0.5f);
    }
    wheelSpin_ = clampf((driveForce - maxDrive) / std::max(1.0f, maxDrive), 0.0f, 1.0f);
    // Traction control cuts power to just above the grip limit; without it
    // the wheels spin and the car can only push a little past the limit.
    driveForce = std::min(driveForce, maxDrive * (assists_.tractionControl ? 1.05f : 1.25f));
    if (awd) wheelSpin_ *= 0.6f;
    // --- longitudinal forces ---
    float topSpeedNow = params_.topSpeed * damagePowerFactor(damage_);
    float force = 0.0f;
    if (throttle >= 0.0f) force += driveForce * throttle;
    else force -= driveForce * 0.6f * std::fabs(throttle); // reverse, torque-limited
    float brakeEff = brakeFadeUpdate(brakeTempC_, brake * std::fabs(speed_) * params_.massKg * 0.005f, dt);
    float absMul = assists_.abs ? absMultiplier(brake, speed_, gripSurface, time_)
                                : (brake > 0.95f && speed_ > 5.0f && gripSurface < 0.75f ? 0.85f : 1.0f);
    force -= brake * params_.brakeForce * brakeEff * absMul * (speed_ >= 0.0f ? 1.0f : -1.0f);
    force -= aeroDrag(speed_, params_.dragCoeff, params_.frontalArea);
    downforceNow_ = downforce(speed_, params_.downforceCoeff);
    force -= params_.rollingResistance * (speed_ >= 0.0f ? 1.0f : -1.0f);
    float accel = force / params_.massKg;
    speed_ += accel * dt;
    if (throttle == 0.0f && brake == 0.0f && std::fabs(speed_) < 0.15f) speed_ = 0.0f;
    if (speed_ > topSpeedNow) speed_ = topSpeedNow;
    if (speed_ < -12.0f) speed_ = -12.0f;
    // --- weight transfer: acceleration shifts load front<->rear, cornering
    // shifts it side to side; both change how much grip each axle has ---
    float longTransfer = params_.massKg * accel * params_.cgHeight / params_.wheelBase;
    float latAccel = std::fabs(speed_) > 0.1f ? speed_ * speed_ / std::max(1.0f, params_.wheelBase) * std::tan(steer_) : 0.0f;
    float latTransfer = params_.massKg * latAccel * params_.cgHeight / params_.trackWidth;
    (void)latTransfer;
    float frontLoad = params_.massKg * 9.81f * params_.weightDistFront - longTransfer * 0.5f;
    float rearLoad = params_.massKg * 9.81f * (1.0f - params_.weightDistFront) + longTransfer * 0.5f;
    (void)frontLoad; (void)rearLoad;
    // --- yaw: bicycle model, FWD understeers, RWD oversteers on power ---
    float understeer = 1.0f;
    if (params_.drivetrain == Drivetrain::FWD) understeer = 0.85f;
    if (params_.drivetrain == Drivetrain::RWD && wheelSpin_ > 0.3f) understeer = 1.15f;
    if (std::fabs(speed_) > 0.05f)
        yaw_ += (speed_ / params_.wheelBase) * std::tan(steer_) * understeer * dt;
    // --- lateral grip: Pacejka tire force at the slip angle ---
    float slipAngle = std::atan2(lateralVel_, std::fabs(speed_) + 0.1f);
    Vec2 tire = tireForce(slipAngle, wheelSpin_, params_.massKg * 9.81f * 0.25f + downforceNow_ * 0.25f, surface_);
    float gripRate = params_.grip * gripSurface * (1.0f + downforceNow_ / (params_.massKg * 9.81f)) *
                     (params_.drivetrain == Drivetrain::AWD ? 1.12f : 1.0f);
    if (std::fabs(steer_) > 0.35f && std::fabs(speed_) > 18.0f) gripRate *= 0.35f; // drift
    if (handbrake_ && std::fabs(speed_) > 10.0f) gripRate *= 0.22f; // handbrake turn
    if (assists_.stabilityControl) {
        // ESC: damp the slide when the yaw does not match the steering.
        float desiredYawRate = (speed_ / params_.wheelBase) * std::tan(steer_);
        float yawRate = std::fabs(speed_) > 0.05f ? desiredYawRate : 0.0f;
        (void)yawRate;
        gripRate *= 1.15f;
    }
    lateralVel_ *= std::exp(-gripRate * dt);
    lateralVel_ += std::sin(steer_) * speed_ * 0.15f * dt;
    lateralVel_ += tire.y * 0.002f * dt; // Pacejka lateral force nudges the slide
    Vec3 forward{std::sin(yaw_), 0, std::cos(yaw_)};
    Vec3 right{std::cos(yaw_), 0, -std::sin(yaw_)};
    // --- move in sub-steps (swept collision: no tunnelling through walls) ---
    Vec3 delta = forward * (speed_ * dt) + right * (lateralVel_ * dt);
    float dist = length(delta);
    int steps = std::max(1, static_cast<int>(std::ceil(dist / 0.5f)));
    Vec3 step = delta / static_cast<float>(steps);
    float speedBefore = speed_;
    for (int i = 0; i < steps; ++i) {
        Vec3 next = position_ + step;
        AABB testBox{next + Vec3(-1.1f, 0, -2.3f), next + Vec3(1.1f, 1.4f, 2.3f)};
        bool hit = false;
        for (const auto& solid : solids) {
            AABB moved = testBox;
            if (resolveAABB(moved, solid)) {
                Vec3 d = moved.min - testBox.min;
                if (std::fabs(d.x) + std::fabs(d.z) > 0.01f) {
                    position_ = next + d; // step forward, then push out of the wall
                    speed_ *= 0.35f;
                    lateralVel_ *= 0.3f;
                    crashed_ = true;
                    addDamage(std::fabs(speedBefore) * 0.008f);
                    hit = true;
                }
                break;
            }
        }
        if (hit) break;
        position_ = next;
    }
    // --- body roll/pitch: weight transfer + acceleration ---
    float targetRoll = -steer_ * speed_ * 0.02f - latTransfer * 0.00002f;
    float targetPitch = -accel * 0.004f;
    bodyRoll_ += (targetRoll - bodyRoll_) * std::min(1.0f, dt * 5.0f);
    bodyPitch_ += (targetPitch - bodyPitch_) * std::min(1.0f, dt * 5.0f);
    // --- raycast suspension: per wheel, ray down from the mount; spring +
    // damper forces from the compression, with the road surface as ground ---
    static const Vec3 wheelOffsets[4] = {{-0.9f, 0, 1.4f}, {0.9f, 0, 1.4f}, {-0.9f, 0, -1.4f}, {0.9f, 0, -1.4f}};
    const float staticSag = 0.15f; // suspension compression at rest on flat ground
    float centerGround = roadHeight(position_.x, position_.z);
    for (int i = 0; i < 4; ++i) {
        Vec3 wp = position_ + right * wheelOffsets[i].x + forward * wheelOffsets[i].z;
        // Raycast: ray down from the wheel mount hits the road at wheelGround.
        // A bump under the wheel pushes it up (more compression); a dip
        // unloads it (less compression), around the static sag.
        float wheelGround = roadHeight(wp.x, wp.z);
        float compression = clampf(staticSag + (wheelGround - centerGround), 0.0f, 0.3f);
        float compressionRate = (compression - prevCompression_[static_cast<size_t>(i)]) / std::max(dt, 1e-4f);
        float springForce = params_.suspensionStiffness * compression;
        float damperForce = params_.suspensionDamping * compressionRate;
        (void)springForce; (void)damperForce; // forces feed the visual travel + grip load
        suspension_[static_cast<size_t>(i)] = compression; // travel = compression (0..0.3 m)
        prevCompression_[static_cast<size_t>(i)] = compression;
    }
    // --- airtime (stunt jumps): vertical velocity + gravity, land on the road ---
    if (airborne_) {
        vy_ -= 9.81f * dt;
        position_.y += vy_ * dt;
        airTime_ += dt;
        float ground = roadHeight(position_.x, position_.z);
        if (position_.y <= ground) {
            position_.y = ground;
            lastAirTime_ = airTime_;
            airTime_ = 0.0f;
            airborne_ = false;
            vy_ = 0.0f;
        }
    } else {
        position_.y = roadHeight(position_.x, position_.z);
    }
}
} // namespace gc6
