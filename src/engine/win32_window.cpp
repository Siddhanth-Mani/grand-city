#ifdef _WIN32
// Minimal Win32 window: registers a class, pumps messages, fills InputState.
#include "engine/window.h"
#include "core/logger.h"
#include <windows.h>
namespace gc6 {
namespace {
InputState* g_input = nullptr;
int keyToGc6(WPARAM vk) {
    switch (vk) {
        case 'W': return Key_W; case 'A': return Key_A; case 'S': return Key_S; case 'D': return Key_D;
        case 'E': return Key_E; case VK_SPACE: return Key_Space; case VK_SHIFT: return Key_Shift;
        case VK_ESCAPE: return Key_Escape; case VK_UP: return Key_Up; case VK_DOWN: return Key_Down;
        case VK_LEFT: return Key_Left; case VK_RIGHT: return Key_Right;
        case 'F': return Key_F; case 'R': return Key_R; case 'M': return Key_M; case 'P': return Key_P;
        default: return -1;
    }
}
LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    if (g_input && (msg == WM_KEYDOWN || msg == WM_KEYUP)) {
        int k = keyToGc6(w);
        if (k >= 0) {
            bool down = (msg == WM_KEYDOWN);
            if (down && !g_input->down[static_cast<size_t>(k)])
                g_input->pressed[static_cast<size_t>(k)] = true;
            g_input->down[static_cast<size_t>(k)] = down;
        }
        return 0;
    }
    return DefWindowProc(h, msg, w, l);
}
} // namespace
class Win32Window : public IWindow {
public:
    bool create(const WindowDesc& d) override {
        width_ = d.width; height_ = d.height;
        // Render at real pixels instead of letting Windows stretch a blurry
        // 1280x720 image on high-DPI displays.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        HINSTANCE inst = GetModuleHandle(nullptr);
        WNDCLASS wc{};
        wc.lpfnWndProc = wndProc; wc.hInstance = inst;
        wc.lpszClassName = L"GrandCity6"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClass(&wc);
        // Fixed-size window whose client area is exactly width x height.
        DWORD style = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        RECT r{0, 0, width_, height_};
        AdjustWindowRect(&r, style, FALSE);
        hwnd_ = CreateWindowEx(0, L"GrandCity6", L"Grand City",
            style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
            nullptr, nullptr, inst, nullptr);
        if (!hwnd_) { Logger::error("CreateWindowEx failed"); return false; }
        ShowWindow(hwnd_, SW_SHOW);
        Logger::info("Win32 window created");
        return true;
    }
    bool pollEvents(InputState& input) override {
        g_input = &input;
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        return true;
    }
    void* nativeHandle() override { return hwnd_; }
    int width() const override { return width_; }
    int height() const override { return height_; }
private:
    HWND hwnd_ = nullptr;
    int width_ = 1280, height_ = 720;
};
std::unique_ptr<IWindow> createWin32Window() { return std::make_unique<Win32Window>(); }
} // namespace gc6
#endif // _WIN32
