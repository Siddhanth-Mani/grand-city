#include "engine/window.h"
#include "core/logger.h"
namespace gc6 {
// Null window: headless. pollEvents always succeeds; used on Linux/CI and --headless.
class NullWindow : public IWindow {
public:
    bool create(const WindowDesc& d) override {
        width_ = d.width; height_ = d.height;
        Logger::info("NullWindow created " + std::to_string(width_) + "x" + std::to_string(height_) + " (headless)");
        return true;
    }
    bool pollEvents(InputState&) override { return true; }
    void* nativeHandle() override { return nullptr; }
    int width() const override { return width_; }
    int height() const override { return height_; }
private:
    int width_ = 1280, height_ = 720;
};
std::unique_ptr<IWindow> createWindow() {
#ifdef _WIN32
    return createWin32Window();
#else
    return std::make_unique<NullWindow>();
#endif
}
} // namespace gc6
