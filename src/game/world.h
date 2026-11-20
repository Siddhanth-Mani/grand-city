#pragma once
#include "audio/audio.h"
#include "audio/radio.h"
#include <array>
#include "core/config.h"
#include "core/ecs.h"
#include "core/events.h"
#include "core/input.h"
#include "core/jobs.h"
#include "core/profiler.h"
#include "engine/resource_manager.h"
#include "engine/scene.h"
#include "game/car_data.h"
#include "game/city.h"
#include "game/collectibles.h"
#include "game/debug_overlay.h"
#include "game/economy.h"
#include "game/hud.h"
#include "game/menus.h"
#include "game/missions.h"
#include "game/pathfinding.h"
#include "game/pedestrian.h"
#include "game/player.h"
#include "game/savegame.h"
#include "game/story.h"
#include "game/traffic.h"
#include "game/wanted.h"
#include "game/weather.h"
#include "renderer/lights.h"
#include "renderer/material.h"
#include "renderer/post.h"
#include "renderer/renderer.h"
#include "renderer/shader_watcher.h"
#include "scripting/lua_binding.h"
namespace gc6 {
// The whole game: owns city, traffic, pedestrians, player, police, missions,
// story campaign, garage/economy, weather, audio, ECS + jobs, save/load,
// and the menu flow. Fixed update = physics, update = visuals/timers.
class World {
public:
    void init(const Config& config);
    // Release owned resources (closes the Lua VM and the audio engine).
    // Call before exit so leak checkers see a clean shutdown.
    void shutdown();
    void fixedUpdate(float dt, const InputState& input);
    void update(float dt, const InputState& input);
    void render(IRenderer& renderer);
    const HudData& hud() const { return hud_; }
    const City& city() const { return city_; }
    MissionManager& missions() { return missions_; }
    Wanted& wanted() { return wanted_; }
    float timeOfDay() const { return timeOfDay_; }
    EventBus& events() { return events_; }
    GameFlow& flow() { return flow_; }
    StoryCampaign& story() { return story_; }
    Garage& garage() { return garage_; }
    WeatherSystem& weather() { return weather_; }
    Radio& radio() { return radio_; }
    DebugOverlay& debugOverlay() { return debug_; }
    Profiler& profiler() { return Profiler::instance(); }
    const std::vector<CarDef>& catalogue() const { return catalogue_; }
    bool photoMode() const { return hud_.photoMode; }
    int cameraMode() const { return cameraMode_; }
    float playTimeMinutes() const { return playTimeMinutes_; }
    // --- game flow ---
    void newGame();
    bool save(const std::string& path);
    bool load(const std::string& path);
    bool hasSave(const std::string& path) const;
    void startMission(int index);
    void applyGarageCar();
    // Re-read [post]/[audio]/[assists]/[game] from a Config (Settings menu).
    void applySettings(const Config& config);
    void setMuted(bool m) { muted_ = m; audio_.setMuted(m); }
    bool isMuted() const { return muted_; }
    void cycleCamera() { cameraMode_ = (cameraMode_ + 1) % 3; hud_.cameraMode = cameraMode_; }
    void togglePhotoMode();
    void toggleRadio() { radio_.nextStation(); }
    JobSystem& jobs() { return jobs_; }
    Registry& registry() { return registry_; }
    const SideActivities& activities() const { return activities_; }
private:
    void rebuildScene();
    void syncECS();
    void updateLights();
    void updateAudio(float dt);
    void checkStuntRamps();
    void onMissionCompleted(int index, int stars);
    void onMissionFailed(int index);
    void pollHotReload();
    void drawCar(IRenderer& renderer, const Vehicle& v, const Vec3& colour,
                 const std::string& carClass, bool headlightsOn);
    Vec3 paintColour(int paintIndex, const std::string& baseColour) const;
    Config config_;
    City city_;
    Traffic traffic_;
    Pedestrians pedestrians_;
    Player player_;
    Police police_;
    Wanted wanted_;
    MissionManager missions_;
    LuaVM lua_;
    EventBus events_;
    ResourceManager resources_;
    Scene scene_;
    HudData hud_;
    float timeOfDay_ = 10.0f;
    float chaos_ = 0.0f;
    int money_ = 0;
    std::vector<Vehicle*> nearbyCars_;
    std::vector<Vehicle> parkedCars_;
    std::vector<int> parkedCatalogueIndex_; // catalogue index per parked car
    bool muted_ = false;
    // --- v2 systems ---
    JobSystem jobs_;
    Registry registry_;
    RoadGraph graph_;
    std::vector<CarDef> catalogue_;
    std::vector<Material> materials_;
    Garage garage_;
    WeatherSystem weather_;
    Radio radio_;
    AudioEngine audio_;
    StoryCampaign story_;
    GameFlow flow_;
    SideActivities activities_;
    DebugOverlay debug_;
    ShaderWatcher shaderWatcher_;
    ShaderWatcher luaWatcher_;
    LightRig lightRig_;
    PostSettings post_;
    int cameraMode_ = 0;
    float photoYaw_ = 0.0f, photoPitch_ = 0.4f, photoDist_ = 14.0f;
    float playTimeMinutes_ = 0.0f;
    float screenShake_ = 0.0f;
    float speedBefore_ = 0.0f;
    float lastAirTimeSeen_ = 0.0f;
    int activeMissionLua_ = -1;
    bool missionLuaLoaded_ = false;
    float radioStepTimer_ = 0.0f;
    int radioStep_ = 0;
    std::vector<Entity> trafficEntities_;
    std::vector<Entity> pedEntities_;
    Entity playerEntity_ = kNullEntity;
    std::array<int, 6> starRatings_{0, 0, 0, 0, 0, 0};
    float fpsTimer_ = 0.0f;
    int fpsFrames_ = 0;
};
} // namespace gc6
