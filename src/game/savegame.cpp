#include "game/savegame.h"
#include "core/logger.h"
#include <fstream>
#include <sstream>
namespace gc6 {
namespace {
std::string boolArr(const bool* arr, size_t n) {
    std::string s = "[";
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ", ";
        s += arr[i] ? "true" : "false";
    }
    return s + "]";
}
bool findNumber(const std::string& text, const std::string& key, float& out) {
    std::string pat = "\"" + key + "\"";
    size_t k = text.find(pat);
    if (k == std::string::npos) return false;
    size_t colon = text.find(':', k + pat.size());
    if (colon == std::string::npos) return false;
    try { out = std::stof(text.substr(colon + 1)); return true; } catch (...) { return false; }
}
bool findIntArray(const std::string& text, const std::string& key, int* out, size_t n) {
    std::string pat = "\"" + key + "\"";
    size_t k = text.find(pat);
    if (k == std::string::npos) return false;
    size_t b = text.find('[', k);
    size_t e = text.find(']', b);
    if (b == std::string::npos || e == std::string::npos) return false;
    std::string inner = text.substr(b + 1, e - b - 1);
    size_t pos = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t comma = inner.find(',', pos);
        std::string tok = inner.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        try { out[i] = std::stoi(tok); } catch (...) { return false; }
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    return true;
}
std::string intArr(const int* arr, size_t n) {
    std::string s = "[";
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ", ";
        s += std::to_string(arr[i]);
    }
    return s + "]";
}
std::string upgradesArr(const std::array<std::array<int, 4>, 6>& u) {
    std::string s = "[";
    for (size_t c = 0; c < 6; ++c) {
        if (c) s += ", ";
        s += intArr(u[c].data(), 4);
    }
    return s + "]";
}
bool findBoolArray(const std::string& text, const std::string& key, bool* out, size_t n) {
    std::string pat = "\"" + key + "\"";
    size_t k = text.find(pat);
    if (k == std::string::npos) return false;
    size_t b = text.find('[', k);
    size_t e = text.find(']', b);
    if (b == std::string::npos || e == std::string::npos) return false;
    std::string inner = text.substr(b + 1, e - b - 1);
    size_t pos = 0;
    for (size_t i = 0; i < n; ++i) {
        size_t t = inner.find("true", pos);
        size_t f = inner.find("false", pos);
        if (t == std::string::npos && f == std::string::npos) return false;
        if (t != std::string::npos && (f == std::string::npos || t < f)) { out[i] = true; pos = t + 4; }
        else { out[i] = false; pos = f + 5; }
    }
    return true;
}
} // namespace
bool saveGame(const std::string& path, const SaveData& d) {
    std::ofstream out(path);
    if (!out) { Logger::error("Save failed: cannot write " + path); return false; }
    out << "{\n";
    out << "  \"formatVersion\": " << d.formatVersion << ",\n";
    out << "  \"playerX\": " << d.playerPos.x << ", \"playerY\": " << d.playerPos.y
        << ", \"playerZ\": " << d.playerPos.z << ",\n";
    out << "  \"money\": " << d.money << ",\n";
    out << "  \"carsOwned\": " << boolArr(d.carsOwned.data(), d.carsOwned.size()) << ",\n";
    out << "  \"currentCar\": " << d.currentCar << ",\n";
    out << "  \"paintIndex\": " << d.paintIndex << ",\n";
    out << "  \"missionsCompleted\": " << boolArr(d.missionsCompleted.data(), d.missionsCompleted.size()) << ",\n";
    out << "  \"wantedHeat\": " << d.wantedHeat << ",\n";
    out << "  \"timeOfDay\": " << d.timeOfDay << ",\n";
    out << "  \"collectiblesFound\": " << d.collectiblesFound << ",\n";
    out << "  \"bestStuntScore\": " << d.bestStuntScore << ",\n";
    out << "  \"starRatings\": " << intArr(d.starRatings.data(), d.starRatings.size()) << ",\n";
    out << "  \"upgrades\": " << upgradesArr(d.upgrades) << ",\n";
    out << "  \"playTimeMinutes\": " << d.playTimeMinutes << "\n";
    out << "}\n";
    Logger::info("Game saved to " + path);
    return true;
}
bool loadGame(const std::string& path, SaveData& out) {
    std::ifstream in(path);
    if (!in) { Logger::warn("No save file: " + path); return false; }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    float v = 0.0f;
    if (!findNumber(text, "formatVersion", v) || static_cast<int>(v) != kSaveFormatVersion) {
        Logger::error("Save file is an unsupported version");
        return false;
    }
    out = SaveData{};
    float x = 0, y = 0, z = 0;
    findNumber(text, "playerX", x); findNumber(text, "playerY", y); findNumber(text, "playerZ", z);
    out.playerPos = Vec3(x, y, z);
    if (findNumber(text, "money", v)) out.money = static_cast<int>(v);
    findBoolArray(text, "carsOwned", out.carsOwned.data(), out.carsOwned.size());
    if (findNumber(text, "currentCar", v)) out.currentCar = static_cast<int>(v);
    if (findNumber(text, "paintIndex", v)) out.paintIndex = static_cast<int>(v);
    findBoolArray(text, "missionsCompleted", out.missionsCompleted.data(), out.missionsCompleted.size());
    if (findNumber(text, "wantedHeat", v)) out.wantedHeat = v;
    if (findNumber(text, "timeOfDay", v)) out.timeOfDay = v;
    if (findNumber(text, "collectiblesFound", v)) out.collectiblesFound = static_cast<int>(v);
    if (findNumber(text, "bestStuntScore", v)) out.bestStuntScore = static_cast<int>(v);
    findIntArray(text, "starRatings", out.starRatings.data(), out.starRatings.size());
    // upgrades: nested arrays — parse the flat int sequence after the key.
    {
        std::string pat = "\"upgrades\"";
        size_t k = text.find(pat);
        if (k != std::string::npos) {
            size_t outer = text.find('[', k); // skip the outer array bracket
            size_t pos = outer == std::string::npos ? k : outer + 1;
            for (size_t c = 0; c < 6; ++c) {
                size_t b = text.find('[', pos);
                if (b == std::string::npos) break;
                size_t e = text.find(']', b);
                if (e == std::string::npos) break;
                std::string inner = text.substr(b + 1, e - b - 1);
                size_t p2 = 0;
                for (size_t u = 0; u < 4; ++u) {
                    size_t comma = inner.find(',', p2);
                    std::string tok = inner.substr(p2, comma == std::string::npos ? std::string::npos : comma - p2);
                    try { out.upgrades[c][u] = std::stoi(tok); } catch (...) {}
                    if (comma == std::string::npos) break;
                    p2 = comma + 1;
                }
                pos = e + 1;
            }
        }
    }
    if (findNumber(text, "playTimeMinutes", v)) out.playTimeMinutes = v;
    Logger::info("Game loaded from " + path);
    return true;
}
} // namespace gc6
