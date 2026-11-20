#include "physics/collision.h"
#include <algorithm>
namespace gc6 {
bool resolveAABB(AABB& box, const AABB& solid) {
    if (!aabbOverlap(box, solid)) return false;
    float px = std::min(box.max.x - solid.min.x, solid.max.x - box.min.x);
    float py = std::min(box.max.y - solid.min.y, solid.max.y - box.min.y);
    float pz = std::min(box.max.z - solid.min.z, solid.max.z - box.min.z);
    Vec3 centre = (box.min + box.max) * 0.5f;
    Vec3 solidCentre = (solid.min + solid.max) * 0.5f;
    if (px <= py && px <= pz) {
        float d = centre.x < solidCentre.x ? -px : px;
        box.min.x += d; box.max.x += d;
    } else if (py <= pz) {
        float d = centre.y < solidCentre.y ? -py : py;
        box.min.y += d; box.max.y += d;
    } else {
        float d = centre.z < solidCentre.z ? -pz : pz;
        box.min.z += d; box.max.z += d;
    }
    return true;
}
bool sphereHitsAABB(const Vec3& c, float r, const AABB& b) {
    float x = std::max(b.min.x, std::min(c.x, b.max.x));
    float y = std::max(b.min.y, std::min(c.y, b.max.y));
    float z = std::max(b.min.z, std::min(c.z, b.max.z));
    float dx = c.x - x, dy = c.y - y, dz = c.z - z;
    return dx * dx + dy * dy + dz * dz <= r * r;
}
} // namespace gc6
