#pragma once
#include "renderer/renderer.h"
#include <memory>
namespace gc6 {
// Vulkan backend (Windows with the Vulkan SDK; Linux build planned).
// Header uses pimpl so it compiles anywhere; .cpp is only built when the
// Vulkan SDK is found (see CMakeLists).
class VulkanRenderer : public IRenderer {
public:
    VulkanRenderer(void* windowHandle, int width, int height);
    bool init() override;
    void beginFrame() override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) override;
    void setCamera(const Camera& cam) override;
    void setSun(const Vec3& dir, const Vec3& colour, float intensity) override;
    void present() override;
    void resize(int width, int height) override;
    const char* name() const override { return "vulkan"; }
    size_t drawsThisFrame() const override;
    void drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) override;
    void setLights(const LightRig& rig) override;
    void setPostSettings(const PostSettings& s) override;
    void setWireframe(bool on) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gc6
