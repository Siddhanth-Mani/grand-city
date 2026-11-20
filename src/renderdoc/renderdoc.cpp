#include "renderdoc/renderdoc.h"
#include "core/logger.h"
#ifdef _WIN32
#include <windows.h>
#endif
namespace gc6 {
namespace {
// Minimal subset of the RenderDoc API (matches renderdoc_app.h v1.6).
typedef void (*RENDERDOC_StartFrameCapture)(void*, void*);
typedef void (*RENDERDOC_EndFrameCapture)(void*, void*);
typedef int (*RENDERDOC_GetAPI)(int, void**);
RENDERDOC_StartFrameCapture g_start = nullptr;
RENDERDOC_EndFrameCapture g_end = nullptr;
bool g_available = false;
} // namespace
void RenderDoc::init() {
#ifdef _WIN32
    HMODULE mod = GetModuleHandleA("renderdoc.dll");
    if (!mod) mod = LoadLibraryA("renderdoc.dll");
    if (mod) {
        auto getApi = reinterpret_cast<RENDERDOC_GetAPI>(GetProcAddress(mod, "RENDERDOC_GetAPI"));
        void* api = nullptr;
        if (getApi && getApi(106, &api) == 1 && api) {
            // RENDERDOC_API_1_6_0: StartFrameCapture is at a fixed offset; the
            // real struct layout is documented in renderdoc_app.h. We resolve
            // the two entry points by offset here (see docs/RENDERDOC_PIX.md).
            g_start = *reinterpret_cast<RENDERDOC_StartFrameCapture*>(static_cast<char*>(api) + 8 * sizeof(void*));
            g_end = *reinterpret_cast<RENDERDOC_EndFrameCapture*>(static_cast<char*>(api) + 9 * sizeof(void*));
            g_available = (g_start != nullptr && g_end != nullptr);
        }
    }
    Logger::info(std::string("RenderDoc: ") + (g_available ? "API available" : "not present (captures via the RenderDoc UI still work)"));
#else
    Logger::info("RenderDoc: not available on this platform (use RenderDoc on Windows)");
#endif
}
void RenderDoc::startFrameCapture() { if (g_available && g_start) g_start(nullptr, nullptr); }
void RenderDoc::endFrameCapture() { if (g_available && g_end) g_end(nullptr, nullptr); }
void RenderDoc::endFrame() {}
bool RenderDoc::isAvailable() { return g_available; }
} // namespace gc6
