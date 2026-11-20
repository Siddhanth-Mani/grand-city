#pragma once
// Post-processing chain: bloom (bright-pass + blur), ACES tone mapping,
// vignette, and an FXAA-lite edge filter. Toggles live in settings.ini
// [post]. The HLSL implementation is shaders/post.hlsl (GLSL in
// shaders/glsl/post.*); the null renderer logs the passes it would run so
// the chain is testable headless. Backends implement applyPost() on their
// own render targets; the interface here is the shared settings + pass list.
#include <string>
#include <vector>
namespace gc6 {
struct PostSettings {
    bool bloom = true;
    bool toneMappingACES = true;
    bool vignette = true;
    bool fxaa = true;
    float exposure = 1.1f;
    float bloomThreshold = 0.85f;
    float bloomStrength = 0.6f;
    bool msaa = false; // 4x MSAA on the main target (backend option)
    int msaaSamples = 4;
    float wetness = 0.0f; // 0 dry .. 1 soaked: darker, glossier roads
};
struct PostPass {
    std::string name;
    bool enabled = true;
};
// The ordered pass list for a given settings set (used by backends and tests).
std::vector<PostPass> postPassList(const PostSettings& s);
} // namespace gc6
