#pragma once
// Simplified Pacejka "magic formula" tire model. One curve for lateral grip
// (vs slip angle) and one for longitudinal grip (vs slip ratio). The curve
// shape is the classic Pacejka form; B/C/D/E are tuned per surface, not
// measured from a real tire — this is a game model, not a tire lab.
// See docs/PHYSICS.md for the formulas.
#include "core/math.h"
namespace gc6 {
enum class Surface { Tarmac, Wet, Gravel };
struct TireParams {
    float B = 10.0f;  // stiffness factor
    float C = 1.9f;   // shape factor
    float D = 1.0f;   // peak factor (scales with load and surface grip)
    float E = 0.97f;  // curvature factor
};
// Pacejka magic formula: F = D * sin(C * atan(B*x - E*(B*x - atan(B*x)))).
float pacejka(float slip, const TireParams& p);
TireParams tireParamsForSurface(Surface s);
// Combined tire force for one wheel: slipAngle (rad) and slipRatio
// (-1..1) in, load in newtons, returns (longitudinal, lateral) force in N.
Vec2 tireForce(float slipAngle, float slipRatio, float loadN, Surface surface);
// Surface grip multiplier (peak D scale) — tarmac 1.0, wet ~0.6, gravel ~0.45.
float surfaceGrip(Surface s);
// Surface at a world position: beach strip (-Z edge) is gravel, roads are
// tarmac; wetness is a global weather flag set by the World.
Surface surfaceAt(float x, float z, bool wet);
} // namespace gc6
