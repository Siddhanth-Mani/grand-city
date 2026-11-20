#include "renderer/null_renderer.h"
#include "core/logger.h"
#include <cmath>
namespace gc6 {
bool NullRenderer::init() {
    Logger::info("NullRenderer initialised (headless, no GPU output)");
    return true;
}
void NullRenderer::beginFrame() { draws_ = 0; pbrDraws_ = 0; }
void NullRenderer::drawMesh(const MeshData& mesh, const Mat4& model, const Vec3&) {
    // Validate: no NaN in the model matrix, mesh is non-empty.
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        if (std::isnan(model[c][r])) { Logger::error("NullRenderer: NaN in model matrix"); return; }
    if (mesh.positions.empty()) return;
    ++draws_;
}
void NullRenderer::drawMeshPBR(const MeshData& mesh, const Mat4& model, const Material& mat) {
    if (!mat.valid()) { Logger::error("NullRenderer: invalid PBR material " + mat.name); return; }
    ++pbrDraws_;
    drawMesh(mesh, model, mat.albedo);
}
void NullRenderer::setLights(const LightRig& rig) {
    if (rig.lights.size() > LightRig::kMaxLights)
        Logger::error("NullRenderer: light rig exceeds 16 lights");
    lightCount_ = rig.lights.size();
}
void NullRenderer::setPostSettings(const PostSettings& s) { post_ = s; }
void NullRenderer::present() {
    totalDraws_ += draws_;
    ++frames_;
    if (frames_ % 600 == 0) {
        auto passes = postPassList(post_);
        Logger::info("NullRenderer: " + std::to_string(frames_) + " frames, " +
                     std::to_string(totalDraws_) + " draws total, " +
                     std::to_string(lightCount_) + " lights, post passes: " +
                     std::to_string(passes.size()));
    }
}
} // namespace gc6
