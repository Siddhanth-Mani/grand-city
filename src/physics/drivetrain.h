#pragma once
// Drivetrain: engine torque curve, gearbox with real gears and shift points,
// clutch slip on launch, differential behaviour per drivetrain, ABS /
// traction control / stability control assists, aerodynamics, brake fade,
// and a damage model. Pure functions + small state structs so the whole
// thing is unit-testable without a Vehicle. See docs/PHYSICS.md.
#include <array>
namespace gc6 {
enum class Drivetrain { FWD, RWD, AWD, FourWD };
struct Assists {
    bool abs = true;            // anti-lock braking
    bool tractionControl = true;
    bool stabilityControl = true;
};
struct GearboxState {
    int gear = 1;               // 1-based, 0 = reverse
    float clutch = 1.0f;        // 0 = slipping, 1 = fully engaged
    float shiftTimer = 0.0f;    // time spent shifting (torque cut)
};
// Engine torque (Nm) at an RPM, from a peak-torque curve: rises to the
// peak, holds briefly, then falls toward the redline.
float engineTorque(float rpm, float peakTorqueNm, float peakTorqueRpm, float redlineRpm, float idleRpm);
// Wheel speed (m/s) -> engine RPM for a given gear.
float rpmForSpeed(float speedMs, int gear, const std::array<float, 6>& ratios, int gearCount,
                  float finalDrive, float wheelRadius);
// Gearbox step: picks the gear for the current speed/RPM, with shift
// hysteresis and a short torque cut while shifting. Returns the gear.
int updateGearbox(GearboxState& gb, float speedMs, float rpm, float dt,
                  const std::array<float, 6>& ratios, int gearCount, float redlineRpm);
// Wheel torque (Nm at the driven axle) from engine torque, gear and diff.
float wheelTorque(float engineTorqueNm, int gear, const std::array<float, 6>& ratios,
                  int gearCount, float finalDrive, float clutch);
// ABS: modulates brake force to avoid wheel lock. Returns the effective
// brake force multiplier (0.7..1.0) — hard braking on a slippery surface
// pulses the brakes instead of locking the wheels.
float absMultiplier(float brakeInput, float speedMs, float surfaceGrip, float time);
// Traction control: cuts engine torque when the driven wheels spin.
// wheelSpin 0..1 in; returns the torque multiplier.
float tractionMultiplier(float wheelSpin, bool enabled);
// Aerodynamics: quadratic drag and downforce (N) at a speed.
float aeroDrag(float speedMs, float dragCoeff, float frontalArea);
float downforce(float speedMs, float downforceCoeff);
// Brake fade: temperature rises with braking energy, falls as it cools.
// Returns the brake effectiveness multiplier (1.0 cold -> ~0.7 hot).
float brakeFadeUpdate(float& tempC, float brakingEnergy, float dt);
// Damage: 0 = pristine, 1 = wrecked. Engine power, steering and top speed
// degrade with damage; above 0.6 the engine smokes.
float damagePowerFactor(float damage);
float damageSteerFactor(float damage);
} // namespace gc6
