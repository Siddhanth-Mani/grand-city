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
    timeOfDay_ = 10.0f;
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
    lightRig_.sunDir = Vec3(std::cos((timeOfDay_ / 24.0f) * 6.28318f),
                            std::sin((timeOfDay_ / 24.0f) * 6.28318f), 0.3f);
    lightRig_.sunColour = Vec3(1.0f, 0.9f, 0.8f);
    lightRig_.sunIntensity = hud_.night ? 0.25f : 1.0f;
    bool night = timeOfDay_ < 6.0f || timeOfDay_ > 19.0f;
    // Headlights: player car + traffic cars ahead (forward list, max 16).
    auto addHeadlight = [&](const Vehicle& v) {
        Vec3 fwd{std::sin(v.yaw()), 0, std::cos(v.yaw())};
        DynamicLight l;
        l.position = v.position() + fwd * 2.5f + Vec3(0, 0.8f, 0);
        l.colour = Vec3(1.0f, 0.95f, 0.8f);
        l.radius = 25.0f; l.intensity = night ? 2.0f : 0.8f; l.isHeadlight = true;
        lightRig_.add(l);
    };
    if (player_.currentVehicle()) addHeadlight(*player_.currentVehicle());
    for (const auto& c : traffic_.cars()) {
        if (lightRig_.lights.size() >= LightRig::kMaxLights) break;
        addHeadlight(c.vehicle);
    }
    for (const auto& c : police_.cars()) {
        if (lightRig_.lights.size() >= LightRig::kMaxLights) break;
        DynamicLight l;
        l.position = c.position + Vec3(0, 1.2f, 0);
        l.colour = Vec3(0.2f, 0.4f, 1.0f);
        l.radius = 15.0f; l.intensity = 2.0f; l.isPolice = true;
        lightRig_.add(l);
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
    scene_.clear();
    const MeshData& ground = resources_.groundPlane(city_.size);
    scene_.add("ground", &ground, Mat4(1.0f), Vec3(0.16f, 0.16f, 0.18f));
    const MeshData& box = resources_.box(1, 1, 1);
    for (const auto& b : city_.buildings) {
        Vec3 centre = (b.bounds.min + b.bounds.max) * 0.5f;
        Vec3 size = b.bounds.max - b.bounds.min;
        Mat4 model = translate(Mat4(1.0f), Vec3(centre.x, size.y / 2, centre.z));
        model = scale(model, size);
        scene_.add("building", &box, model, b.colour);
    }
    const MeshData& lamp = resources_.box(0.3f, 6.0f, 0.3f);
    for (const auto& l : city_.lamps)
        scene_.add("lamp", &lamp, translate(Mat4(1.0f), l + Vec3(0, 3, 0)), Vec3(0.9f, 0.85f, 0.6f));
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
void World::drawCar(IRenderer& renderer, const Vehicle& v, const Vec3& colour,
                    const std::string& carClass, bool headlightsOn) {
    const MeshData& body = resources_.carBodyFor(carClass);
    Mat4 model = translate(Mat4(1.0f), v.position());
    model = rotateY(model, v.yaw());
    model = rotateZ(model, v.bodyRoll());
    model = rotateX(model, v.bodyPitch());
    const Material* paint = findMaterial(materials_, "carPaintRed");
    if (paint) {
        Material m = *paint;
        m.albedo = colour;
        renderer.drawMeshPBR(body, model, m);
    } else {
        renderer.drawMesh(body, model, colour);
    }
    // Wheels: front pair steers, all four spin with speed.
    const MeshData& wheel = resources_.wheel();
    float spin = v.speed() * 0.1f; // accumulated visually per frame is fine
    static const Vec3 offsets[4] = {{-0.9f, 0.35f, 1.4f}, {0.9f, 0.35f, 1.4f},
                                    {-0.9f, 0.35f, -1.4f}, {0.9f, 0.35f, -1.4f}};
    for (int i = 0; i < 4; ++i) {
        Mat4 wm = model;
        wm = translate(wm, offsets[static_cast<size_t>(i)]);
        if (i < 2) wm = rotateY(wm, v.frontWheelAngle());
        wm = rotateX(wm, spin);
        renderer.drawMesh(wheel, wm, Vec3(0.05f, 0.05f, 0.05f));
    }
    // Mirrors + spoiler for the sporty classes.
    const MeshData& mirror = resources_.mirror();
    renderer.drawMesh(mirror, translate(model, Vec3(-1.05f, 0.75f, 0.9f)), Vec3(0.1f, 0.1f, 0.1f));
    renderer.drawMesh(mirror, translate(model, Vec3(1.05f, 0.75f, 0.9f)), Vec3(0.1f, 0.1f, 0.1f));
    if (carClass == "supercar" || carClass == "sports") {
        const MeshData& spoiler = resources_.spoiler();
        renderer.drawMesh(spoiler, translate(model, Vec3(0, 0.85f, -2.1f)), colour);
    }
    // Headlights / taillights: emissive when on (night or always for brake).
    const MeshData& head = resources_.headlight();
    const MeshData& tail = resources_.taillight();
    Vec3 headColour = headlightsOn ? Vec3(1.0f, 0.95f, 0.8f) : Vec3(0.4f, 0.4f, 0.35f);
    renderer.drawMesh(head, translate(model, Vec3(-0.7f, 0.55f, 2.25f)), headColour);
    renderer.drawMesh(head, translate(model, Vec3(0.7f, 0.55f, 2.25f)), headColour);
    renderer.drawMesh(tail, translate(model, Vec3(-0.7f, 0.6f, -2.25f)), Vec3(0.8f, 0.05f, 0.05f));
    renderer.drawMesh(tail, translate(model, Vec3(0.7f, 0.6f, -2.25f)), Vec3(0.8f, 0.05f, 0.05f));
}
void World::render(IRenderer& renderer) {
    // Sun: low at dawn/dusk, high at noon; warm colour at sunset.
    float sunAngle = (timeOfDay_ / 24.0f) * 6.28318f;
    Vec3 sunDir{std::cos(sunAngle), std::sin(sunAngle), 0.3f};
    float warm = clampf(1.0f - std::fabs(timeOfDay_ - 18.0f) / 6.0f, 0.0f, 1.0f);
    Vec3 sunColour{1.0f, 1.0f - 0.35f * warm, 1.0f - 0.6f * warm};
    renderer.setSun(normalize(sunDir), sunColour, hud_.night ? 0.25f : 1.0f);
    renderer.setLights(lightRig_);
    renderer.setPostSettings(post_);
    renderer.setWireframe(debug_.wireframe);
    // Camera: chase (speed-sensitive FOV), hood, or cockpit.
    Vec3 playerPos = player_.position();
    float playerYaw = player_.yaw();
    Vec3 fwd{std::sin(playerYaw), 0, std::cos(playerYaw)};
    Camera cam;
    cam.aspect = 16.0f / 9.0f;
    float speedKmh = player_.speedKmh();
    if (hud_.photoMode) {
        Vec3 target = playerPos;
        cam.position = target + Vec3(std::sin(photoYaw_) * std::cos(photoPitch_),
                                     std::sin(photoPitch_), std::cos(photoYaw_) * std::cos(photoPitch_)) * photoDist_;
        cam.target = target;
        cam.fov = 55.0f;
    } else if (cameraMode_ == 2 && player_.currentVehicle()) {
        // Cockpit: behind the dashboard, looking forward; the interior is
        // drawn in front of the camera.
        cam.position = playerPos + fwd * 0.4f + Vec3(0, 1.15f, 0);
        cam.target = playerPos + fwd * 30.0f + Vec3(0, 1.0f, 0);
        cam.fov = 70.0f + speedKmh * 0.08f;
    } else if (cameraMode_ == 1 && player_.currentVehicle()) {
        cam.position = playerPos + fwd * 1.6f + Vec3(0, 1.1f, 0);
        cam.target = playerPos + fwd * 30.0f + Vec3(0, 0.8f, 0);
        cam.fov = 72.0f + speedKmh * 0.08f;
    } else {
        Vec3 eye = playerPos - fwd * 9.0f + Vec3(0, 3.5f, 0);
        cam.position = eye;
        cam.target = playerPos + fwd * 4.0f;
        cam.fov = 70.0f + speedKmh * 0.12f; // FOV widens with speed
    }
    if (screenShake_ > 0.01f) {
        cam.position.x += std::sin(playTimeMinutes_ * 400.0f) * screenShake_ * 0.15f;
        cam.position.y += std::cos(playTimeMinutes_ * 370.0f) * screenShake_ * 0.15f;
    }
    renderer.setCamera(cam);
    renderer.beginFrame();
    scene_.render(renderer);
    bool night = hud_.night;
    // Parked cars (garage cars), traffic, police.
    for (size_t i = 0; i < parkedCars_.size(); ++i) {
        const Vehicle& car = parkedCars_[i];
        int ci = i < parkedCatalogueIndex_.size() ? parkedCatalogueIndex_[i] : 3;
        std::string cls = (ci >= 0 && ci < static_cast<int>(catalogue_.size()))
                              ? catalogue_[static_cast<size_t>(ci)].params.carClass : "sedan";
        Vec3 col = paintColour(garage_.paintIndex(),
                               (ci >= 0 && ci < static_cast<int>(catalogue_.size()))
                                   ? catalogue_[static_cast<size_t>(ci)].colourName : "grey");
        drawCar(renderer, car, col, cls, night);
    }
    for (const auto& car : traffic_.cars())
        drawCar(renderer, car.vehicle, Vec3(0.7f, 0.7f, 0.75f), "sedan", night);
    for (const auto& car : police_.cars()) {
        Vehicle pv;
        pv.setPosition(car.position, car.yaw);
        drawCar(renderer, pv, Vec3(0.1f, 0.15f, 0.9f), "sedan", true);
    }
    // Player's own car: drawn only when it is the car being driven (a parked
    // car the player entered is already drawn in the parked loop above).
    if (player_.currentVehicle() && player_.currentVehicle() == player_.ownedVehicle()) {
        const Vehicle& v = *player_.currentVehicle();
        drawCar(renderer, v,
                paintColour(garage_.paintIndex(), catalogue_[static_cast<size_t>(garage_.currentCar())].colourName),
                catalogue_[static_cast<size_t>(garage_.currentCar())].params.carClass, night);
    }
    // Cockpit interior: dashboard + steering wheel that turns with input.
    if (cameraMode_ == 2 && player_.currentVehicle()) {
        const Vehicle& v = *player_.currentVehicle();
        Mat4 base = translate(Mat4(1.0f), playerPos + fwd * 0.4f + Vec3(0, 1.15f, 0));
        base = rotateY(base, playerYaw);
        renderer.drawMesh(resources_.dashboard(), translate(base, Vec3(0, -0.15f, 0.9f)), Vec3(0.1f, 0.1f, 0.12f));
        Mat4 wheel = translate(base, Vec3(0, -0.1f, 0.6f));
        wheel = rotateZ(wheel, -v.frontWheelAngle() * 2.0f);
        renderer.drawMesh(resources_.steeringWheel(), wheel, Vec3(0.15f, 0.15f, 0.15f));
    }
    // Collectibles (gold boxes) and stunt ramps (orange wedges).
    const MeshData& box = resources_.box(1, 1, 1);
    for (const auto& item : activities_.items()) {
        if (item.taken) continue;
        renderer.drawMesh(box, translate(Mat4(1.0f), item.position + Vec3(0, 0.5f, 0)),
                          Vec3(1.0f, 0.85f, 0.1f));
    }
    for (const auto& j : activities_.jumps()) {
        Mat4 ramp = translate(Mat4(1.0f), j.position + Vec3(0, 0.6f, 0));
        renderer.drawMesh(box, ramp, Vec3(0.95f, 0.4f, 0.05f));
    }
    const MeshData& personMesh = resources_.person();
    for (const auto& p : pedestrians_.all())
        renderer.drawMesh(personMesh, translate(Mat4(1.0f), p.position), Vec3(0.9f, 0.8f, 0.6f));
    if (player_.onFoot())
        renderer.drawMesh(personMesh, translate(Mat4(1.0f), player_.position()), Vec3(0.9f, 0.5f, 0.1f));
}
} // namespace gc6
