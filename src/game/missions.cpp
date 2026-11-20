#include "game/missions.h"
#include "core/logger.h"
#include <fstream>
#include <sstream>
namespace gc6 {
namespace {
// Minimal field-scan over data/missions.json (same approach as car_data):
// split the top-level "missions" array into objects, then pull string/int
// fields out of each object by key. No JSON dependency.
std::vector<std::string> splitObjects(const std::string& text) {
    // Objects inside the top-level array sit at depth 2 (root object = 1);
    // the root object itself is not a mission and is skipped.
    std::vector<std::string> out;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{') {
            if (depth == 1) start = i;
            ++depth;
        } else if (text[i] == '}') {
            --depth;
            if (depth == 1 && start != std::string::npos) {
                out.push_back(text.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return out;
}
std::string findString(const std::string& obj, const std::string& key) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return {};
    size_t colon = obj.find(':', k + pat.size());
    if (colon == std::string::npos) return {};
    size_t q1 = obj.find('"', colon);
    if (q1 == std::string::npos) return {};
    size_t q2 = obj.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return obj.substr(q1 + 1, q2 - q1 - 1);
}
int findInt(const std::string& obj, const std::string& key, int fallback) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return fallback;
    size_t colon = obj.find(':', k + pat.size());
    if (colon == std::string::npos) return fallback;
    try { return std::stoi(obj.substr(colon + 1)); } catch (...) { return fallback; }
}
} // namespace
void MissionManager::loadDefinitions(const std::string& dataDir) {
    missions_.clear();
    std::ifstream in(dataDir + "/missions.json");
    if (!in) {
        Logger::error("MissionManager: cannot open " + dataDir + "/missions.json");
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    for (const auto& obj : splitObjects(ss.str())) {
        MissionDef d;
        d.id = findString(obj, "id");
        d.name = findString(obj, "name");
        d.description = findString(obj, "description");
        d.luaFile = dataDir + "/../scripts/missions/" + findString(obj, "lua");
        d.rewardMoney = findInt(obj, "reward", 0);
        d.timeLimit = static_cast<float>(findInt(obj, "timeLimit", 120));
        if (d.id.empty() || d.name.empty()) continue;
        Mission m;
        m.def = d;
        missions_.push_back(m);
    }
    Logger::info("Loaded " + std::to_string(missions_.size()) + " mission definitions");
}
void MissionManager::setMissionState(int index, MissionState state) {
    if (index < 0 || index >= static_cast<int>(missions_.size())) return;
    missions_[static_cast<size_t>(index)].state = state;
}
bool MissionManager::startMission(int index) {
    if (index < 0 || index >= static_cast<int>(missions_.size())) return false;
    if (active()) return false;
    Mission& m = missions_[static_cast<size_t>(index)];
    m.state = MissionState::Active;
    m.timeLeft = m.def.timeLimit;
    m.progress = 0;
    m.goal = m.def.id == "collect" ? 5 : 1;
    m.target = m.def.id == "delivery" ? Vec3(-160, 0, -180) : Vec3(120, 0, 120);
    Logger::info("Mission started: " + m.def.name);
    return true;
}
void MissionManager::update(float dt, const Vec3& playerPos) {
    Mission* m = active();
    if (!m) return;
    m->timeLeft -= dt;
    if (m->timeLeft <= 0.0f) { m->state = MissionState::Failed; Logger::info("Mission failed: " + m->def.name); return; }
    // C++-owned arrival logic only for the first three missions; taxi,
    // escape-time and stunt are Lua-driven (World reads LuaGameState).
    if (m->def.id != "delivery" && m->def.id != "collect") return;
    if (length(Vec3(m->target - playerPos)) < 8.0f) {
        ++m->progress;
        if (m->progress >= m->goal) {
            m->state = MissionState::Completed;
            moneyEarned_ += m->def.rewardMoney;
            Logger::info("Mission completed: " + m->def.name + " (+$" + std::to_string(m->def.rewardMoney) + ")");
        } else {
            m->target = Vec3(-m->target.x, 0, -m->target.z); // next pickup across town
        }
    }
}
void MissionManager::onEvent(const std::string& eventName, const Vec3&) {
    Mission* m = active();
    if (!m) return;
    if (eventName == "package_collected") {
        ++m->progress;
        if (m->progress >= m->goal) {
            m->state = MissionState::Completed;
            moneyEarned_ += m->def.rewardMoney;
        }
    }
}
} // namespace gc6
