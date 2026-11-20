// Metal backend — MINIMAL STUB, macOS only.
// This file is only compiled on Apple platforms (see CMakeLists).
// What exists: the MetalRenderer class declaration (metal_renderer.h) and this
// file, which documents the exact steps to complete it. It is NOT a fake full
// backend: a complete Metal renderer needs MetalKit + a CAMetalLayer, an
// MTLRenderPipelineState built from .metal shaders, and per-frame command
// buffers. The interface it must satisfy is IRenderer (renderer.h), so the
// game code does not change when the backend is finished.
// To complete it on a Mac:
//   1. Add a CAMetalLayer to the NSWindow content view.
//   2. Create MTLDevice, MTLCommandQueue, MTLLibrary from shaders/metal/lit.metal.
//   3. Implement beginFrame/drawMesh/present with MTLRenderCommandEncoder.
//   4. Keep the same Camera/sun constant-buffer layout as the D3D12 backend.
// Until then, macOS builds fall back to the null renderer.
#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#include "renderer/metal/metal_renderer.h"
namespace gc6 {
struct MetalRenderer::Impl { id<MTLDevice> device = nil; id<MTLCommandQueue> queue = nil; };
MetalRenderer::MetalRenderer(void*, int, int) : impl_(std::make_unique<Impl>()) {}
bool MetalRenderer::init() { impl_->device = MTLCreateSystemDefaultDevice(); return impl_->device != nil; }
void MetalRenderer::beginFrame() {}
void MetalRenderer::drawMesh(const MeshData&, const Mat4&, const Vec3&) {}
void MetalRenderer::setCamera(const Camera&) {}
void MetalRenderer::setSun(const Vec3&, const Vec3&, float) {}
void MetalRenderer::present() {}
void MetalRenderer::resize(int, int) {}
size_t MetalRenderer::drawsThisFrame() const { return 0; }
std::unique_ptr<IRenderer> createMetalRenderer(void* h, int w, int hh) {
    return std::make_unique<MetalRenderer>(h, w, hh);
}
} // namespace gc6
#endif
