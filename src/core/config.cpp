#include "core/config.h"
#include "core/logger.h"
#include <fstream>
#include <sstream>
namespace gc6 {
std::string Config::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) { Logger::warn("Config not found: " + path + " (using defaults)"); return false; }
    std::string section, line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') { section = trim(line.substr(1, line.size() - 2)); continue; }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        values_[section + "." + trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    Logger::info("Config loaded: " + path);
    return true;
}
std::string Config::getString(const std::string& s, const std::string& k, const std::string& d) const {
    auto it = values_.find(s + "." + k);
    return it == values_.end() ? d : it->second;
}
int Config::getInt(const std::string& s, const std::string& k, int d) const {
    auto it = values_.find(s + "." + k);
    if (it == values_.end()) return d;
    try { return std::stoi(it->second); } catch (...) { return d; }
}
float Config::getFloat(const std::string& s, const std::string& k, float d) const {
    auto it = values_.find(s + "." + k);
    if (it == values_.end()) return d;
    try { return std::stof(it->second); } catch (...) { return d; }
}
bool Config::getBool(const std::string& s, const std::string& k, bool d) const {
    std::string v = getString(s, k, d ? "true" : "false");
    return v == "true" || v == "1" || v == "yes";
}
} // namespace gc6
