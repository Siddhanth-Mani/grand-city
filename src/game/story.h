#pragma once
#include <string>
#include <vector>
namespace gc6 {
// Story campaign: an original story about a new driver in the city.
// Six missions in order, linked by short in-engine cutscenes (camera moves
// + dialogue lines). All characters and dialogue are original fiction —
// no Rockstar content, no real people.
//   1 Harbour Delivery — Ray hires you to move a package; meet Mara.
//   2 City Pickup     — Mara's courier runs; learn the city.
//   3 Lose the Heat   — the police are onto the crew; lose them.
//   4 City Taxi       — go legit for a day; earn fares, stay clean.
//   5 Four-Star Escape— the crew's last job goes wrong; survive the chase.
//   6 Stunt Show      — prove it at the harbour ramps; the crew is free.
// Completing a mission unlocks the next, unlocks a car, and earns a star
// rating (3 stars = fast + no damage, 2 = completed cleanly, 1 = completed).
struct CutsceneLine {
    std::string speaker;   // e.g. "RAY", "MARA", "YOU"
    std::string text;
    float cameraYaw = 0.0f; // camera orbits the player during the line
};
struct StoryMission {
    int index = 0;               // 0..5, matches MissionManager order
    std::string title;
    std::string unlockCar;       // car unlocked on completion ("" = none)
    std::string unlockArea;      // area unlocked ("" = none)
    std::vector<CutsceneLine> intro;   // played before the mission
    std::vector<CutsceneLine> outro;   // played after completion
};
class StoryCampaign {
public:
    StoryCampaign();
    int missionCount() const { return static_cast<int>(missions_.size()); }
    const StoryMission& mission(int index) const { return missions_[static_cast<size_t>(index)]; }
    bool isUnlocked(int index, const std::vector<bool>& completed) const;
    // Star rating: 3 = finished with >= 60% of the time left and no damage,
    // 2 = finished with time left, 1 = finished.
    static int starRating(float timeLeft, float timeLimit, float damage);
    const std::string& introText() const { return introText_; }
private:
    std::vector<StoryMission> missions_;
    std::string introText_ =
        "You are a new driver in Grand City. Ray needs a package moved, "
        "Mara knows every street, and the police know every face. "
        "Drive well, stay free.";
};
} // namespace gc6
