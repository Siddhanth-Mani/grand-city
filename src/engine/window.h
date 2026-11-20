#pragma once
#include "core/input.h"
#include <memory>
#include <string>
namespace gc6 {
struct WindowDesc { int width = 1280, height = 720; std::string title = "Grand City 6"; bool fullscreen = false; bool noActivate = false; };
// Platform window. Win32 on Windows; NullWindow elsewhere (headless/CI).
class IWindow {
public:
    virtual ~IWindow() = default;
    virtual bool create(const WindowDesc& desc) = 0;
    virtual bool pollEvents(InputState& input) = 0; // false = quit requested
    virtual void* nativeHandle() = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
};
std::unique_ptr<IWindow> createWindow();
#ifdef _WIN32
std::unique_ptr<IWindow> createWin32Window();
#endif
} // namespace gc6
