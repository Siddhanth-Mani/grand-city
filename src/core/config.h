#pragma once
#include <string>
#include <unordered_map>
namespace gc6 {
// Minimal INI reader: sections [x], key=value. Missing keys return defaults.
class Config {
public:
    bool load(const std::string& path);
    // Change a value at runtime (Settings menu) — same "section.key" store.
    void set(const std::string& section, const std::string& key, const std::string& value) {
        values_[section + "." + key] = value;
    }
    std::string getString(const std::string& section, const std::string& key,
                          const std::string& def) const;
    int getInt(const std::string& section, const std::string& key, int def) const;
    float getFloat(const std::string& section, const std::string& key, float def) const;
    bool getBool(const std::string& section, const std::string& key, bool def) const;
private:
    std::unordered_map<std::string, std::string> values_;
    static std::string trim(const std::string& s);
};
} // namespace gc6
