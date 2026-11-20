#pragma once
#include "renderer/renderer.h"
namespace gc6 {
// Headless renderer: validates every draw (no NaNs, sane counts, valid PBR
// material ranges) and logs a summary per frame, including the post passes
// it would have run. Used on Linux/CI and for automated tests — the game
// logic runs identically, only the pixels are skipped.
class NullRenderer : public IRenderer {
public:
    NullRenderer(int w, int h) : width_(w), height_(h) {}
    bool init() override;
    void beginFrame() override;
    void drawMesh(const MeshData& mesh, const Mat4& model, const Vec3& colour) override;
    void setCamera(const Camera& cam) override { camera_ = cam; }
    void setSun(const Vec3& dir, const Vec3& colour, float intensity) override {
        sunDir_ = dir; sunColour_ = colour; sunIntensity_ = intensity;
    }
    void present() override;
    void resize(int w, int h) override { width_ = w; height_ = h; }
    const char* name() const override { return "null"; }
    size_t drawsThisFrame() const override { return draws_; }
    void drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) override;
    void setLights(const LightRig& rig) override;
    void setPostSettings(const PostSettings& s) override;
    void setWireframe(bool on) override { wireframe_ = on; }
    size_t pbrDraws() const { return pbrDraws_; }
    size_t lightCount() const { return lightCount_; }
private:
    int width_, height_;
    size_t draws_ = 0, totalDraws_ = 0, frames_ = 0;
    size_t pbrDraws_ = 0, lightCount_ = 0;
    bool wireframe_ = false;
    PostSettings post_;
    Camera camera_;
    Vec3 sunDir_{0, -1, 0}, sunColour_{1, 1, 1};
    float sunIntensity_ = 1.0f;
};
std::unique_ptr<IRenderer> createD3D12Renderer(void*, int, int);
std::unique_ptr<IRenderer> createVulkanRenderer(void*, int, int);
std::unique_ptr<IRenderer> createMetalRenderer(void*, int, int);
} // namespace gc6
