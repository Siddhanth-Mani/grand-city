#include "scripting/lua_binding.h"
#include "core/logger.h"
#include <cmath>
#ifdef GC6_USE_LUA
// The github.com/lua/lua mirror tarball ships no lua.hpp, so include the C
// headers directly — this is exactly what lua.hpp does.
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
namespace gc6 {
namespace {
LuaVM* g_vm = nullptr;
int l_log(lua_State* L) {
    Logger::info(std::string("[Lua] ") + (lua_isstring(L, 1) ? lua_tostring(L, 1) : "?"));
    return 0;
}
int l_setObjective(lua_State* L) {
    if (g_vm && lua_isstring(L, 1)) g_vm->setObjective(lua_tostring(L, 1));
    return 0;
}
int l_giveMoney(lua_State* L) {
    if (g_vm) g_vm->gameState().moneyDelta += lua_isnumber(L, 1) ? static_cast<int>(lua_tonumber(L, 1)) : 0;
    return 0;
}
int l_setWanted(lua_State* L) {
    if (g_vm) g_vm->gameState().wantedLevel = lua_isnumber(L, 1) ? static_cast<int>(lua_tonumber(L, 1)) : 0;
    return 0;
}
int l_setMarker(lua_State* L) {
    if (g_vm) {
        auto& g = g_vm->gameState();
        g.marker = Vec3(lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L, 1)) : 0.0f, 0,
                        lua_isnumber(L, 2) ? static_cast<float>(lua_tonumber(L, 2)) : 0.0f);
        g.hasMarker = true;
    }
    return 0;
}
int l_distanceTo(lua_State* L) {
    if (g_vm) {
        const auto& g = g_vm->gameState();
        float x = lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L, 1)) : 0.0f;
        float z = lua_isnumber(L, 2) ? static_cast<float>(lua_tonumber(L, 2)) : 0.0f;
        float dx = x - g.playerPos.x, dz = z - g.playerPos.z;
        lua_pushnumber(L, std::sqrt(dx * dx + dz * dz));
        return 1;
    }
    lua_pushnumber(L, 1e9f);
    return 1;
}
int l_addMoney(lua_State* L) {
    if (g_vm) g_vm->gameState().moneyDelta += lua_isnumber(L, 1) ? static_cast<int>(lua_tonumber(L, 1)) : 0;
    return 0;
}
int l_completeMission(lua_State* L) {
    (void)L;
    if (g_vm) g_vm->gameState().completed = true;
    return 0;
}
int l_failMission(lua_State* L) {
    if (g_vm) {
        g_vm->gameState().failed = true;
        if (lua_isstring(L, 1)) g_vm->gameState().failReason = lua_tostring(L, 1);
    }
    return 0;
}
int l_setTimer(lua_State* L) {
    if (g_vm) {
        g_vm->gameState().timeLeft = lua_isnumber(L, 1) ? static_cast<float>(lua_tonumber(L, 1)) : 0.0f;
        g_vm->gameState().timerSet = true;
    }
    return 0;
}
int l_timeLeft(lua_State* L) {
    lua_pushnumber(L, g_vm ? g_vm->gameState().timeLeft : 0.0f);
    return 1;
}
} // namespace
bool LuaVM::init() {
    auto* L = luaL_newstate();
    if (!L) { Logger::error("Lua: failed to create state"); return false; }
    luaL_openlibs(L);
    g_vm = this;
    lua_register(L, "gc6_log", l_log);
    lua_register(L, "gc6_setObjective", l_setObjective);
    lua_register(L, "gc6_giveMoney", l_giveMoney);
    lua_register(L, "gc6_setWanted", l_setWanted);
    // game.* table for missions 4-6.
    lua_newtable(L);
    lua_pushcfunction(L, l_log); lua_setfield(L, -2, "log");
    lua_pushcfunction(L, l_setObjective); lua_setfield(L, -2, "setObjective");
    lua_pushcfunction(L, l_setMarker); lua_setfield(L, -2, "setMarker");
    lua_pushcfunction(L, l_distanceTo); lua_setfield(L, -2, "distanceTo");
    lua_pushcfunction(L, l_addMoney); lua_setfield(L, -2, "addMoney");
    lua_pushcfunction(L, l_completeMission); lua_setfield(L, -2, "completeMission");
    lua_pushcfunction(L, l_failMission); lua_setfield(L, -2, "failMission");
    lua_pushcfunction(L, l_setTimer); lua_setfield(L, -2, "setTimer");
    lua_pushcfunction(L, l_timeLeft); lua_setfield(L, -2, "timeLeft");
    lua_pushcfunction(L, l_setWanted); lua_setfield(L, -2, "setWanted");
    lua_setglobal(L, "game");
    state_ = L;
    Logger::info("Lua VM initialised (" + std::string(LUA_VERSION) + ")");
    return true;
}
bool LuaVM::loadMission(const std::string& path) {
    if (!state_) return false;
    auto* L = static_cast<lua_State*>(state_);
    if (luaL_dofile(L, path.c_str()) != LUA_OK) {
        Logger::error("Lua error in " + path + ": " + lua_tostring(L, -1));
        lua_pop(L, 1);
        return false; // error contained — engine keeps running
    }
    Logger::info("Lua mission loaded: " + path);
    return true;
}
bool LuaVM::callFunction(const std::string& name, float dt) {
    if (!state_) return false;
    auto* L = static_cast<lua_State*>(state_);
    lua_getglobal(L, name.c_str());
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    lua_pushnumber(L, dt);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        Logger::error("Lua error in " + name + ": " + lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}
bool LuaVM::callStuntJump(float airTime) {
    if (!state_) return false;
    auto* L = static_cast<lua_State*>(state_);
    lua_getglobal(L, "onStuntJump");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    lua_pushnumber(L, airTime);
    if (lua_pcall(L, 1, 0, 0) != LUA_OK) {
        Logger::error(std::string("Lua error in onStuntJump: ") + lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}
void LuaVM::setObjective(const std::string& text) { objective_ = text; game_.objective = text; }
void LuaVM::shutdown() {
    if (state_) { lua_close(static_cast<lua_State*>(state_)); state_ = nullptr; }
}
} // namespace gc6
#else
namespace gc6 {
bool LuaVM::init() { Logger::warn("Lua disabled in this build (GC6_USE_LUA=0)"); return false; }
bool LuaVM::loadMission(const std::string&) { return false; }
bool LuaVM::callFunction(const std::string&, float) { return false; }
bool LuaVM::callStuntJump(float) { return false; }
void LuaVM::setObjective(const std::string& t) { objective_ = t; }
void LuaVM::shutdown() {}
} // namespace gc6
#endif
