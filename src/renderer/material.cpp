#include "renderer/material.h"
#include "core/logger.h"
#include <fstream>
#include <sstream>
namespace gc6 {
namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
float findNumber(const std::string& obj, const std::string& key, float def) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return def;
    size_t colon = obj.find(':', k + pat.size());
    if (colon == std::string::npos) return def;
    try { return std::stof(obj.substr(colon + 1)); } catch (...) { return def; }
}
Vec3 findVec3(const std::string& obj, const std::string& key, Vec3 def) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return def;
    size_t b = obj.find('[', k);
    size_t e = obj.find(']', b);
    if (b == std::string::npos || e == std::string::npos) return def;
    std::string inner = obj.substr(b + 1, e - b - 1);
    try {
        std::stringstream ss(inner);
        std::string part;
        float v[3] = {def.x, def.y, def.z};
        int i = 0;
        while (std::getline(ss, part, ',') && i < 3) v[i++] = std::stof(part);
        return Vec3(v[0], v[1], v[2]);
    } catch (...) { return def; }
}
std::string findString(const std::string& obj, const std::string& key, const std::string& def) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return def;
    size_t q1 = obj.find('"', k + pat.size() + 1);
    if (q1 == std::string::npos) return def;
    size_t q2 = obj.find('"', q1 + 1);
    if (q2 == std::string::npos) return def;
    return obj.substr(q1 + 1, q2 - q1 - 1);
}
} // namespace
std::vector<Material> loadMaterials(const std::string& path) {
    std::vector<Material> out;
    std::string text = readFile(path);
    if (text.empty()) { Logger::warn("Materials file not found: " + path); return out; }
    size_t pos = 0;
    while (true) {
        size_t open = text.find('{', pos);
        if (open == std::string::npos) break;
        size_t close = text.find('}', open);
        if (close == std::string::npos) break;
        std::string obj = text.substr(open, close - open + 1);
        Material m;
        m.name = findString(obj, "name", "unnamed");
        m.albedo = findVec3(obj, "albedo", Vec3(0.8f));
        m.metalness = findNumber(obj, "metalness", 0.0f);
        m.roughness = findNumber(obj, "roughness", 0.6f);
        m.normalStrength = findNumber(obj, "normalStrength", 1.0f);
        m.emissive = findVec3(obj, "emissive", Vec3(0, 0, 0));
        if (m.valid()) out.push_back(m);
        pos = close + 1;
    }
    Logger::info("Loaded " + std::to_string(out.size()) + " PBR materials from " + path);
    return out;
}
const Material* findMaterial(const std::vector<Material>& mats, const std::string& name) {
    for (const auto& m : mats) if (m.name == name) return &m;
    return nullptr;
}
} // namespace gc6
