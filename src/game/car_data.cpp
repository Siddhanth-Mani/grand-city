#include "game/car_data.h"
#include "core/logger.h"
#include <algorithm>
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
float num(const std::string& obj, const std::string& key, float def) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return def;
    size_t colon = obj.find(':', k + pat.size());
    if (colon == std::string::npos) return def;
    try { return std::stof(obj.substr(colon + 1)); } catch (...) { return def; }
}
std::string str(const std::string& obj, const std::string& key, const std::string& def) {
    std::string pat = "\"" + key + "\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return def;
    size_t colon = obj.find(':', k + pat.size());
    size_t q1 = obj.find('"', colon + 1);
    if (q1 == std::string::npos) return def;
    size_t q2 = obj.find('"', q1 + 1);
    if (q2 == std::string::npos) return def;
    return obj.substr(q1 + 1, q2 - q1 - 1);
}
std::array<float, 6> gearArray(const std::string& obj, const std::array<float, 6>& def) {
    std::string pat = "\"gearRatios\"";
    size_t k = obj.find(pat);
    if (k == std::string::npos) return def;
    size_t b = obj.find('[', k);
    size_t e = obj.find(']', b);
    if (b == std::string::npos || e == std::string::npos) return def;
    std::string inner = obj.substr(b + 1, e - b - 1);
    std::array<float, 6> out = def;
    std::stringstream ss(inner);
    std::string part;
    int i = 0;
    while (std::getline(ss, part, ',') && i < 6) {
        try { out[static_cast<size_t>(i++)] = std::stof(part); } catch (...) {}
    }
    return out;
}
Drivetrain parseDrivetrain(const std::string& s) {
    if (s == "FWD") return Drivetrain::FWD;
    if (s == "AWD") return Drivetrain::AWD;
    if (s == "4x4") return Drivetrain::FourWD;
    return Drivetrain::RWD;
}
CarDef parseCar(const std::string& obj) {
    CarDef d;
    VehicleParams& p = d.params;
    p.name = str(obj, "name", "Sedan");
    p.carClass = str(obj, "class", "sedan");
    p.massKg = num(obj, "massKg", 1400.0f);
    p.engineForce = num(obj, "engineForce", 9000.0f);
    p.brakeForce = num(obj, "brakeForce", 14000.0f);
    p.maxSteerDeg = num(obj, "maxSteerDeg", 32.0f);
    p.grip = num(obj, "grip", 8.0f);
    p.dragCoeff = num(obj, "dragCoeff", 0.35f);
    p.topSpeed = num(obj, "topSpeed", 50.0f);
    p.wheelBase = num(obj, "wheelBase", 2.8f);
    p.suspensionStiffness = num(obj, "suspensionStiffness", 22000.0f);
    p.suspensionDamping = num(obj, "suspensionDamping", 2600.0f);
    p.horsepower = num(obj, "horsepower", 300.0f);
    p.peakTorqueNm = num(obj, "peakTorqueNm", 400.0f);
    p.peakTorqueRpm = num(obj, "peakTorqueRpm", 4500.0f);
    p.redlineRpm = num(obj, "redlineRpm", 6500.0f);
    p.idleRpm = num(obj, "idleRpm", 900.0f);
    p.finalDrive = num(obj, "finalDrive", 3.4f);
    p.gearRatios = gearArray(obj, p.gearRatios);
    p.gearCount = static_cast<int>(num(obj, "gearCount", 5.0f));
    p.drivetrain = parseDrivetrain(str(obj, "drivetrain", "RWD"));
    p.weightDistFront = num(obj, "weightDistFront", 0.52f);
    p.downforceCoeff = num(obj, "downforceCoeff", 0.0f);
    p.frontalArea = num(obj, "frontalArea", 2.2f);
    p.zeroToHundred = num(obj, "zeroToHundred", 7.0f);
    d.price = static_cast<int>(num(obj, "price", 20000.0f));
    d.colourName = str(obj, "colour", "grey");
    return d;
}
} // namespace
std::vector<CarDef> defaultCarCatalogue() {
    std::vector<CarDef> out;
    const char* names[6] = {"Vortex R", "Falcon GT", "Thunderhawk", "Meridian", "Bison 4x4", "Ranger X"};
    for (int i = 0; i < 6; ++i) { CarDef d; d.params.name = names[i]; out.push_back(d); }
    return out;
}
std::vector<CarDef> loadCarCatalogue(const std::string& path) {
    std::string text = readFile(path);
    if (text.empty()) {
        Logger::warn("Car catalogue not found: " + path + " (using built-in defaults)");
        return defaultCarCatalogue();
    }
    // Find the "cars" array, then split its objects by brace depth.
    size_t arr = text.find("\"cars\"");
    if (arr == std::string::npos) return defaultCarCatalogue();
    size_t pos = text.find('[', arr);
    if (pos == std::string::npos) return defaultCarCatalogue();
    std::vector<CarDef> out;
    size_t i = pos + 1;
    while (i < text.size()) {
        size_t open = text.find('{', i);
        if (open == std::string::npos) break;
        int depth = 0;
        size_t j = open;
        for (; j < text.size(); ++j) {
            if (text[j] == '{') ++depth;
            if (text[j] == '}') { --depth; if (depth == 0) break; }
        }
        if (j >= text.size()) break;
        out.push_back(parseCar(text.substr(open, j - open + 1)));
        i = j + 1;
        if (out.size() >= 6) break;
    }
    if (out.empty()) return defaultCarCatalogue();
    Logger::info("Loaded " + std::to_string(out.size()) + " cars from " + path);
    return out;
}
} // namespace gc6
