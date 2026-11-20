#pragma once
#include "core/math.h"
#include "engine/resource_manager.h"
#include "renderer/lights.h"
#include "renderer/material.h"
#include "renderer/post.h"
#include <memory>
#include <string>
namespace gc6 {
struct Camera {
    Vec3 position{0, 8, -12}, target{0, 0, 0}, up{0, 1, 0};
    float fov = 70.0f, aspect = 16.0f / 9.0f, nearPlane = 0.1f, farPlane = 800.0f;
    Mat4 view() const { return lookAt(position, target, up); }
    Mat4 proj() const { return perspective(fov, aspect, nearPlane, farPlane); }
};
// Every backend implements this. Game code never touches D3D12/Vulkan directly.
// The PBR / lights / post virtuals have default implementations (plain lit
// draw) so older backends keep working; the null renderer overrides them to
// validate and log.
class IRenderer {
public:
    virtual ~IRenderer() = default;
    virtual bool init() = 0;
    virtual void beginFrame() = 0;
    virtual void drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) = 0;
    virtual void setCamera(const Camera& cam) = 0;
    virtual void setSun(const Vec3& direction, const Vec3& colour, float intensity) = 0;
    virtual void present() = 0;
    virtual void resize(int width, int height) = 0;
    virtual const char* name() const = 0;
    virtual size_t drawsThisFrame() const = 0;
    // --- v2 extensions (defaults keep old backends working) ---
    virtual void drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) {
        drawMesh(mesh, model, mat.albedo);
    }
    virtual void setLights(const LightRig& rig) { (void)rig; }
    virtual void setPostSettings(const PostSettings& s) { (void)s; }
    virtual void setWireframe(bool) {}
    // --- 2D overlay (menus, HUD). Coordinates are pixels on a virtual
    // 1280x720 canvas, scaled to the window; drawn on top of the 3D frame.
    // Backends without an overlay (null) ignore them. ---
    virtual void drawRect2D(float x, float y, float w, float h, const Vec4& colour) {
        (void)x; (void)y; (void)w; (void)h; (void)colour;
    }
    // Text in a fixed-pitch font; size = line height in canvas pixels.
    virtual void drawText2D(float x, float y, float size, const std::string& text, const Vec4& colour) {
        (void)x; (void)y; (void)size; (void)text; (void)colour;
    }
    // Width in canvas pixels that drawText2D would use for this text.
    static float textWidth(const std::string& text, float size);
};
std::unique_ptr<IRenderer> createRenderer(const std::string& name, void* windowHandle,
                                          int width, int height);
} // namespace gc6
