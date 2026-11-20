#pragma once
#include <string>
#include <vector>
namespace gc6 {
// Complete game flow — every screen and every button does something real.
// Title -> (New Game / Continue / Garage / Missions / Settings / How to Play)
// -> Loading -> Playing -> Pause (Resume / Settings / Save & Quit to Menu)
// -> Mission Complete / Failed -> Game Complete (stats + credits).
// There are no dead ends: selecting an option always transitions to a real
// state or performs a real action (the World/Application drive the effects).
enum class MenuState {
    Title, Settings, Garage, MissionSelect, HowToPlay,
    Loading, Playing, Paused, MissionComplete, MissionFailed, GameComplete
};
struct MenuButton {
    std::string label;
    std::string action; // id the game layer acts on
    bool enabled = true;
};
class GameFlow {
public:
    MenuState state() const { return state_; }
    void setState(MenuState s) { state_ = s; }
    // Buttons for the current state. enabled=false when the action cannot
    // run yet (e.g. Continue with no save file).
    std::vector<MenuButton> buttons(bool hasSave) const;
    // Handle an action id; returns the new state (or the same state).
    MenuState press(const std::string& action, bool hasSave);
    // Loading screen: progress 0..1 and the tip currently shown.
    float loadingProgress() const { return loadingProgress_; }
    void setLoadingProgress(float p) { loadingProgress_ = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p); }
    std::string loadingTip() const;
    // Mission result screens.
    void setMissionResult(const std::string& name, int stars, int money, bool completed);
    const std::string& resultName() const { return resultName_; }
    int resultStars() const { return resultStars_; }
    int resultMoney() const { return resultMoney_; }
    bool resultCompleted() const { return resultCompleted_; }
    // Game-complete stats.
    void setFinalStats(int missionsDone, int money, int packages, int bestStunt, float playTimeMinutes);
    std::string finalStatsText() const;
    static const char* stateName(MenuState s);
    static const char* howToPlayText();
private:
    MenuState state_ = MenuState::Title;
    float loadingProgress_ = 0.0f;
    std::string resultName_;
    int resultStars_ = 0, resultMoney_ = 0;
    bool resultCompleted_ = false;
    int statMissions_ = 0, statMoney_ = 0, statPackages_ = 0, statBestStunt_ = 0;
    float statPlayTime_ = 0.0f;
};
} // namespace gc6
