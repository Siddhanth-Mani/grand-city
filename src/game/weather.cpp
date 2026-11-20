#include "game/weather.h"
#include <algorithm>
namespace gc6 {
const char* WeatherSystem::name(Weather w) {
    switch (w) {
        case Weather::Rain: return "Rain";
        case Weather::Fog: return "Fog";
        default: return "Clear";
    }
}
Weather WeatherSystem::fromString(const std::string& s) {
    if (s == "rain") return Weather::Rain;
    if (s == "fog") return Weather::Fog;
    return Weather::Clear;
}
void WeatherSystem::set(Weather w) { target_ = w; }
void WeatherSystem::update(float dt) {
    // Fade toward the target weather over ~5 seconds.
    float targetWet = target_ == Weather::Rain ? 1.0f : 0.0f;
    float targetFog = target_ == Weather::Fog ? 0.8f : 0.0f;
    float targetRain = target_ == Weather::Rain ? 1.0f : 0.0f;
    float rate = dt / 5.0f;
    state_.wetness += std::clamp(targetWet - state_.wetness, -rate, rate);
    state_.fogDensity += std::clamp(targetFog - state_.fogDensity, -rate, rate);
    state_.rainIntensity += std::clamp(targetRain - state_.rainIntensity, -rate, rate);
    state_.current = target_;
}
} // namespace gc6
