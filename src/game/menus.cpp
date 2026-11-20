#include "game/menus.h"
namespace gc6 {
const char* GameFlow::stateName(MenuState s) {
    switch (s) {
        case MenuState::Title: return "Title";
        case MenuState::Settings: return "Settings";
        case MenuState::Garage: return "Garage";
        case MenuState::MissionSelect: return "MissionSelect";
        case MenuState::HowToPlay: return "HowToPlay";
        case MenuState::Loading: return "Loading";
        case MenuState::Playing: return "Playing";
        case MenuState::Paused: return "Paused";
        case MenuState::MissionComplete: return "MissionComplete";
        case MenuState::MissionFailed: return "MissionFailed";
        case MenuState::GameComplete: return "GameComplete";
    }
    return "Title";
}
std::vector<MenuButton> GameFlow::buttons(bool hasSave) const {
    switch (state_) {
        case MenuState::Title:
            return {{"New Game", "new_game"}, {"Continue", "continue", hasSave},
                    {"Garage", "garage"}, {"Missions", "missions"},
                    {"Settings", "settings"}, {"How to Play", "howto"}, {"Quit", "quit"}};
        case MenuState::Settings:
            return {{"Graphics: High", "graphics_cycle"}, {"Resolution: 1280x720", "resolution_cycle"},
                    {"Fullscreen: Off", "fullscreen_toggle"}, {"Volume: 80%", "volume_cycle"},
                    {"Mute: Off", "mute_toggle"}, {"ABS: On", "abs_toggle"},
                    {"Traction Control: On", "tc_toggle"}, {"Stability Control: On", "esp_toggle"},
                    {"Weather: Clear", "weather_cycle"}, {"Back", "back"}};
        case MenuState::Garage:
            return {{"Buy / Select Car", "garage_select"}, {"Paint ($200)", "garage_paint"},
                    {"Upgrade Engine", "garage_engine"}, {"Upgrade Brakes", "garage_brakes"},
                    {"Upgrade Tyres", "garage_tyres"}, {"Upgrade Suspension", "garage_suspension"},
                    {"Back", "back"}};
        case MenuState::MissionSelect:
            return {{"Mission 1 — Harbour Delivery", "mission_0"},
                    {"Mission 2 — City Pickup", "mission_1"},
                    {"Mission 3 — Lose the Heat", "mission_2"},
                    {"Mission 4 — City Taxi", "mission_3"},
                    {"Mission 5 — Four-Star Escape", "mission_4"},
                    {"Mission 6 — Stunt Show", "mission_5"},
                    {"Back", "back"}};
        case MenuState::HowToPlay:
            return {{"Back", "back"}};
        case MenuState::Paused:
            return {{"Resume", "resume"}, {"Settings", "settings"},
                    {"Save & Quit to Menu", "save_quit"}};
        case MenuState::MissionComplete:
            return {{"Continue", "continue_playing"}, {"Replay Mission", "replay_mission"},
                    {"Missions", "missions"}, {"Quit to Menu", "quit_to_menu"}};
        case MenuState::MissionFailed:
            return {{"Retry Mission", "retry_mission"}, {"Free Roam", "continue_playing"},
                    {"Quit to Menu", "quit_to_menu"}};
        case MenuState::GameComplete:
            return {{"Free Roam", "continue_playing"}, {"Missions", "missions"},
                    {"Quit to Menu", "quit_to_menu"}};
        default:
            return {};
    }
}
MenuState GameFlow::press(const std::string& action, bool hasSave) {
    if (action == "new_game") { state_ = MenuState::Loading; return state_; }
    if (action == "continue") { if (hasSave) state_ = MenuState::Loading; return state_; }
    if (action == "garage") { state_ = MenuState::Garage; return state_; }
    if (action == "missions") { state_ = MenuState::MissionSelect; return state_; }
    if (action == "settings") { state_ = MenuState::Settings; return state_; }
    if (action == "howto") { state_ = MenuState::HowToPlay; return state_; }
    if (action == "quit") { state_ = MenuState::Title; return state_; }
    if (action == "back") { state_ = MenuState::Title; return state_; }
    if (action == "resume" || action == "continue_playing") { state_ = MenuState::Playing; return state_; }
    if (action == "save_quit" || action == "quit_to_menu") { state_ = MenuState::Title; return state_; }
    if (action.rfind("mission_", 0) == 0) { state_ = MenuState::Loading; return state_; }
    if (action == "retry_mission" || action == "replay_mission") { state_ = MenuState::Loading; return state_; }
    // Settings/garage actions keep the player on the same screen; the
    // World applies the change and the button label refreshes next frame.
    return state_;
}
std::string GameFlow::loadingTip() const {
    static const char* tips[] = {
        "Tip: ABS stops you shorter on wet roads — toggle it in Settings.",
        "Tip: hold Space while turning for a handbrake turn.",
        "Tip: 50 hidden packages are scattered around the city. Each pays $50.",
        "Tip: rain cuts grip by 30%. Slow down or you'll slide.",
        "Tip: the Vortex R is the fastest car in the garage — and the priciest.",
        "Tip: press F to switch camera, P for photo mode.",
        "Tip: police roadblocks appear at 3 stars. Go around, not through.",
        "Tip: stunt ramps pay by air time. Faster entry, bigger score.",
    };
    size_t idx = static_cast<size_t>(loadingProgress_ * 8.0f) % 8;
    return tips[idx];
}
void GameFlow::setMissionResult(const std::string& name, int stars, int money, bool completed) {
    resultName_ = name; resultStars_ = stars; resultMoney_ = money; resultCompleted_ = completed;
    state_ = completed ? MenuState::MissionComplete : MenuState::MissionFailed;
}
void GameFlow::setFinalStats(int missionsDone, int money, int packages, int bestStunt, float playTimeMinutes) {
    statMissions_ = missionsDone; statMoney_ = money; statPackages_ = packages;
    statBestStunt_ = bestStunt; statPlayTime_ = playTimeMinutes;
    state_ = MenuState::GameComplete;
}
std::string GameFlow::finalStatsText() const {
    return "Missions completed: " + std::to_string(statMissions_) + "/6\n" +
           "Money earned: $" + std::to_string(statMoney_) + "\n" +
           "Packages found: " + std::to_string(statPackages_) + "/50\n" +
           "Best stunt score: " + std::to_string(statBestStunt_) + "\n" +
           "Play time: " + std::to_string(static_cast<int>(statPlayTime_)) + " minutes\n" +
           "Credits: Grand City 6 — an original game. Made in Fiji.\n"
           "No real brands, no licensed content, no Rockstar assets.";
}
const char* GameFlow::howToPlayText() {
    return "HOW TO PLAY\n"
           "W/A/S/D or arrows — drive / walk\n"
           "Space — brake (hold while turning: handbrake turn)\n"
           "E — enter / exit a car\n"
           "1-6 — start a story mission\n"
           "F — switch camera (chase / hood / cockpit)\n"
           "Shift+F — debug overlay\n"
           "P — photo mode (freeze time, free camera, hide HUD)\n"
           "R — switch radio station, M — mute\n"
           "Esc — pause menu\n"
           "Goal: finish all 6 story missions, collect 50 packages,\n"
           "hit all 10 stunt jumps, and own every car in the garage.";
}
} // namespace gc6
