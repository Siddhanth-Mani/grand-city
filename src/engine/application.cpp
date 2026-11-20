#include "engine/application.h"
#include "core/clock.h"
#include "core/logger.h"
#include "core/profiler.h"
#include "engine/window.h"
#include "game/overlay.h"
#include "renderer/renderer.h"
#include "renderdoc/renderdoc.h"
#include <string>
namespace gc6 {
Application::Application() {
    Logger::init("grand-city-6.log");
    config_.load("data/settings.ini");
}
namespace {
// Menu navigation with the keys the input layer has: Up/Down move the
// selection, E confirms, Esc goes back. Every option performs a real action.
struct MenuDriver {
    int selection = 0;
    void move(const InputState& input, int count) {
        if (count <= 0) return;
        if (input.wasPressed(Key_Up)) selection = (selection + count - 1) % count;
        if (input.wasPressed(Key_Down)) selection = (selection + 1) % count;
    }
};
} // namespace
int Application::run(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless") headless_ = true;
        if (a == "--frames" && i + 1 < argc) headlessFrames_ = std::stoi(argv[++i]);
    }
    std::string rendererName = config_.getString("renderer", "backend", GC6_RENDERER_NAME);
    Logger::info(std::string("Grand City 6 starting, renderer=") + rendererName);
    auto window = createWindow();
    WindowDesc wd;
    wd.width = config_.getInt("window", "width", 1280);
    wd.height = config_.getInt("window", "height", 720);
    if (!window->create(wd)) { Logger::error("Window creation failed"); return 1; }
    auto renderer = createRenderer(rendererName, window->nativeHandle(), wd.width, wd.height);
    if (!renderer || !renderer->init()) { Logger::error("Renderer init failed"); return 1; }
    RenderDoc::init();
    world_.init(config_);
    if (headless_) world_.flow().setState(MenuState::Playing);
    Clock clock;
    int frame = 0;
    bool running = true;
    MenuDriver menu;
    int garageSelection = 0;
    float loadingTimer = 0.0f;
    float wallClock = 0.0f;
    InputState fixedInput;
    bool saveExists = world_.hasSave("savegame.json");
    Logger::info("=== GRAND CITY 6 ===");
    Logger::info("Title menu: Up/Down to choose, E to confirm, Esc to go back");
    while (running) {
        double dt = clock.tick();
        // Headless (CI/verification) runs at a fixed 60 Hz: every render
        // frame advances exactly one physics step, so --frames N really
        // simulates N/60 seconds of game time instead of spinning through
        // N renders with almost no physics (real dt is ~0.02 ms/frame).
        if (headless_) dt = 1.0 / 60.0;
        if (!window->pollEvents(input_)) break;
        if (input_.wasPressed(Key_Escape) && world_.flow().state() == MenuState::Playing) {
            world_.flow().setState(MenuState::Paused);
            menu.selection = 0;
            Logger::info("Paused");
        }
        MenuState state = world_.flow().state();
        // ---- menu states: input drives real actions ----
        if (state == MenuState::Title) {
            auto buttons = world_.flow().buttons(saveExists);
            menu.move(input_, static_cast<int>(buttons.size()));
            if (input_.wasPressed(Key_E)) {
                std::string action = buttons[static_cast<size_t>(menu.selection)].action;
                Logger::info("Menu: " + buttons[static_cast<size_t>(menu.selection)].label);
                if (action == "new_game") {
                    world_.newGame();
                    world_.flow().setState(MenuState::Loading);
                    loadingTimer = 0.0f;
                }
                else if (action == "continue") {
                    if (world_.load("savegame.json")) {
                        world_.flow().setState(MenuState::Loading);
                        loadingTimer = 0.0f;
                    } else Logger::info("No save file found");
                }
                else if (action == "quit") running = false;
                else world_.flow().press(action, saveExists);
                menu.selection = 0;
            }
        } else if (state == MenuState::Settings) {
            auto buttons = world_.flow().buttons(saveExists);
            menu.move(input_, static_cast<int>(buttons.size()));
            if (input_.wasPressed(Key_E)) {
                std::string action = buttons[static_cast<size_t>(menu.selection)].action;
                if (action == "back") { world_.flow().setState(MenuState::Title); }
                else if (action == "graphics_cycle") {
                    bool b = !config_.getBool("post", "bloom", true);
                    config_.set("post", "bloom", b ? "true" : "false");
                    world_.applySettings(config_);
                    Logger::info(std::string("Bloom ") + (b ? "on" : "off"));
                } else if (action == "resolution_cycle") {
                    int w = config_.getInt("window", "width", 1280) == 1280 ? 1920 : 1280;
                    config_.set("window", "width", std::to_string(w));
                    config_.set("window", "height", w == 1920 ? "1080" : "720");
                    Logger::info("Resolution set to " + std::to_string(w) + "x" +
                                 (w == 1920 ? "1080" : "720"));
                } else if (action == "fullscreen_toggle") {
                    bool f = !config_.getBool("window", "fullscreen", false);
                    config_.set("window", "fullscreen", f ? "true" : "false");
                    Logger::info(std::string("Fullscreen ") + (f ? "on" : "off"));
                } else if (action == "volume_cycle") {
                    float v = config_.getFloat("audio", "volume", 0.8f);
                    v = v > 0.9f ? 0.5f : (v > 0.6f ? 1.0f : 0.8f);
                    config_.set("audio", "volume", std::to_string(v));
                    world_.applySettings(config_);
                    Logger::info("Volume set to " + std::to_string(v));
                } else if (action == "mute_toggle") {
                    bool m = !config_.getBool("audio", "muted", false);
                    config_.set("audio", "muted", m ? "true" : "false");
                    world_.applySettings(config_);
                    Logger::info(std::string("Muted: ") + (m ? "yes" : "no"));
                } else if (action == "abs_toggle" || action == "tc_toggle" || action == "esp_toggle") {
                    std::string key = action == "abs_toggle" ? "abs"
                                      : action == "tc_toggle" ? "tractionControl" : "stabilityControl";
                    bool v = !config_.getBool("assists", key, true);
                    config_.set("assists", key, v ? "true" : "false");
                    world_.applySettings(config_);
                    Logger::info(key + std::string(" ") + (v ? "on" : "off"));
                } else if (action == "weather_cycle") {
                    std::string w = config_.getString("game", "weather", "clear");
                    w = w == "clear" ? "rain" : (w == "rain" ? "fog" : "clear");
                    config_.set("game", "weather", w);
                    world_.applySettings(config_);
                    Logger::info("Weather set to " + w);
                }
            }
            if (input_.wasPressed(Key_Escape)) world_.flow().setState(MenuState::Title);
        } else if (state == MenuState::Garage) {
            int count = world_.garage().carCount();
            if (input_.wasPressed(Key_Up)) garageSelection = (garageSelection + count - 1) % count;
            if (input_.wasPressed(Key_Down)) garageSelection = (garageSelection + 1) % count;
            if (input_.wasPressed(Key_E)) {
                if (world_.garage().owns(garageSelection)) {
                    world_.garage().selectCar(garageSelection);
                    world_.applyGarageCar();
                    Logger::info("Selected car: " + world_.garage().car(garageSelection).params.name);
                } else if (world_.garage().buyCar(garageSelection)) {
                    world_.garage().selectCar(garageSelection);
                    world_.applyGarageCar();
                    Logger::info("Bought car: " + world_.garage().car(garageSelection).params.name);
                } else {
                    Logger::info("Not enough money for " + world_.garage().car(garageSelection).params.name);
                }
            }
            if (input_.wasPressed(Key_P)) {
                if (world_.garage().repaint((world_.garage().paintIndex() + 1) % 7))
                    Logger::info("Repainted ($200)");
                else
                    Logger::info("Not enough money to repaint");
            }
            if (input_.wasPressed(Key_R)) {
                if (world_.garage().buyUpgrade(garageSelection, UpgradeKind::Engine))
                    Logger::info("Engine upgraded");
                else
                    Logger::info("Engine upgrade failed (maxed, not owned, or broke)");
                world_.applyGarageCar();
            }
            if (input_.wasPressed(Key_F)) {
                if (world_.garage().buyUpgrade(garageSelection, UpgradeKind::Brakes))
                    Logger::info("Brakes upgraded");
                world_.applyGarageCar();
            }
            if (input_.wasPressed(Key_Escape)) world_.flow().setState(MenuState::Title);
        } else if (state == MenuState::MissionSelect) {
            auto buttons = world_.flow().buttons(saveExists);
            menu.move(input_, static_cast<int>(buttons.size()));
            if (input_.wasPressed(Key_E)) {
                std::string action = buttons[static_cast<size_t>(menu.selection)].action;
                if (action == "back") { world_.flow().setState(MenuState::Title); }
                else if (action.rfind("mission_", 0) == 0) {
                    int idx = std::stoi(action.substr(8));
                    std::vector<bool> completed(6, false);
                    for (size_t i = 0; i < 6 && i < world_.missions().missions().size(); ++i)
                        completed[i] = world_.missions().missions()[i].state == MissionState::Completed;
                    if (world_.story().isUnlocked(idx, completed)) {
                        world_.startMission(idx);
                        world_.flow().setState(MenuState::Loading);
                        loadingTimer = 0.0f;
                    } else {
                        Logger::info("Mission " + std::to_string(idx + 1) + " is locked — finish the previous one first");
                    }
                }
            }
            if (input_.wasPressed(Key_Escape)) world_.flow().setState(MenuState::Title);
        } else if (state == MenuState::HowToPlay) {
            Logger::info(GameFlow::howToPlayText());
            if (input_.wasPressed(Key_E) || input_.wasPressed(Key_Escape))
                world_.flow().setState(MenuState::Title);
        } else if (state == MenuState::Loading) {
            loadingTimer += static_cast<float>(dt);
            world_.flow().setLoadingProgress(loadingTimer / 1.0f);
            if (loadingTimer >= 1.0f) {
                world_.flow().setState(MenuState::Playing);
                Logger::info("Loaded. " + world_.flow().loadingTip());
            }
        } else if (state == MenuState::Paused) {
            auto buttons = world_.flow().buttons(saveExists);
            menu.move(input_, static_cast<int>(buttons.size()));
            if (input_.wasPressed(Key_E)) {
                std::string action = buttons[static_cast<size_t>(menu.selection)].action;
                if (action == "save_quit") {
                    world_.save("savegame.json");
                    saveExists = true;
                    world_.flow().setState(MenuState::Title);
                    Logger::info("Game saved. Back to the title menu.");
                } else if (action == "settings") {
                    world_.flow().setState(MenuState::Settings);
                } else {
                    world_.flow().setState(MenuState::Playing);
                }
                menu.selection = 0;
            }
            if (input_.wasPressed(Key_Escape)) world_.flow().setState(MenuState::Playing);
        } else if (state == MenuState::MissionComplete || state == MenuState::MissionFailed) {
            if (input_.wasPressed(Key_E)) world_.flow().setState(MenuState::Playing);
        } else if (state == MenuState::GameComplete) {
            if (input_.wasPressed(Key_E)) world_.flow().setState(MenuState::Playing);
        }
        // ---- gameplay ----
        // Key presses for the fixed-step simulation are queued until a step
        // actually runs: at 60 Hz vsync some frames run zero steps, and a press
        // on such a frame (E to enter a car, Space to jump) used to be lost.
        // Each press is seen by exactly one step.
        for (size_t k = 0; k < fixedInput.pressed.size(); ++k)
            fixedInput.pressed[k] = fixedInput.pressed[k] || input_.pressed[k];
        fixedInput.down = input_.down;
        if (world_.flow().state() == MenuState::Playing) {
            int steps = clock.fixedSteps(dt);
            for (int s = 0; s < steps; ++s) {
                world_.fixedUpdate(1.0f / 60.0f, fixedInput);
                fixedInput.pressed.fill(false);
            }
            world_.update(static_cast<float>(dt), input_);
        } else {
            fixedInput.pressed.fill(false); // menu keys never leak into gameplay
        }
        input_.endFrame();
        world_.render(*renderer); // starts the frame and draws the 3D scene
        OverlayState overlay;
        overlay.menuSelection = menu.selection;
        overlay.garageSelection = garageSelection;
        overlay.saveExists = saveExists;
        overlay.timeSeconds = wallClock += static_cast<float>(dt);
        drawOverlay(*renderer, world_, overlay);
        renderer->present();
        RenderDoc::endFrame();
        if (headless_ && ++frame >= headlessFrames_) break;
    }
    world_.shutdown(); // closes Lua + audio, dumps profile.log
    Logger::info("Grand City 6 exiting after " + std::to_string(frame) + " frames");
    Logger::shutdown();
    return 0;
}
} // namespace gc6
