#pragma once
// Metal backend declaration. Only usable on Apple platforms; on other
// platforms this header intentionally declares nothing.
#if defined(__APPLE__)
#include "renderer/renderer.h"
#include <memory>
namespace gc6 {
class MetalRenderer : public IRenderer {
public:
    MetalRenderer(void* windowHandle, int width, int height);
    bool init() override;
    void beginFrame() override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) override;
    void setCamera(const Camera& cam) override;
    void setSun(const Vec3& dir, const Vec3& colour, float intensity) override;
    void present() override;
    void resize(int width, int height) override;
    const char* name() const override { return "metal"; }
    size_t drawsThisFrame() const override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gc6
#endif
