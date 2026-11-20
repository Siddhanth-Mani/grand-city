#pragma once
#include "core/math.h"
#include <string>
#include <vector>
namespace gc6 {
struct MissionDef {
    std::string id, name, description, luaFile;
    int rewardMoney = 0;
    float timeLimit = 0.0f;
};
enum class MissionState { NotStarted, Active, Completed, Failed };
struct Mission {
    MissionDef def;
    MissionState state = MissionState::NotStarted;
    float timeLeft = 0.0f;
    Vec3 target{0, 0, 0};
    int progress = 0, goal = 1;
};
class MissionManager {
public:
    void loadDefinitions(const std::string& dataDir);
    bool startMission(int index);
    // Restore a completed state from a save file.
    void setMissionState(int index, MissionState state);
    void update(float dt, const Vec3& playerPos);
    void onEvent(const std::string& eventName, const Vec3& pos);
    const std::vector<Mission>& missions() const { return missions_; }
    Mission* active() {
        for (auto& m : missions_) if (m.state == MissionState::Active) return &m;
        return nullptr;
    }
    int moneyEarned() const { return moneyEarned_; }
private:
    std::vector<Mission> missions_;
    int moneyEarned_ = 0;
};
} // namespace gc6
