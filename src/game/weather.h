#pragma once
#include <string>
namespace gc6 {
enum class Weather { Clear, Rain, Fog };
struct WeatherState {
    Weather current = Weather::Clear;
    float wetness = 0.0f;     // 0 = dry, 1 = soaked roads (rain)
    float fogDensity = 0.0f;  // 0 = clear view, 1 = heavy fog
    float rainIntensity = 0.0f;
};
// Weather changes how the world drives and looks: rain wets the roads
// (grip multiplier ~0.7 on wet tarmac), fog cuts visibility, clear is dry.
// Selectable from Settings and per-mission; changes fade in over ~5s.
class WeatherSystem {
public:
    void set(Weather w);
    void update(float dt);
    const WeatherState& state() const { return state_; }
    // Grip multiplier applied to the surface grip (1.0 = dry).
    float gripMultiplier() const { return 1.0f - 0.3f * state_.wetness; }
    static const char* name(Weather w);
    static Weather fromString(const std::string& s);
private:
    WeatherState state_;
    Weather target_ = Weather::Clear;
};
} // namespace gc6
