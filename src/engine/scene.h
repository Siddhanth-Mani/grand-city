#pragma once
#include "core/math.h"
#include "renderer/renderer.h"
#include <string>
#include <vector>
namespace gc6 {
// A renderable scene entry: a mesh + transform + colour. The scene is a flat
// list (no deep hierarchy) — enough for a city of boxes, cars and people.
// Named SceneEntry (not Entity) to avoid clashing with the ECS Entity id.
struct SceneEntry {
    std::string name;
    const MeshData* mesh = nullptr;
    Mat4 model = Mat4(1.0f);
    Vec3 colour{0.8f, 0.8f, 0.8f};
    bool visible = true;
};
class Scene {
public:
    SceneEntry& add(const std::string& name, const MeshData* mesh, const Mat4& model, const Vec3& colour) {
        entities_.push_back({name, mesh, model, colour, true});
        return entities_.back();
    }
    void clear() { entities_.clear(); }
    void render(IRenderer& r) const {
        for (const auto& e : entities_) if (e.visible && e.mesh) r.drawMesh(*e.mesh, e.model, e.colour);
    }
    size_t count() const { return entities_.size(); }
private:
    std::vector<SceneEntry> entities_;
};
} // namespace gc6
