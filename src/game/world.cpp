#include "game/world.h"
#include "core/logger.h"
#include <algorithm>
#include <cmath>
#include <fstream>
namespace gc6 {
namespace {
Vec3 colourFromName(const std::string& name) {
    if (name == "orange") return Vec3(0.9f, 0.35f, 0.05f);
    if (name == "red") return Vec3(0.75f, 0.05f, 0.05f);
    if (name == "black") return Vec3(0.03f, 0.03f, 0.03f);
    if (name == "blue") return Vec3(0.05f, 0.2f, 0.7f);
    if (name == "green") return Vec3(0.1f, 0.5f, 0.1f);
    if (name == "yellow") return Vec3(0.9f, 0.8f, 0.1f);
    return Vec3(0.6f, 0.6f, 0.6f);
}
CarSoundType soundTypeFor(const std::string& carClass) {
    if (carClass == "supercar") return CarSoundType::Supercar;
    if (carClass == "sports") return CarSoundType::Sports;
    if (carClass == "muscle") return CarSoundType::Muscle;
    if (carClass == "suv") return CarSoundType::SUV;
    if (carClass == "pickup") return CarSoundType::Pickup;
    return CarSoundType::Sedan;
}
} // namespace
void World::init(const Config& config) {
    config_ = config;
    int seed = config.getInt("world", "seed", 42);
    city_.generate(seed);
    // Jobs: road graph, side activities and police routing run in parallel
    // (each writes disjoint state; the main thread merges by waiting).
    jobs_.submit([this] { graph_.build(city_); });
    jobs_.submit([this, seed] { activities_.init(seed); });
    jobs_.submit([this] { police_.init(city_); });
    jobs_.waitAll();
    traffic_.init(city_, config.getInt("world", "trafficCars", 14));
    pedestrians_.init(city_, config.getInt("world", "pedestrians", 24));
    player_.init(city_);
    missions_.loadDefinitions("data");
    lua_.init();
    lua_.loadMission("scripts/missions/mission1_delivery.lua");
    catalogue_ = loadCarCatalogue("data/cars.json");
    materials_ = loadMaterials("data/materials.json");
    garage_.init(catalogue_, config.getInt("game", "startingMoney", 0));
    if (config.getBool("game", "wetRoads", false)) weather_.set(Weather::Rain);
    post_.bloom = config.getBool("post", "bloom", true);
    post_.toneMappingACES = config.getBool("post", "tonemapping", true);
    post_.vignette = config.getBool("post", "vignette", true);
    post_.fxaa = config.getBool("post", "fxaa", true);
    post_.exposure = config.getFloat("post", "exposure", 1.1f);
    post_.msaa = config.getBool("renderer", "msaa", false);
    post_.msaaSamples = config.getInt("renderer", "msaaSamples", 4);
    audio_.init();
    audio_.setVolume(config.getFloat("audio", "volume", 0.8f));
    audio_.setMuted(config.getBool("audio", "muted", false));
    debug_.enabled = config.getBool("debug", "overlay", false);
    debug_.wireframe = config.getBool("debug", "wireframe", false);
    Profiler::instance().enabled = config.getBool("debug", "profile", true);
    if (config.getBool("debug", "shaderHotReload", true)) {
        shaderWatcher_.watch("shaders/lit_pbr.hlsl");
        shaderWatcher_.watch("shaders/post.hlsl");
        shaderWatcher_.watch("shaders/lit.hlsl");
    }
    // Parked cars the player can take: the current garage car + two more.
    Assists assists{config.getBool("assists", "abs", true),
                    config.getBool("assists", "tractionControl", true),
                    config.getBool("assists", "stabilityControl", true)};
    auto addParked = [&](int catalogueIndex, Vec3 pos) {
        if (catalogueIndex < 0 || catalogueIndex >= static_cast<int>(catalogue_.size())) return;
        CarDef def = (catalogueIndex == garage_.currentCar()) ? garage_.upgradedParams(catalogueIndex)
                                                              : catalogue_[static_cast<size_t>(catalogueIndex)];
        Vehicle v(def.params);
        v.setAssists(assists);
        v.setPosition(pos, 0.0f);
        parkedCars_.push_back(std::move(v));
        parkedCatalogueIndex_.push_back(catalogueIndex);
    };
    addParked(garage_.currentCar(), Vec3(4, 0, 10));
    addParked(1, Vec3(-4, 0, 10));
    addParked(5, Vec3(12, 0, 10));
    applyGarageCar();
    // ECS entities: player + traffic + pedestrians (police are added/removed
    // as the wanted level changes — see syncECS).
    playerEntity_ = registry_.create();
    registry_.add(playerEntity_, Transform{player_.position(), player_.yaw()});
    for (const auto& c : traffic_.cars()) {
        Entity e = registry_.create();
        registry_.add(e, Transform{c.vehicle.position(), c.vehicle.yaw()});
        registry_.add(e, VehicleComp{c.vehicle, c.nodeIndex, c.targetSpeed, 0, 0, 0, false});
        registry_.add(e, RenderComp{Vec3(0.7f, 0.7f, 0.75f), 0});
        registry_.add(e, AIComp{});
        trafficEntities_.push_back(e);
    }
    for (const auto& p : pedestrians_.all()) {
        Entity e = registry_.create();
        registry_.add(e, Transform{p.position, 0.0f});
        registry_.add(e, PedComp{p.waypoint, p.speed, false});
        registry_.add(e, RenderComp{Vec3(0.9f, 0.8f, 0.6f), 1});
        pedEntities_.push_back(e);
    }
    Logger::info("World initialised: " + std::to_string(city_.buildings.size()) + " buildings, " +
                 std::to_string(city_.roadNodes.size()) + " road nodes, " +
                 std::to_string(catalogue_.size()) + " cars, " +
                 std::to_string(activities_.items().size()) + " packages, " +
                 std::to_string(activities_.jumps().size()) + " stunt jumps");
    rebuildScene();
}
void World::shutdown() {
    lua_.shutdown();
    audio_.shutdown();
    Profiler::instance().dump("profile.log");
}
void World::applyGarageCar() {
    if (catalogue_.empty()) return;
    CarDef def = garage_.upgradedParams(garage_.currentCar());
    Assists assists{config_.getBool("assists", "abs", true),
                    config_.getBool("assists", "tractionControl", true),
                    config_.getBool("assists", "stabilityControl", true)};
    player_.setVehicleParams(def.params);
    if (player_.ownedVehicle()) player_.ownedVehicle()->setAssists(assists);
    if (!parkedCars_.empty()) {
        parkedCars_[0] = Vehicle(def.params);
        parkedCars_[0].setAssists(assists);
        parkedCars_[0].setPosition(Vec3(4, 0, 10), 0.0f);
        parkedCatalogueIndex_[0] = garage_.currentCar();
    }
}
void World::applySettings(const Config& config) {
    config_ = config;
    post_.bloom = config.getBool("post", "bloom", true);
    post_.toneMappingACES = config.getBool("post", "tonemapping", true);
    post_.vignette = config.getBool("post", "vignette", true);
    post_.fxaa = config.getBool("post", "fxaa", true);
    post_.exposure = config.getFloat("post", "exposure", 1.1f);
    audio_.setVolume(config.getFloat("audio", "volume", 0.8f));
    audio_.setMuted(config.getBool("audio", "muted", false));
    muted_ = config.getBool("audio", "muted", false);
    weather_.set(WeatherSystem::fromString(config.getString("game", "weather", "clear")));
    Assists assists{config.getBool("assists", "abs", true),
                    config.getBool("assists", "tractionControl", true),
                    config.getBool("assists", "stabilityControl", true)};
    if (player_.ownedVehicle()) player_.ownedVehicle()->setAssists(assists);
    for (auto& car : parkedCars_) car.setAssists(assists);
}
void World::newGame() {
    garage_.init(catalogue_, 0);
    money_ = 0;
    playTimeMinutes_ = 0.0f;
    timeOfDay_ = clampf(config_.getFloat("world", "startHour", 10.0f), 0.0f, 23.99f);
    wanted_.setHeat(0.0f);
    activities_.init(config_.getInt("world", "seed", 42));
    applyGarageCar();
    player_.init(city_);
    flow_.setState(MenuState::Playing);
    hud_.pushMessage(story_.introText());
}
bool World::save(const std::string& path) {
    SaveData d;
    d.playerPos = player_.position();
    d.money = garage_.money();
    garage_.fillSave(d.carsOwned, d.currentCar, d.paintIndex);
    garage_.fillUpgrades(d.upgrades);
    for (size_t i = 0; i < missions_.missions().size() && i < 6; ++i) {
        d.missionsCompleted[i] = missions_.missions()[i].state == MissionState::Completed;
        d.starRatings[i] = starRatings_[i];
    }
    d.wantedHeat = wanted_.heat();
    d.timeOfDay = timeOfDay_;
    d.collectiblesFound = activities_.collected();
    d.bestStuntScore = activities_.bestStunt();
    d.playTimeMinutes = playTimeMinutes_;
    return saveGame(path, d);
}
bool World::load(const std::string& path) {
    SaveData d;
    if (!loadGame(path, d)) return false;
    garage_.applySave(d.carsOwned, d.currentCar, d.paintIndex, d.money);
    garage_.applyUpgrades(d.upgrades);
    starRatings_ = d.starRatings;
    for (size_t i = 0; i < 6; ++i)
        if (d.missionsCompleted[i]) missions_.setMissionState(static_cast<int>(i), MissionState::Completed);
    money_ = d.money;
    timeOfDay_ = d.timeOfDay;
    wanted_.setHeat(d.wantedHeat);
    playTimeMinutes_ = d.playTimeMinutes;
    applyGarageCar();
    player_.init(city_);
    flow_.setState(MenuState::Playing);
    return true;
}
bool World::hasSave(const std::string& path) const {
    std::ifstream in(path);
    return in.good();
}
void World::startMission(int index) {
    if (index < 0 || index >= static_cast<int>(missions_.missions().size())) return;
    if (!missions_.startMission(index)) return;
    activeMissionLua_ = index;
    lua_.resetGameState();
    const MissionDef& def = missions_.missions()[static_cast<size_t>(index)].def;
    if (lua_.loadMission(def.luaFile)) {
        missionLuaLoaded_ = true;
        lua_.callFunction("onStart", 0.0f);
        luaWatcher_.watch(def.luaFile);
    }
    flow_.setState(MenuState::Playing);
    hud_.pushMessage("Mission started: " + def.name);
}
void World::togglePhotoMode() {
    hud_.photoMode = !hud_.photoMode;
    if (hud_.photoMode) {
        photoYaw_ = player_.yaw();
        hud_.pushMessage("Photo mode: arrows orbit the camera, V to exit");
    }
}
void World::fixedUpdate(float dt, const InputState& input) {
    if (hud_.photoMode) return; // time is frozen in photo mode
    PROFILE_SCOPE("world.fixed");
    playTimeMinutes_ += dt / 60.0f;
    timeOfDay_ = std::fmod(timeOfDay_ + dt * (24.0f / 600.0f), 24.0f);
    weather_.update(dt);
    for (auto& light : city_.lights) {
        light.timer += dt;
        if (light.timer > 8.0f) { light.timer = 0.0f; light.green = !light.green; }
    }
    // Traffic AI decisions run across the job pool; physics applies them.
    {
        PROFILE_SCOPE("traffic.ai");
        jobs_.parallelFor(traffic_.cars().size(), [this, dt](size_t i) { traffic_.aiDecide(i, dt); });
    }
    {
        PROFILE_SCOPE("traffic.physics");
        for (size_t i = 0; i < traffic_.cars().size(); ++i)
            traffic_.physicsStep(i, dt, city_.solids);
    }
    nearbyCars_.clear();
    for (auto& car : parkedCars_) nearbyCars_.push_back(&car);
    for (auto& car : traffic_.cars()) nearbyCars_.push_back(const_cast<Vehicle*>(&car.vehicle));
    speedBefore_ = player_.currentVehicle() ? player_.currentVehicle()->speed() : 0.0f;
    bool wet = weather_.state().wetness > 0.3f;
    if (player_.currentVehicle()) player_.currentVehicle()->setWet(wet);
    if (player_.ownedVehicle()) player_.ownedVehicle()->setWet(wet);
    player_.update(dt, input, city_.solids, nearbyCars_);
    // Car-car collisions (OBB) between the player and traffic.
    if (player_.currentVehicle()) {
        for (auto& car : traffic_.cars())
            player_.currentVehicle()->collideWith(const_cast<Vehicle&>(car.vehicle));
    }
    float chaos = wanted_.heat() / 100.0f;
    std::vector<Vec3> carPos, carFwd;
    for (const auto& c : traffic_.cars()) {
        carPos.push_back(c.vehicle.position());
        carFwd.push_back(Vec3(std::sin(c.vehicle.yaw()), 0, std::cos(c.vehicle.yaw())));
    }
    if (player_.currentVehicle()) {
        carPos.push_back(player_.currentVehicle()->position());
        carFwd.push_back(Vec3(std::sin(player_.currentVehicle()->yaw()), 0, std::cos(player_.currentVehicle()->yaw())));
    }
    pedestrians_.update(dt, player_.position(), chaos, carPos, carFwd);
    bool policeNearby = police_.anyNearby(player_.position(), 40.0f);
    wanted_.update(dt, policeNearby, chaos > 0.1f);
    police_.update(dt, player_.position(), wanted_.stars(), city_.solids);
    missions_.update(dt, player_.position());
    // Lua mission logic (taxi / escape-time / stunt are Lua-driven).
    if (missionLuaLoaded_ && activeMissionLua_ >= 0) {
        auto& g = lua_.gameState();
        g.playerPos = player_.position();
        if (g.timerSet) g.timeLeft -= dt;
        lua_.callFunction("onUpdate", dt);
        if (g.wantedLevel >= 0) { wanted_.setHeat(static_cast<float>(g.wantedLevel) * 20.0f); g.wantedLevel = -1; }
        if (g.moneyDelta != 0) { garage_.addMoney(g.moneyDelta); g.moneyDelta = 0; }
        if (g.completed) {
            Mission* m = missions_.active();
            int stars = StoryCampaign::starRating(g.timeLeft,
                m ? m->def.timeLimit : 120.0f,
                player_.currentVehicle() ? player_.currentVehicle()->damage() : 0.0f);
            onMissionCompleted(activeMissionLua_, stars);
            activeMissionLua_ = -1;
        } else if (g.failed) {
            onMissionFailed(activeMissionLua_);
            activeMissionLua_ = -1;
        }
    }
    // Collectibles + stunt jumps.
    {
        int before = activities_.collected();
        int earned = activities_.update(player_.position(), !player_.onFoot());
        if (activities_.collected() > before) {
            garage_.addMoney(earned);
            audio_.pickup(player_.position());
            hud_.pushMessage("Package found! +$" + std::to_string(earned) + " (" +
                             std::to_string(activities_.collected()) + "/50)");
            missions_.onEvent("package_collected", player_.position());
        }
    }
    checkStuntRamps();
    if (player_.currentVehicle() && player_.currentVehicle()->crashed()) {
        float impact = std::fabs(speedBefore_ - player_.currentVehicle()->speed());
        screenShake_ = std::min(1.0f, impact / 20.0f);
        wanted_.addHeat(8.0f);
        events_.emit({EventType::CarCrashed, "Crash", 8});
        audio_.crashSound(std::min(1.0f, impact / 25.0f), player_.position());
    }
    if (player_.currentVehicle()) {
        float rpm = player_.currentVehicle()->rpm();
        float redline = player_.currentVehicle()->params().redlineRpm;
        if (rpm > redline * 0.98f && player_.currentVehicle()->speed() > 5.0f)
            audio_.backfire(player_.position());
    }
    money_ = garage_.money(); // garage is the single source of truth for money
    chaos_ = chaos;
    syncECS();
    updateLights();
    updateAudio(dt);
    screenShake_ = std::max(0.0f, screenShake_ - dt * 1.5f);
    pollHotReload();
}
void World::checkStuntRamps() {
    Vehicle* v = player_.currentVehicle();
    if (!v) return;
    float launch = activities_.checkRamp(v->position(), std::fabs(v->speed()));
    if (launch > 0.0f) v->launch(launch);
    if (v->lastAirTime() > lastAirTimeSeen_ + 0.01f) {
        float air = v->lastAirTime();
        lastAirTimeSeen_ = air;
        int before = activities_.stuntScore();
        activities_.onLanded(air, std::fabs(v->speed()));
        int points = activities_.stuntScore() - before;
        if (points > 0) {
            garage_.addMoney(points / 10);
            hud_.pushMessage("Stunt jump! " + std::to_string(points) + " points");
        }
        if (missionLuaLoaded_) lua_.callStuntJump(air);
    }
}
void World::onMissionCompleted(int index, int stars) {
    Mission* m = missions_.active();
    if (index >= 0 && index < 6) starRatings_[static_cast<size_t>(index)] = stars;
    if (m) {
        m->state = MissionState::Completed;
        garage_.addMoney(m->def.rewardMoney);
        hud_.pushMessage("Mission complete: " + m->def.name + " (+$" + std::to_string(m->def.rewardMoney) +
                         ", " + std::to_string(stars) + " stars)");
    }
    const StoryMission& sm = story_.mission(index);
    if (!sm.unlockCar.empty())
        hud_.pushMessage("Unlocked car: " + sm.unlockCar);
    flow_.setMissionResult(m ? m->def.name : "Mission", stars,
                           m ? m->def.rewardMoney : 0, true);
    // All six done -> game complete screen with stats and credits.
    int done = 0;
    for (const auto& mm : missions_.missions())
        if (mm.state == MissionState::Completed) ++done;
    if (done >= 6)
        flow_.setFinalStats(done, garage_.money(), activities_.collected(),
                            activities_.bestStunt(), playTimeMinutes_);
    missionLuaLoaded_ = false;
}
void World::onMissionFailed(int index) {
    (void)index;
    Mission* m = missions_.active();
    if (m) m->state = MissionState::Failed;
    flow_.setMissionResult(m ? m->def.name : "Mission", 0, 0, false);
    missionLuaLoaded_ = false;
}
void World::syncECS() {
    if (auto* t = registry_.get<Transform>(playerEntity_)) {
        t->position = player_.position();
        t->yaw = player_.yaw();
    }
    for (size_t i = 0; i < traffic_.cars().size() && i < trafficEntities_.size(); ++i) {
        Entity e = trafficEntities_[i];
        if (auto* t = registry_.get<Transform>(e)) {
            t->position = traffic_.cars()[i].vehicle.position();
            t->yaw = traffic_.cars()[i].vehicle.yaw();
        }
    }
    for (size_t i = 0; i < pedestrians_.all().size() && i < pedEntities_.size(); ++i) {
        Entity e = pedEntities_[i];
        if (auto* t = registry_.get<Transform>(e)) t->position = pedestrians_.all()[i].position;
    }
}
void World::updateLights() {
    lightRig_.clear();
    Vec3 sunDir, sunColour;
    float sunIntensity = 1.0f;
    {
        float dayFrac = (timeOfDay_ - 6.0f) / 13.0f;
        if (dayFrac > 0.0f && dayFrac < 1.0f) {
            float a = dayFrac * 3.14159265f;
            sunDir = normalize(Vec3(-std::cos(a), std::sin(a) * 1.15f, 0.42f));
            sunIntensity = sunDir.y > 0.05f ? 1.0f : 0.4f;
        } else {
            sunDir = normalize(Vec3(0.35f, 0.75f, -0.45f));
            sunIntensity = 0.15f;
        }
        sunColour = Vec3(1.0f, 0.9f, 0.8f);
    }
    lightRig_.sunDir = sunDir;
    lightRig_.sunColour = sunColour;
    lightRig_.sunIntensity = sunIntensity;
    bool dark = timeOfDay_ > 18.4f || timeOfDay_ < 6.6f;
    Vec3 focus = player_.position();
    // Police light bars first: they matter for gameplay at any time of day.
    float t = playTimeMinutes_ * 60.0f;
    for (const auto& c : police_.cars()) {
        if (lightRig_.lights.size() >= 4) break;
        if (length(c.position - focus) > 120.0f) continue;
        DynamicLight l;
        l.position = c.position + Vec3(0, 1.8f, 0);
        l.colour = std::fmod(t, 0.5f) < 0.25f ? Vec3(1.0f, 0.1f, 0.05f) : Vec3(0.15f, 0.3f, 1.0f);
        l.radius = 16.0f; l.intensity = 2.5f; l.isPolice = true;
        lightRig_.add(l);
    }
    if (!dark) return;
    // Headlights: a pool of light on the road ahead of each car.
    auto addHeadlight = [&](const Vehicle& v, float strength) {
        Vec3 fwd{std::sin(v.yaw()), 0, std::cos(v.yaw())};
        DynamicLight l;
        l.position = v.position() + fwd * 7.0f + Vec3(0, 1.2f, 0);
        l.colour = Vec3(1.0f, 0.95f, 0.85f);
        l.radius = 16.0f; l.intensity = strength; l.isHeadlight = true;
        lightRig_.add(l);
    };
    if (player_.currentVehicle()) addHeadlight(*player_.currentVehicle(), 3.0f);
    // The nearest streetlamps.
    std::vector<std::pair<float, Vec3>> lamps;
    for (const auto& p : city_.lamps) {
        float d = length(p - focus);
        if (d < 140.0f) lamps.push_back({d, p});
    }
    std::sort(lamps.begin(), lamps.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& lp : lamps) {
        if (lightRig_.lights.size() >= 13) break;
        DynamicLight l;
        l.position = lp.second + Vec3(0, City::kKerb - 0.3f, 0);
        l.colour = Vec3(1.0f, 0.80f, 0.58f);
        l.radius = 19.0f; l.intensity = 6.0f;
        lightRig_.add(l);
    }
    // Traffic headlights nearest the player fill what is left.
    std::vector<std::pair<float, const Vehicle*>> cars;
    for (const auto& c : traffic_.cars()) cars.push_back({length(c.vehicle.position() - focus), &c.vehicle});
    std::sort(cars.begin(), cars.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& c : cars) {
        if (lightRig_.lights.size() >= LightRig::kMaxLights) break;
        if (c.first > 90.0f) break;
        addHeadlight(*c.second, 1.6f);
    }
}
void World::updateAudio(float dt) {
    if (!audio_.ready()) return;
    Vec3 pos = player_.position();
    Vec3 fwd{std::sin(player_.yaw()), 0, std::cos(player_.yaw())};
    audio_.setListener(pos, fwd);
    if (player_.currentVehicle()) {
        const Vehicle& v = *player_.currentVehicle();
        std::string cls = v.params().carClass;
        audio_.engineLoop(0, v.rpm(), 0.7f, soundTypeFor(cls), v.position());
        audio_.skid(v.wheelSpin(), v.position());
    } else {
        audio_.engineStop(0);
    }
    audio_.siren(police_.anyNearby(pos, 60.0f), pos);
    // Radio: one note per 16th-note step at the station's BPM.
    radioStepTimer_ += dt;
    float stepLen = 60.0f / static_cast<float>(radio_.current().bpm) / 4.0f;
    if (radioStepTimer_ >= stepLen) {
        radioStepTimer_ -= stepLen;
        ++radioStep_;
        float note = radio_.noteAt(radioStep_);
        float bass = radio_.bassAt(radioStep_);
        if (note > 0.0f) audio_.tone(note, 0.10f);
        if (bass > 0.0f) audio_.tone(bass, 0.12f);
    }
    audio_.update(dt);
}
void World::pollHotReload() {
    for (const auto& path : shaderWatcher_.poll())
        Logger::info("Shader changed: " + path + " — reload requested (backend recompiles on next present)");
    for (const auto& path : luaWatcher_.poll()) {
        Logger::info("Lua mission changed: " + path + " — reloading");
        if (activeMissionLua_ >= 0 && lua_.loadMission(path))
            lua_.callFunction("onStart", 0.0f);
    }
}
void World::update(float dt, const InputState& input) {
    // Frame-rate accounting for the debug overlay.
    fpsTimer_ += dt;
    ++fpsFrames_;
    if (fpsTimer_ >= 0.5f) {
        debug_.fps = static_cast<float>(fpsFrames_) / fpsTimer_;
        debug_.frameMs = 1000.0f * fpsTimer_ / static_cast<float>(fpsFrames_);
        fpsTimer_ = 0.0f;
        fpsFrames_ = 0;
    }
    // F cycles the camera (Shift+F toggles the debug overlay), P is photo
    // mode, R cycles the radio, M mutes. (The input enum has no C/V keys.)
    if (input.wasPressed(Key_F)) {
        if (input.isDown(Key_Shift)) debug_.enabled = !debug_.enabled;
        else cycleCamera();
    }
    if (input.wasPressed(Key_P)) togglePhotoMode();
    if (input.wasPressed(Key_R)) toggleRadio();
    if (input.wasPressed(Key_M)) { muted_ = !muted_; audio_.setMuted(muted_); }
    if (hud_.photoMode) {
        if (input.isDown(Key_Left)) photoYaw_ -= dt * 0.8f;
        if (input.isDown(Key_Right)) photoYaw_ += dt * 0.8f;
        if (input.isDown(Key_Up)) photoPitch_ = std::min(1.2f, photoPitch_ + dt * 0.5f);
        if (input.isDown(Key_Down)) photoPitch_ = std::max(0.05f, photoPitch_ - dt * 0.5f);
    }
    // Story missions start from the mission-select screen (GameFlow), not
    // from in-game keys — the input enum has no digit keys.
    hud_.speedKmh = player_.speedKmh();
    hud_.onFoot = player_.onFoot();
    hud_.money = garage_.money();
    hud_.health = player_.health();
    hud_.wantedStars = wanted_.stars();
    hud_.timeOfDay = timeOfDay_;
    hud_.night = timeOfDay_ < 6.0f || timeOfDay_ > 19.0f;
    hud_.playerPos = player_.position();
    hud_.packagesFound = activities_.collected();
    hud_.stuntScore = activities_.stuntScore();
    hud_.bestStunt = activities_.bestStunt();
    hud_.weatherName = WeatherSystem::name(weather_.state().current);
    hud_.radioStation = radio_.stationName();
    hud_.cameraMode = cameraMode_;
    if (player_.currentVehicle()) {
        const Vehicle& v = *player_.currentVehicle();
        hud_.gear = v.gear();
        hud_.rpm = v.rpm();
        hud_.redlineRpm = v.params().redlineRpm;
        hud_.damage = v.damage();
        hud_.smoking = v.isSmoking();
    } else {
        hud_.gear = 0;
        hud_.rpm = 0.0f;
        hud_.damage = 0.0f;
        hud_.smoking = false;
    }
    hud_.screenShake = screenShake_;
    if (auto* m = missions_.active()) {
        hud_.objective = m->def.name + ": " + m->def.description;
        hud_.missionTimeLeft = m->timeLeft;
        hud_.missionTarget = m->target;
    } else {
        hud_.objective = "Free Roam — Esc, then Save & Quit to Menu, for the Garage and Missions";
        hud_.missionTimeLeft = 0.0f;
    }
    hud_.trafficPositions.clear();
    for (const auto& c : traffic_.cars()) hud_.trafficPositions.push_back(c.vehicle.position());
    hud_.policePositions.clear();
    for (const auto& c : police_.cars()) hud_.policePositions.push_back(c.position);
    hud_.roadblockPositions = police_.roadblockPositions();
    hud_.packagePositions.clear();
    for (const auto& item : activities_.items())
        if (!item.taken) hud_.packagePositions.push_back(item.position);
    hud_.rampPositions.clear();
    for (const auto& j : activities_.jumps()) hud_.rampPositions.push_back(j.position);
}
void World::rebuildScene() {
    // The city is drawn straight from city_ in drawCity() (with culling and
    // per-building materials); the flat scene list is kept for tools/tests.
    scene_.clear();
    const MeshData& ground = resources_.groundPlane(3000.0f);
    scene_.add("ground", &ground, Mat4(1.0f), Vec3(0.16f, 0.16f, 0.18f));
    const MeshData& box = resources_.box(1, 1, 1);
    for (const auto& b : city_.buildings) {
        Vec3 centre = (b.bounds.min + b.bounds.max) * 0.5f;
        Vec3 size = b.bounds.max - b.bounds.min;
        Mat4 model = translate(Mat4(1.0f), Vec3(centre.x, size.y / 2, centre.z));
        model = scale(model, size);
        SceneEntry& e = scene_.add("building", &box, model, b.colour);
        e.visible = false;
    }
}
Vec3 World::paintColour(int paintIndex, const std::string& baseColour) const {
    switch (paintIndex) {
        case 1: return Vec3(0.75f, 0.05f, 0.05f);
        case 2: return Vec3(0.05f, 0.2f, 0.7f);
        case 3: return Vec3(0.1f, 0.5f, 0.1f);
        case 4: return Vec3(0.03f, 0.03f, 0.03f);
        case 5: return Vec3(0.95f, 0.95f, 0.95f);
        case 6: return Vec3(0.5f, 0.1f, 0.7f);
        default: return colourFromName(baseColour);
    }
}
namespace {
// Sun (day) or moon (night) for an hour of the day: direction towards the
// light, its colour and intensity. Sunrise 06:00, sunset 19:00.
void skyLight(float hour, Vec3& dir, Vec3& colour, float& intensity) {
    float dayFrac = (hour - 6.0f) / 13.0f;
    if (dayFrac > 0.0f && dayFrac < 1.0f) {
        float a = dayFrac * 3.14159265f;
        dir = normalize(Vec3(-std::cos(a), std::sin(a) * 1.15f, 0.42f));
        float warm = 1.0f - clampf((dir.y - 0.04f) / 0.40f, 0.0f, 1.0f);
        colour = Vec3(1.0f, 0.96f - 0.36f * warm, 0.90f - 0.58f * warm);
        float t = clampf((dir.y + 0.02f) / 0.22f, 0.0f, 1.0f);
        intensity = 0.15f + 0.85f * t * t * (3.0f - 2.0f * t);
    } else {
        dir = normalize(Vec3(0.35f, 0.75f, -0.45f));
        colour = Vec3(0.55f, 0.65f, 0.95f);
        intensity = 0.15f;
    }
}
float hashf(float a, float b) {
    float v = std::sin(a * 12.9898f + b * 78.233f) * 43758.5453f;
    return v - std::floor(v);
}
Material mat(const Vec3& albedo, float rough, float metal = 0.0f, int shading = kShadeStandard) {
    Material m;
    m.albedo = albedo;
    m.roughness = rough;
    m.metalness = metal;
    m.shading = shading;
    return m;
}
Material glow(const Vec3& colour, float strength) {
    Material m = mat(colour, 0.3f, 0.0f, kShadeEmissive);
    m.emissive = colour * strength;
    return m;
}
// Real-world car colour mix: white, black, greys and silver dominate.
Vec3 trafficPaint(size_t i) {
    static const Vec3 palette[] = {
        {0.92f, 0.92f, 0.93f}, {0.03f, 0.03f, 0.035f}, {0.42f, 0.43f, 0.45f}, {0.70f, 0.71f, 0.73f},
        {0.92f, 0.92f, 0.93f}, {0.06f, 0.10f, 0.25f}, {0.55f, 0.04f, 0.04f}, {0.03f, 0.03f, 0.035f},
        {0.25f, 0.26f, 0.28f}, {0.62f, 0.58f, 0.50f}, {0.08f, 0.20f, 0.14f}, {0.70f, 0.71f, 0.73f},
        {0.10f, 0.28f, 0.55f}, {0.92f, 0.92f, 0.93f}};
    return palette[i % (sizeof(palette) / sizeof(palette[0]))];
}
const char* trafficClass(size_t i) {
    static const char* classes[] = {"sedan", "hatch", "suv", "sedan", "van", "hatch", "pickup", "suv", "sedan", "sports"};
    return classes[i % (sizeof(classes) / sizeof(classes[0]))];
}
struct PersonLook { Vec3 skin, top, legs, hair, shoes; };
PersonLook lookFor(size_t i) {
    static const Vec3 skins[] = {{0.87f, 0.68f, 0.55f}, {0.62f, 0.43f, 0.31f}, {0.42f, 0.28f, 0.19f},
                                 {0.93f, 0.77f, 0.64f}, {0.30f, 0.20f, 0.14f}, {0.75f, 0.55f, 0.40f}};
    static const Vec3 tops[] = {{0.08f, 0.10f, 0.16f}, {0.75f, 0.74f, 0.72f}, {0.45f, 0.08f, 0.08f},
                                {0.12f, 0.30f, 0.45f}, {0.20f, 0.30f, 0.18f}, {0.05f, 0.05f, 0.05f},
                                {0.60f, 0.48f, 0.30f}, {0.85f, 0.85f, 0.88f}, {0.35f, 0.18f, 0.40f},
                                {0.80f, 0.55f, 0.10f}};
    static const Vec3 legs[] = {{0.08f, 0.10f, 0.18f}, {0.05f, 0.05f, 0.05f}, {0.30f, 0.28f, 0.24f},
                                {0.12f, 0.17f, 0.30f}, {0.45f, 0.40f, 0.32f}, {0.20f, 0.20f, 0.22f}};
    static const Vec3 hair[] = {{0.04f, 0.03f, 0.02f}, {0.20f, 0.12f, 0.06f}, {0.45f, 0.32f, 0.16f},
                                {0.10f, 0.08f, 0.06f}, {0.55f, 0.52f, 0.50f}};
    return {skins[i % 6], tops[(i * 7 + 3) % 10], legs[(i * 5 + 1) % 6], hair[(i * 3 + 2) % 5],
            (i % 3) ? Vec3(0.04f, 0.04f, 0.045f) : Vec3(0.85f, 0.85f, 0.85f)};
}
} // namespace

void World::drawCar(IRenderer& renderer, const Vehicle& v, const Vec3& colour,
                    const std::string& carClass, bool headlightsOn) {
    drawCarStyled(renderer, v, colour, carClass, headlightsOn, false, true);
}
void World::drawCarStyled(IRenderer& renderer, const Vehicle& v, const Vec3& colour, const std::string& carClass,
                          bool headlightsOn, bool police, bool drawGlass) {
    const CarShape& s = ResourceManager::carShape(carClass);
    Vec3 pos = v.position();
    // Off the road the car sits on the kerb-height pavement.
    pos.y += city_.groundHeight(pos) * (v.isAirborne() ? 0.0f : 1.0f);
    Mat4 model = translate(Mat4(1.0f), pos);
    model = rotateY(model, v.yaw());
    model = rotateZ(model, v.bodyRoll());
    model = rotateX(model, v.bodyPitch());
    Material paint = mat(colour, 0.38f, 0.35f, kShadeCarPaint);
    renderer.drawMeshPBR(resources_.carBodyFor(carClass), model, paint);
    if (drawGlass)
        renderer.drawMeshPBR(resources_.carGlassFor(carClass), model, mat(Vec3(0.05f, 0.06f, 0.07f), 0.05f, 0.0f, kShadeGlass));
    renderer.drawMeshPBR(resources_.carTrimFor(carClass), model, mat(Vec3(0.035f, 0.035f, 0.04f), 0.55f));
    renderer.drawMeshPBR(resources_.carPlatesFor(carClass), model, mat(Vec3(0.85f, 0.84f, 0.78f), 0.4f));
    if (headlightsOn)
        renderer.drawMeshPBR(resources_.carHeadlightsFor(carClass), model, glow(Vec3(1.0f, 0.97f, 0.9f), 1.6f));
    else
        renderer.drawMeshPBR(resources_.carHeadlightsFor(carClass), model, mat(Vec3(0.72f, 0.75f, 0.78f), 0.1f, 0.6f));
    if (headlightsOn)
        renderer.drawMeshPBR(resources_.carTaillightsFor(carClass), model, glow(Vec3(0.85f, 0.03f, 0.02f), 0.9f));
    else
        renderer.drawMeshPBR(resources_.carTaillightsFor(carClass), model, mat(Vec3(0.45f, 0.02f, 0.02f), 0.08f));
    if (police) {
        float t = playTimeMinutes_ * 60.0f;
        bool phase = std::fmod(t, 0.5f) < 0.25f;
        Mat4 bar = translate(model, Vec3(0, s.roofY + 0.06f, (s.zW + s.zR) * 0.5f));
        renderer.drawMeshPBR(resources_.box(1.1f, 0.05f, 0.24f), translate(bar, Vec3(0, -0.04f, 0)),
                             mat(Vec3(0.05f, 0.05f, 0.05f), 0.5f));
        renderer.drawMeshPBR(resources_.box(0.5f, 0.08f, 0.2f), translate(bar, Vec3(-0.27f, 0, 0)),
                             phase ? glow(Vec3(1.0f, 0.08f, 0.05f), 2.5f) : mat(Vec3(0.4f, 0.05f, 0.05f), 0.2f));
        renderer.drawMeshPBR(resources_.box(0.5f, 0.08f, 0.2f), translate(bar, Vec3(0.27f, 0, 0)),
                             !phase ? glow(Vec3(0.1f, 0.25f, 1.0f), 2.5f) : mat(Vec3(0.05f, 0.08f, 0.4f), 0.2f));
    }
    // Wheels: the front pair steers; all four roll with the car's speed.
    float r = s.wheelRadius;
    float spin = std::fmod(playTimeMinutes_ * 60.0f * v.speed() / r, 6.2831853f);
    const MeshData& tyre = resources_.tyre(r);
    const MeshData& rimMesh = resources_.rimFor(r);
    Material rubber = mat(Vec3(0.025f, 0.025f, 0.028f), 0.85f);
    Material alloy = carClass == "supercar" || carClass == "sports"
                         ? mat(Vec3(0.18f, 0.18f, 0.2f), 0.3f, 1.0f)
                         : mat(Vec3(0.78f, 0.79f, 0.8f), 0.25f, 1.0f);
    for (int i = 0; i < 4; ++i) {
        bool front = i < 2;
        float side = (i % 2 == 0) ? -1.0f : 1.0f;
        Mat4 wm = translate(model, Vec3(side * s.wheelX(), r, front ? s.frontAxleZ() : s.rearAxleZ()));
        if (front) wm = rotateY(wm, v.frontWheelAngle());
        if (side < 0) wm = scale(wm, Vec3(-1, 1, 1));
        wm = rotateX(wm, spin * side);
        renderer.drawMeshPBR(tyre, wm, rubber);
        renderer.drawMeshPBR(rimMesh, wm, alloy);
    }
}

void World::drawPerson(IRenderer& renderer, const Vec3& feet, float yaw, float phase, float moving,
                       size_t lookIndex) {
    PersonLook look = lookFor(lookIndex);
    Material skin = mat(look.skin, 0.6f), top = mat(look.top, 0.8f), legs = mat(look.legs, 0.85f);
    Material hair = mat(look.hair, 0.7f), shoes = mat(look.shoes, 0.5f);
    float amp = moving > 1.5f ? 0.9f : (moving > 0.5f ? 0.5f : 0.0f);
    float bob = amp > 0.0f ? std::fabs(std::sin(phase)) * 0.035f * amp : 0.0f;
    float hipY = 0.93f + bob - (amp > 0.8f ? 0.05f : 0.0f);
    float scaleH = 0.94f + 0.12f * hashf(static_cast<float>(lookIndex), 3.0f);
    Mat4 root = translate(Mat4(1.0f), feet);
    root = rotateY(root, yaw);
    root = scale(root, Vec3(scaleH));
    Mat4 hips = translate(root, Vec3(0, hipY, 0));
    float lean = amp > 0.8f ? 0.18f : 0.03f * amp;
    Mat4 torso = rotateX(hips, lean);
    renderer.drawMeshPBR(resources_.personTorso(), torso, top);
    Mat4 neck = translate(torso, Vec3(0, 0.6f, 0.0f));
    renderer.drawMeshPBR(resources_.personHead(), neck, skin);
    renderer.drawMeshPBR(resources_.personHair(), neck, hair);
    for (int sd = -1; sd <= 1; sd += 2) {
        float legPhase = phase + (sd > 0 ? 0.0f : 3.14159265f);
        float hip = std::sin(legPhase) * amp;
        float knee = amp > 0.0f ? (0.1f + 0.7f * std::max(0.0f, -std::cos(legPhase))) * amp : 0.05f;
        Mat4 thigh = translate(hips, Vec3(sd * 0.09f, 0, 0));
        thigh = rotateX(thigh, -hip);
        renderer.drawMeshPBR(resources_.personLimb(0.46f, 0.08f), thigh, legs);
        Mat4 shin = translate(thigh, Vec3(0, -0.45f, 0));
        shin = rotateX(shin, knee);
        renderer.drawMeshPBR(resources_.personLimb(0.44f, 0.065f), shin, legs);
        Mat4 foot = translate(shin, Vec3(0, -0.43f, 0));
        foot = rotateX(foot, -knee * 0.5f + hip * 0.3f);
        renderer.drawMeshPBR(resources_.personShoe(), foot, shoes);
        Mat4 arm = translate(torso, Vec3(sd * 0.215f, 0.54f, 0));
        arm = rotateZ(arm, sd * 0.07f);
        arm = rotateX(arm, hip * 0.8f);
        renderer.drawMeshPBR(resources_.personLimb(0.32f, 0.055f), arm, top);
        Mat4 fore = translate(arm, Vec3(0, -0.3f, 0));
        fore = rotateX(fore, -0.15f - (amp > 0.8f ? 1.2f : 0.25f * amp));
        renderer.drawMeshPBR(resources_.personLimb(0.28f, 0.045f), fore, top);
        renderer.drawMeshPBR(resources_.personHand(), translate(fore, Vec3(0, -0.27f, 0)), skin);
    }
}

void World::drawCity(IRenderer& renderer, const Camera& cam) {
    const Vec3 eye = cam.position;
    Vec3 look = cam.target - cam.position;
    const Vec3 fwd = length(look) > 1e-3f ? normalize(look) : Vec3(0, 0, 1);
    auto visible = [&](const Vec3& c, float radius, float maxDist) {
        Vec3 d = c - eye;
        float dist = length(d);
        if (dist - radius > maxDist) return false;
        return dot(d, fwd) > -radius - 4.0f;   // not behind the camera
    };
    const float hour = timeOfDay_;
    const bool lampsOn = hour > 18.4f || hour < 6.6f;
    Material ground = mat(Vec3(0.3f, 0.3f, 0.3f), 0.9f, 0.0f, kShadeGround);
    renderer.drawMeshPBR(resources_.groundPlane(3000.0f), Mat4(1.0f), ground);
    // Raised blocks: kerbs + pavements + plazas (the beach strip has none).
    const MeshData& unit = resources_.box(1, 1, 1);
    const float s = city_.roadSpacing();
    const int half = static_cast<int>(city_.size / 2);
    for (int gx = -half; gx < half; gx += static_cast<int>(s))
        for (int gz = -half + 40; gz < half; gz += static_cast<int>(s)) {
            Vec3 c(gx + s / 2, City::kKerb / 2, gz + s / 2);
            if (!visible(c, 19.0f, 700.0f)) continue;
            Mat4 m = scale(translate(Mat4(1.0f), c), Vec3(s - 14.0f, City::kKerb, s - 14.0f));
            renderer.drawMeshPBR(unit, m, ground);
        }

    // Buildings: facade shader + cornices, set-back towers, rooftop plant.
    static const Vec3 concrete[] = {{0.62f, 0.60f, 0.56f}, {0.52f, 0.53f, 0.53f}, {0.70f, 0.66f, 0.58f}, {0.45f, 0.47f, 0.50f}};
    static const Vec3 glassTint[] = {{0.32f, 0.44f, 0.50f}, {0.24f, 0.30f, 0.38f}, {0.40f, 0.42f, 0.38f}};
    static const Vec3 brick[] = {{0.50f, 0.23f, 0.15f}, {0.56f, 0.34f, 0.24f}, {0.40f, 0.19f, 0.15f}};
    static const Vec3 stone[] = {{0.74f, 0.68f, 0.57f}, {0.66f, 0.63f, 0.58f}, {0.80f, 0.76f, 0.68f}};
    for (const auto& b : city_.buildings) {
        Vec3 lo = b.bounds.min, hi = b.bounds.max;
        Vec3 size = hi - lo;
        Vec3 centre = (lo + hi) * 0.5f;
        if (!visible(centre, length(size) * 0.5f, 700.0f)) continue;
        size_t pick = static_cast<size_t>(b.seed * 97.0f);
        Vec3 col = b.style == 1 ? glassTint[pick % 3]
                 : b.style == 2 ? brick[pick % 3]
                 : b.style == 3 ? stone[pick % 3] : concrete[pick % 4];
        Material wall = mat(col, 0.8f, 0.0f, kShadeFacade);
        wall.variant = static_cast<float>(b.style) + std::min(b.seed, 0.999f);
        Material trim = mat(col * 0.75f, 0.7f);
        float h = size.y;
        float topY = h;
        if (b.podium > 0.0f) {
            Mat4 pm = scale(translate(Mat4(1.0f), Vec3(centre.x, b.podium / 2, centre.z)), Vec3(size.x, b.podium, size.z));
            renderer.drawMeshPBR(unit, pm, wall);
            float inset = 1.8f;
            Vec3 ts(size.x - 2 * inset, h - b.podium, size.z - 2 * inset);
            Mat4 tm = scale(translate(Mat4(1.0f), Vec3(centre.x, b.podium + ts.y / 2, centre.z)), ts);
            renderer.drawMeshPBR(unit, tm, wall);
            renderer.drawMeshPBR(unit, scale(translate(Mat4(1.0f), Vec3(centre.x, b.podium + 0.3f, centre.z)),
                                             Vec3(size.x + 0.3f, 0.6f, size.z + 0.3f)), trim);
            // Crown: a recessed plant screen on top of the tower.
            Vec3 cs(ts.x * 0.8f, 3.2f, ts.z * 0.8f);
            renderer.drawMeshPBR(unit, scale(translate(Mat4(1.0f), Vec3(centre.x, h + cs.y / 2, centre.z)), cs),
                                 mat(Vec3(0.20f, 0.21f, 0.22f), 0.45f, 0.6f));
            topY = h + cs.y;
        } else {
            Mat4 m = scale(translate(Mat4(1.0f), Vec3(centre.x, h / 2, centre.z)), size);
            renderer.drawMeshPBR(unit, m, wall);
            // Cornice / parapet.
            renderer.drawMeshPBR(unit, scale(translate(Mat4(1.0f), Vec3(centre.x, h + 0.35f, centre.z)),
                                             Vec3(size.x + 0.5f, 0.7f, size.z + 0.5f)), trim);
            topY = h + 0.7f;
        }
        if (length(centre - eye) > 320.0f) continue;
        // Rooftop plant: AC units, a water tank on older buildings, masts on towers.
        Material plant = mat(Vec3(0.55f, 0.56f, 0.57f), 0.5f, 0.4f);
        for (int k = 0; k < 3; ++k) {
            float ox = (hashf(b.seed * 31.0f, static_cast<float>(k)) - 0.5f) * size.x * 0.5f;
            float oz = (hashf(b.seed * 17.0f, static_cast<float>(k) + 5.0f) - 0.5f) * size.z * 0.5f;
            renderer.drawMeshPBR(unit, scale(translate(Mat4(1.0f), Vec3(centre.x + ox, topY + 0.6f, centre.z + oz)),
                                             Vec3(2.2f, 1.2f, 1.6f)), plant);
        }
        if (b.style == 2 || b.style == 3) {
            Vec3 tp(centre.x + size.x * 0.22f, topY, centre.z - size.z * 0.2f);
            renderer.drawMeshPBR(resources_.cylinder(1.3f, 2.4f), translate(Mat4(1.0f), tp + Vec3(0, 1.2f, 0)),
                                 mat(Vec3(0.30f, 0.22f, 0.16f), 0.8f));
            renderer.drawMeshPBR(resources_.cylinder(0.08f, 1.2f), translate(Mat4(1.0f), tp), plant);
        }
        if (b.style == 1 || h > 40.0f) {
            renderer.drawMeshPBR(resources_.cylinder(0.12f, 9.0f), translate(Mat4(1.0f), Vec3(centre.x, topY, centre.z)), plant);
            if (lampsOn || hour < 7.5f || hour > 17.5f)
                renderer.drawMeshPBR(resources_.sphere(0.25f), translate(Mat4(1.0f), Vec3(centre.x, topY + 9.0f, centre.z)),
                                     glow(Vec3(1.0f, 0.1f, 0.05f), std::fmod(playTimeMinutes_ * 60.0f, 2.0f) < 1.0f ? 2.0f : 0.3f));
        }
    }

    // Street trees.
    Material bark = mat(Vec3(0.24f, 0.18f, 0.13f), 0.9f);
    for (size_t i = 0; i < city_.trees.size(); ++i) {
        const Tree& t = city_.trees[i];
        if (!visible(t.position + Vec3(0, 4, 0), 4.0f, 260.0f)) continue;
        Mat4 m = translate(Mat4(1.0f), t.position + Vec3(0, City::kKerb, 0));
        m = rotateY(m, static_cast<float>(i) * 1.7f);
        m = scale(m, Vec3(t.scale));
        renderer.drawMeshPBR(resources_.treeTrunk(), m, bark);
        Vec3 leaf = (i % 3 == 0) ? Vec3(0.22f, 0.33f, 0.10f) : ((i % 3 == 1) ? Vec3(0.17f, 0.28f, 0.09f) : Vec3(0.26f, 0.32f, 0.12f));
        renderer.drawMeshPBR(resources_.treeCanopy(t.variant), m, mat(leaf, 0.85f, 0.0f, kShadeFoliage));
    }
    // Streetlamps.
    Material steel = mat(Vec3(0.16f, 0.17f, 0.18f), 0.45f, 0.7f);
    for (const auto& l : city_.streetLamps) {
        if (!visible(l.position + Vec3(0, 4, 0), 5.0f, 300.0f)) continue;
        Mat4 m = translate(Mat4(1.0f), l.position + Vec3(0, City::kKerb, 0));
        m = rotateY(m, l.yaw);
        renderer.drawMeshPBR(resources_.lampPost(), m, steel);
        Mat4 head = translate(m, Vec3(1.62f, 7.36f, 0));
        renderer.drawMeshPBR(resources_.lampHead(), head, steel);
        renderer.drawMeshPBR(resources_.box(0.5f, 0.02f, 0.2f), translate(head, Vec3(0, -0.055f, 0)),
                             lampsOn ? glow(Vec3(1.0f, 0.86f, 0.66f), 2.2f) : mat(Vec3(0.75f, 0.75f, 0.72f), 0.2f));
    }
    // Traffic signals on two corners of each signalled junction.
    Material signalPaint = mat(Vec3(0.12f, 0.13f, 0.12f), 0.55f, 0.3f);
    for (const auto& tl : city_.lights) {
        if (!visible(tl.position + Vec3(0, 3, 0), 10.0f, 260.0f)) continue;
        for (int c = 0; c < 2; ++c) {
            float sd = c == 0 ? 1.0f : -1.0f;
            Mat4 m = translate(Mat4(1.0f), tl.position + Vec3(sd * 8.1f, City::kKerb, sd * 8.1f));
            m = rotateY(m, c == 0 ? 3.14159265f : 0.0f);
            renderer.drawMeshPBR(resources_.signalPole(), m, signalPaint);
            Mat4 head = translate(m, Vec3(4.6f, 5.05f, 0));
            renderer.drawMeshPBR(resources_.signalHead(), head, signalPaint);
            const MeshData& lamp = resources_.sphere(0.1f);
            renderer.drawMeshPBR(lamp, translate(head, Vec3(0, 0.29f, 0.12f)),
                                 !tl.green ? glow(Vec3(1.0f, 0.06f, 0.03f), 3.0f) : mat(Vec3(0.18f, 0.03f, 0.03f), 0.2f));
            renderer.drawMeshPBR(lamp, translate(head, Vec3(0, 0.0f, 0.12f)), mat(Vec3(0.2f, 0.14f, 0.03f), 0.2f));
            renderer.drawMeshPBR(lamp, translate(head, Vec3(0, -0.29f, 0.12f)),
                                 tl.green ? glow(Vec3(0.1f, 1.0f, 0.45f), 3.0f) : mat(Vec3(0.03f, 0.15f, 0.08f), 0.2f));
        }
    }
}

void World::render(IRenderer& renderer) {
    Vec3 sunDir, sunColour;
    float sunIntensity = 1.0f;
    skyLight(timeOfDay_, sunDir, sunColour, sunIntensity);
    renderer.setSun(sunDir, sunColour, sunIntensity);
    renderer.setLights(lightRig_);
    post_.wetness = weather_.state().wetness;
    renderer.setPostSettings(post_);
    renderer.setWireframe(debug_.wireframe);
    // Camera: chase (speed-sensitive FOV), hood, or cockpit.
    Vec3 playerPos = player_.position();
    float playerYaw = player_.yaw();
    Vec3 fwd{std::sin(playerYaw), 0, std::cos(playerYaw)};
    Camera cam;
    cam.aspect = 16.0f / 9.0f;
    float speedKmh = player_.speedKmh();
    bool inCar = player_.currentVehicle() != nullptr;
    if (hud_.photoMode) {
        Vec3 target = playerPos + Vec3(0, 0.8f, 0);
        cam.position = target + Vec3(std::sin(photoYaw_) * std::cos(photoPitch_),
                                     std::sin(photoPitch_), std::cos(photoYaw_) * std::cos(photoPitch_)) * photoDist_;
        cam.target = target;
        cam.fov = 50.0f;
    } else if (cameraMode_ == 2 && inCar) {
        // Cockpit: behind the dashboard, looking forward; the interior is
        // drawn in front of the camera.
        cam.position = playerPos + fwd * 0.4f + Vec3(0, 1.15f, 0);
        cam.target = playerPos + fwd * 30.0f + Vec3(0, 1.0f, 0);
        cam.fov = 70.0f + speedKmh * 0.08f;
    } else if (cameraMode_ == 1 && inCar) {
        cam.position = playerPos + fwd * 1.6f + Vec3(0, 1.1f, 0);
        cam.target = playerPos + fwd * 30.0f + Vec3(0, 0.8f, 0);
        cam.fov = 72.0f + speedKmh * 0.08f;
    } else if (inCar) {
        Vec3 eye = playerPos - fwd * 7.2f + Vec3(0, 2.4f, 0);
        cam.position = eye;
        cam.target = playerPos + fwd * 4.0f + Vec3(0, 0.9f, 0);
        cam.fov = 62.0f + speedKmh * 0.10f; // FOV widens with speed
    } else {
        cam.position = playerPos - fwd * 4.2f + Vec3(0, 2.1f, 0);
        cam.target = playerPos + fwd * 2.0f + Vec3(0, 1.3f, 0);
        cam.fov = 60.0f;
    }
    if (screenShake_ > 0.01f) {
        cam.position.x += std::sin(playTimeMinutes_ * 400.0f) * screenShake_ * 0.15f;
        cam.position.y += std::cos(playTimeMinutes_ * 370.0f) * screenShake_ * 0.15f;
    }
    renderer.setCamera(cam);
    renderer.beginFrame();
    drawCity(renderer, cam);
    bool lightsOn = timeOfDay_ > 18.4f || timeOfDay_ < 6.6f || weather_.state().wetness > 0.5f;
    auto near = [&](const Vec3& p, float d) { return length(p - cam.position) < d; };
    // Parked cars (garage cars), traffic, police.
    for (size_t i = 0; i < parkedCars_.size(); ++i) {
        const Vehicle& car = parkedCars_[i];
        if (!near(car.position(), 400.0f)) continue;
        int ci = i < parkedCatalogueIndex_.size() ? parkedCatalogueIndex_[i] : 3;
        std::string cls = (ci >= 0 && ci < static_cast<int>(catalogue_.size()))
                              ? catalogue_[static_cast<size_t>(ci)].params.carClass : "sedan";
        Vec3 col = paintColour(garage_.paintIndex(),
                               (ci >= 0 && ci < static_cast<int>(catalogue_.size()))
                                   ? catalogue_[static_cast<size_t>(ci)].colourName : "grey");
        bool driving = player_.currentVehicle() == &car;
        drawCarStyled(renderer, car, col, cls, driving && lightsOn, false, !(driving && cameraMode_ == 2));
    }
    const auto& traffic = traffic_.cars();
    for (size_t i = 0; i < traffic.size(); ++i) {
        if (!near(traffic[i].vehicle.position(), 400.0f)) continue;
        drawCarStyled(renderer, traffic[i].vehicle, trafficPaint(i), trafficClass(i), lightsOn, false, true);
    }
    for (const auto& car : police_.cars()) {
        if (!near(car.position, 400.0f)) continue;
        Vehicle pv;
        pv.setPosition(car.position, car.yaw);
        drawCarStyled(renderer, pv, Vec3(0.93f, 0.93f, 0.95f), "sedan", true, true, true);
    }
    // Player's own car: drawn only when it is the car being driven (a parked
    // car the player entered is already drawn in the parked loop above).
    if (player_.currentVehicle() && player_.currentVehicle() == player_.ownedVehicle()) {
        const Vehicle& v = *player_.currentVehicle();
        drawCarStyled(renderer, v,
                      paintColour(garage_.paintIndex(), catalogue_[static_cast<size_t>(garage_.currentCar())].colourName),
                      catalogue_[static_cast<size_t>(garage_.currentCar())].params.carClass, lightsOn, false,
                      cameraMode_ != 2);
    }
    // Cockpit interior: dashboard + steering wheel that turns with input.
    if (cameraMode_ == 2 && inCar) {
        const Vehicle& v = *player_.currentVehicle();
        Mat4 base = translate(Mat4(1.0f), playerPos + fwd * 0.4f + Vec3(0, 1.15f, 0));
        base = rotateY(base, playerYaw);
        renderer.drawMeshPBR(resources_.dashboard(), translate(base, Vec3(0, -0.2f, 0.95f)), mat(Vec3(0.06f, 0.06f, 0.07f), 0.6f));
        Mat4 wheel = translate(base, Vec3(-0.0f, -0.12f, 0.55f));
        wheel = rotateX(wheel, -0.35f);
        wheel = rotateZ(wheel, -v.frontWheelAngle() * 2.0f);
        renderer.drawMeshPBR(resources_.steeringWheel(), wheel, mat(Vec3(0.05f, 0.05f, 0.055f), 0.5f));
    }
    // Collectibles (glowing parcels) and stunt ramps.
    const MeshData& box = resources_.box(1, 1, 1);
    float spinT = playTimeMinutes_ * 60.0f;
    for (const auto& item : activities_.items()) {
        if (item.taken || !near(item.position, 250.0f)) continue;
        Mat4 m = translate(Mat4(1.0f), item.position + Vec3(0, 0.8f + 0.15f * std::sin(spinT * 2.0f), 0));
        m = rotateY(m, spinT);
        renderer.drawMeshPBR(resources_.box(0.6f, 0.6f, 0.6f), m, glow(Vec3(1.0f, 0.78f, 0.15f), 0.8f));
    }
    for (const auto& j : activities_.jumps()) {
        if (!near(j.position, 300.0f)) continue;
        Mat4 ramp = translate(Mat4(1.0f), j.position + Vec3(0, 0.6f, 0));
        renderer.drawMeshPBR(box, ramp, mat(Vec3(0.95f, 0.45f, 0.08f), 0.6f));
    }
    // Pedestrians on the pavements.
    const auto& peds = pedestrians_.all();
    for (size_t i = 0; i < peds.size(); ++i) {
        const Pedestrian& p = peds[i];
        if (!near(p.position, 160.0f)) continue;
        Vec3 feet = p.position;
        feet.y = city_.groundHeight(p.position);
        drawPerson(renderer, feet, p.yaw, p.phase, p.moving, i + 11);
    }
    if (player_.onFoot()) {
        Vec3 pp = player_.position();
        Vec3 step = pp - lastFootPos_;
        step.y = 0.0f;
        float moved = length(step);
        footPhase_ += moved * 3.6f;
        footMoving_ = moved > 0.002f ? std::min(2.0f, moved * 60.0f / 1.6f) : 0.0f;
        lastFootPos_ = pp;
        Vec3 feet = pp;
        feet.y = city_.groundHeight(pp);
        drawPerson(renderer, feet, player_.yaw(), footPhase_, footMoving_ > 0.1f ? std::max(1.0f, footMoving_) : 0.0f, 0);
    } else {
        lastFootPos_ = player_.position();
    }
}
} // namespace gc6
