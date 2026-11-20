#pragma once
#include "core/math.h"
#include <string>
#include <vector>
namespace gc6 {
// Plain data for the HUD/minimap — the renderer draws it; this is the model.
struct HudData {
    float speedKmh = 0.0f;
    int money = 0, health = 100, wantedStars = 0;
    float timeOfDay = 12.0f; // hours, 0..24
    bool night = false;
    std::string objective;
    float missionTimeLeft = 0.0f;
    Vec3 playerPos{0, 0, 0};
    Vec3 missionTarget{0, 0, 0};
    std::vector<Vec3> trafficPositions;
    std::vector<Vec3> policePositions;
    // --- finished-game HUD fields ---
    bool onFoot = true;          // walking (no speedometer, show the enter-car hint)
    int gear = 1;
    float rpm = 0.0f;
    float redlineRpm = 6500.0f;
    int packagesFound = 0;       // of 50
    int packagesTotal = 50;
    int stuntScore = 0;
    int bestStunt = 0;
    float damage = 0.0f;         // 0..1, drives smoke/shake
    bool smoking = false;        // damage > 0.6: engine smoke
    float screenShake = 0.0f;    // 0..1, decays; set on hard hits
    std::string weatherName = "Clear";
    std::string radioStation = "City FM";
    bool photoMode = false;      // HUD hidden, time frozen, free camera
    int cameraMode = 0;          // 0 chase, 1 hood, 2 cockpit
    std::vector<std::string> messages; // feed, newest last (max 5)
    std::vector<Vec3> packagePositions; // minimap dots (untaken only)
    std::vector<Vec3> rampPositions;    // minimap dots
    std::vector<Vec3> roadblockPositions;
    void pushMessage(const std::string& m) {
        messages.push_back(m);
        if (messages.size() > 5) messages.erase(messages.begin());
    }
};
} // namespace gc6
