// The OpenGL 3.3 probe (gl_probe_win.h): the classic WGL sequence, a legacy context to reach
// wglCreateContextAttribsARB and then the context OGRE would ask for, each step undone in reverse
// order whatever happens after it.

#include "platform/gl_probe_win.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace torchlight::platform::gl_probe {

namespace {

// WGL_ARB_create_context and WGL_ARB_create_context_profile (Khronos wglext.h).
constexpr int kContextMajorVersionArb = 0x2091;
constexpr int kContextMinorVersionArb = 0x2092;
constexpr int kContextProfileMaskArb = 0x9126;
constexpr int kContextCoreProfileBitArb = 0x00000001;
using CreateContextAttribsArb = HGLRC(WINAPI*)(HDC, HGLRC, const int*);

// Everything the probe made, released in reverse order by the destructor.
class Probe {
 public:
  Probe() : previous_dc_(wglGetCurrentDC()), previous_context_(wglGetCurrentContext()) {}
  ~Probe() {
    wglMakeCurrent(previous_dc_, previous_context_);
    if (core_) wglDeleteContext(core_);
    if (legacy_) wglDeleteContext(legacy_);
    if (dc_) ReleaseDC(window_, dc_);
    if (window_) DestroyWindow(window_);
    if (registered_) UnregisterClassW(kWindowClass, instance_);
  }
  Probe(const Probe&) = delete;
  Probe& operator=(const Probe&) = delete;

  bool Run(Step fail_at) {
    auto fails = [&](Step step, bool ok) { return step == fail_at || !ok; };

    instance_ = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance_;
    window_class.lpszClassName = kWindowClass;
    registered_ = fail_at != Step::kRegisterClass && RegisterClassExW(&window_class) != 0;
    if (!registered_) return false;

    if (fail_at != Step::kCreateWindow) {
      window_ = CreateWindowExW(0, kWindowClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                instance_, nullptr);
    }
    if (!window_) return false;

    dc_ = GetDC(window_);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;
    const int format = dc_ ? ChoosePixelFormat(dc_, &pfd) : 0;
    if (fails(Step::kPixelFormat, format != 0 && SetPixelFormat(dc_, format, &pfd))) return false;

    if (fail_at != Step::kLegacyContext) legacy_ = wglCreateContext(dc_);
    if (!legacy_ || !wglMakeCurrent(dc_, legacy_)) return false;

    const auto create = reinterpret_cast<CreateContextAttribsArb>(
        reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));
    if (fails(Step::kGetProcAddress, create != nullptr)) return false;

    const int attributes[] = {kContextMajorVersionArb, 3, kContextMinorVersionArb, 3,
                              kContextProfileMaskArb, kContextCoreProfileBitArb, 0};
    if (fail_at != Step::kCoreContext) core_ = create(dc_, nullptr, attributes);
    return core_ && wglMakeCurrent(dc_, core_);
  }

 private:
  HDC previous_dc_;
  HGLRC previous_context_;
  HINSTANCE instance_ = nullptr;
  bool registered_ = false;
  HWND window_ = nullptr;
  HDC dc_ = nullptr;
  HGLRC legacy_ = nullptr;
  HGLRC core_ = nullptr;
};

}  // namespace

bool CanCreateGl33Context(Step fail_at) {
  Probe probe;
  return probe.Run(fail_at);
}

}  // namespace torchlight::platform::gl_probe
