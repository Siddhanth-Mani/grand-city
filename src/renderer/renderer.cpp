#include "renderer/renderer.h"
#include "core/logger.h"
#include "renderer/null_renderer.h"
namespace gc6 {
// Fixed-pitch overlay font: each glyph advances 0.55 x the line height.
// UTF-8 sequences count as one glyph (the backends draw them as '-').
float IRenderer::textWidth(const std::string& text, float size) {
    size_t glyphs = 0;
    for (unsigned char c : text)
        if ((c & 0xC0) != 0x80) ++glyphs;
    return static_cast<float>(glyphs) * size * 0.55f;
}
std::unique_ptr<IRenderer> createRenderer(const std::string& name, void* handle, int w, int h) {
    (void)handle; // used only by the platform backends (D3D12/Vulkan/Metal)
    if (name == "d3d12") {
#ifdef _WIN32
        return createD3D12Renderer(handle, w, h);
#else
        Logger::warn("D3D12 requested but this is not Windows; falling back to null renderer.");
#endif
    }
    if (name == "vulkan") {
#if defined(_WIN32) && defined(GC6_HAS_VULKAN)
        return createVulkanRenderer(handle, w, h);
#else
        Logger::warn("Vulkan requested but no Vulkan backend in this build; falling back to null renderer.");
#endif
    }
    if (name == "metal") {
#ifdef __APPLE__
        return createMetalRenderer(handle, w, h);
#else
        Logger::warn("Metal requested but this is not macOS; falling back to null renderer.");
#endif
    }
    return std::make_unique<NullRenderer>(w, h);
}
} // namespace gc6
