#pragma once
#include "core/math.h"
#include <string>
namespace gc6 {
// Lua mission scripting. A Lua error is logged and reported — it never crashes
// the engine. When built without Lua (GC6_USE_LUA=0), all calls are safe no-ops.
//
// Two APIs are exposed to scripts:
//  - the legacy gc6_* globals (mission1-3): gc6_log, gc6_setObjective,
//    gc6_giveMoney, gc6_setWanted.
//  - the game.* table (mission4-6): game.log, game.setObjective,
//    game.setMarker(x,z), game.distanceTo(x,z), game.addMoney(n),
//    game.completeMission(), game.failMission(reason), game.setTimer(s),
//    game.timeLeft(), game.setWanted(n).
// The game state (marker, timer, completion) lives in LuaGameState and is
// read by the World each frame; Lua never touches engine objects directly.
struct LuaGameState {
    std::string objective;
    Vec3 marker{0, 0, 0};
    bool hasMarker = false;
    float timeLeft = 0.0f;
    bool timerSet = false;
    int moneyDelta = 0;
    int wantedLevel = -1; // -1 = untouched by the script
    bool completed = false;
    bool failed = false;
    std::string failReason;
    Vec3 playerPos{0, 0, 0};
};
class LuaVM {
public:
    bool init();
    bool loadMission(const std::string& path);
    bool callFunction(const std::string& name, float dt);
    bool callStuntJump(float airTime); // onStuntJump(airTime) if defined
    void setObjective(const std::string& text);
    std::string objective() const { return objective_; }
    LuaGameState& gameState() { return game_; }
    const LuaGameState& gameState() const { return game_; }
    void resetGameState() { game_ = LuaGameState{}; }
    void shutdown();
private:
    void* state_ = nullptr; // lua_State*
    std::string objective_;
    LuaGameState game_;
};
} // namespace gc6
