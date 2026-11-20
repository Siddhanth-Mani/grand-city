#pragma once
#include "renderer/renderer.h"
#include <memory>
namespace gc6 {
// Direct3D 12 backend (Windows only). Implementation uses pimpl so this header
// compiles anywhere; the .cpp is only built on Windows (see CMakeLists).
class D3D12Renderer : public IRenderer {
public:
    D3D12Renderer(void* hwnd, int width, int height);
    ~D3D12Renderer() override;
    bool init() override;
    void beginFrame() override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) override;
    void setCamera(const Camera& cam) override;
    void setSun(const Vec3& dir, const Vec3& colour, float intensity) override;
    void present() override;
    void resize(int width, int height) override;
    const char* name() const override { return "d3d12"; }
    size_t drawsThisFrame() const override;
    void drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) override;
    void setLights(const LightRig& rig) override;
    void setPostSettings(const PostSettings& s) override;
    void setWireframe(bool on) override;
    bool requestScreenshot(const std::string& path) override;
    void drawRect2D(float x, float y, float w, float h, const Vec4& colour) override;
    void drawText2D(float x, float y, float size, const std::string& text, const Vec4& colour) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace gc6
