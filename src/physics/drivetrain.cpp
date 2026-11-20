#include "physics/drivetrain.h"
#include "core/math.h"
#include <cmath>
namespace gc6 {
float engineTorque(float rpm, float peakTorqueNm, float peakTorqueRpm, float redlineRpm, float idleRpm) {
    if (rpm <= idleRpm) return peakTorqueNm * 0.72f;
    if (rpm <= peakTorqueRpm) {
        float t = (rpm - idleRpm) / (peakTorqueRpm - idleRpm);
        return peakTorqueNm * (0.72f + 0.28f * t); // rises to peak
    }
    if (rpm >= redlineRpm) return peakTorqueNm * 0.55f;
    float t = (rpm - peakTorqueRpm) / (redlineRpm - peakTorqueRpm);
    return peakTorqueNm * (1.0f - 0.45f * t); // falls toward redline
}
float rpmForSpeed(float speedMs, int gear, const std::array<float, 6>& ratios, int gearCount,
                  float finalDrive, float wheelRadius) {
    if (gear < 1 || gear > gearCount) return 900.0f;
    float wheelRps = std::fabs(speedMs) / (2.0f * 3.14159f * wheelRadius);
    return 60.0f * wheelRps * ratios[static_cast<size_t>(gear - 1)] * finalDrive;
}
int updateGearbox(GearboxState& gb, float speedMs, float rpm, float dt,
                  const std::array<float, 6>& ratios, int gearCount, float redlineRpm) {
    (void)ratios;
    if (gb.shiftTimer > 0.0f) {
        gb.shiftTimer -= dt;
        gb.clutch = 0.25f; // torque cut while shifting
        return gb.gear;
    }
    gb.clutch = 1.0f;
    if (speedMs < 0.5f) {
        gb.gear = 1; // launch in first
        // Clutch slip on launch: engagement ramps with speed.
        gb.clutch = 0.55f + 0.45f * clampf(speedMs / 8.0f, 0.0f, 1.0f);
        return gb.gear;
    }
    if (rpm > redlineRpm * 0.92f && gb.gear < gearCount) {
        ++gb.gear;
        gb.shiftTimer = 0.18f;
    } else if (rpm < 1400.0f && gb.gear > 1) {
        --gb.gear;
        gb.shiftTimer = 0.18f;
    }
    return gb.gear;
}
float wheelTorque(float engineTorqueNm, int gear, const std::array<float, 6>& ratios,
                  int gearCount, float finalDrive, float clutch) {
    if (gear < 1 || gear > gearCount) return 0.0f;
    return engineTorqueNm * ratios[static_cast<size_t>(gear - 1)] * finalDrive * clutch;
}
float absMultiplier(float brakeInput, float speedMs, float grip, float time) {
    if (brakeInput < 0.95f || speedMs < 5.0f) return 1.0f;
    // Hard braking: without ABS the wheels would lock on low grip. ABS
    // pulses the brakes ~8 Hz, keeping most of the force without a lockup.
    float lockRisk = (1.0f - grip) * brakeInput;
    if (lockRisk < 0.25f) return 1.0f;
    return 0.78f + 0.22f * std::fabs(std::sin(time * 50.0f));
}
float tractionMultiplier(float wheelSpin, bool enabled) {
    if (!enabled) return 1.0f;
    if (wheelSpin < 0.15f) return 1.0f;
    return clampf(1.0f - (wheelSpin - 0.15f) * 1.8f, 0.35f, 1.0f);
}
float aeroDrag(float speedMs, float dragCoeff, float frontalArea) {
    const float rho = 1.225f; // air density kg/m^3
    return 0.5f * rho * dragCoeff * frontalArea * speedMs * std::fabs(speedMs);
}
float downforce(float speedMs, float downforceCoeff) {
    return downforceCoeff * speedMs * speedMs;
}
float brakeFadeUpdate(float& tempC, float brakingEnergy, float dt) {
    tempC += brakingEnergy * 0.9f * dt;   // hard braking heats the discs
    tempC -= (tempC - 60.0f) * 0.05f * dt; // cools toward ambient-ish 60C
    if (tempC < 60.0f) tempC = 60.0f;
    if (tempC > 650.0f) tempC = 650.0f;
    // Fade: full bite cold, ~30% lost when the discs are cooking.
    return clampf(1.0f - (tempC - 350.0f) / 1000.0f, 0.70f, 1.0f);
}
float damagePowerFactor(float damage) { return clampf(1.0f - 0.45f * damage, 0.55f, 1.0f); }
float damageSteerFactor(float damage) { return clampf(1.0f - 0.35f * damage, 0.65f, 1.0f); }
} // namespace gc6
