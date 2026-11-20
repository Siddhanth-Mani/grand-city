#include "renderer/post.h"
namespace gc6 {
std::vector<PostPass> postPassList(const PostSettings& s) {
    std::vector<PostPass> passes;
    passes.push_back({"scene", true});
    if (s.bloom) {
        passes.push_back({"bright-pass", true});
        passes.push_back({"blur-h", true});
        passes.push_back({"blur-v", true});
        passes.push_back({"bloom-composite", true});
    }
    if (s.toneMappingACES) passes.push_back({"aces-tonemap", true});
    if (s.vignette) passes.push_back({"vignette", true});
    if (s.fxaa) passes.push_back({"fxaa", true});
    return passes;
}
} // namespace gc6
