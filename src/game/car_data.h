#pragma once
// Car catalogue loader: reads data/cars.json (6 fictional cars with realistic
// specs) into VehicleParams. Minimal field-scan parser — the engine has no
// JSON dependency. Falls back to built-in defaults if the file is missing.
#include "physics/vehicle.h"
#include <string>
#include <vector>
namespace gc6 {
struct CarDef {
    VehicleParams params;
    int price = 20000;
    std::string colourName = "grey";
};
std::vector<CarDef> loadCarCatalogue(const std::string& path);
std::vector<CarDef> defaultCarCatalogue();
} // namespace gc6
