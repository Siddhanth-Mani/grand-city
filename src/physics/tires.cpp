#include "physics/tires.h"
#include <cmath>
namespace gc6 {
float pacejka(float slip, const TireParams& p) {
    float bx = p.B * slip;
    return p.D * std::sin(p.C * std::atan(bx - p.E * (bx - std::atan(bx))));
}
TireParams tireParamsForSurface(Surface s) {
    TireParams p;
    switch (s) {
        case Surface::Tarmac: p = {10.0f, 1.9f, 1.0f, 0.97f}; break;
        case Surface::Wet:    p = {8.0f, 1.9f, 0.62f, 0.97f}; break;
        case Surface::Gravel: p = {6.0f, 1.7f, 0.45f, 0.90f}; break;
    }
    return p;
}
float surfaceGrip(Surface s) {
    switch (s) {
        case Surface::Tarmac: return 1.0f;
        case Surface::Wet: return 0.62f;
        case Surface::Gravel: return 0.45f;
    }
    return 1.0f;
}
Vec2 tireForce(float slipAngle, float slipRatio, float loadN, Surface surface) {
    TireParams p = tireParamsForSurface(surface);
    // Peak force scales with vertical load: Fz * mu (mu ~ D * surface).
    float peak = loadN * surfaceGrip(surface);
    float lat = pacejka(slipAngle, p) * peak;
    float lon = pacejka(slipRatio, p) * peak;
    // Friction circle: combined force cannot exceed the peak.
    float mag = std::sqrt(lat * lat + lon * lon);
    if (mag > peak && mag > 0.0f) {
        lat *= peak / mag;
        lon *= peak / mag;
    }
    return Vec2(lon, lat);
}
Surface surfaceAt(float x, float z, bool wet) {
    if (z < -160.0f) return Surface::Gravel; // beach strip
    if (wet) return Surface::Wet;
    (void)x;
    return Surface::Tarmac;
}
} // namespace gc6
