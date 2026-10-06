// Windows: whether this machine can create an OpenGL 3.3 core context, the least OGRE's GL3+ render
// system runs on (OgreGL3PlusRenderSystem.cpp:1390, OgreAssert(hasMinGLVersion(3, 3))). Without
// a GPU driver Windows only has its own OpenGL 1.1 (no WGL extensions), and OGRE's Win32 GL window
// crashes on it before reaching that check. platform::CanCreateGl33Context is this probe; the step
// injection is for platform_win_test.

#pragma once

namespace torchlight::platform::gl_probe {

// The probe's steps, in order. Each one creates something the probe releases before returning.
enum class Step {
  kRegisterClass,      // the window class
  kCreateWindow,       // a hidden 1x1 window of it
  kPixelFormat,        // its device context and a GL pixel format
  kLegacyContext,      // a plain wglCreateContext context, made current
  kGetProcAddress,     // wglCreateContextAttribsARB (an ICD's; Windows' own GL has none)
  kCoreContext,        // the 3.3 core context, made current
  kNone,               // no step fails on purpose
};

// True when every step succeeds. `fail_at` makes that step fail as if the system refused it. Either
// way everything created is released and the GL context current before the call is current again.
bool CanCreateGl33Context(Step fail_at = Step::kNone);

// The window class the probe registers, to check that nothing is left behind.
inline constexpr wchar_t kWindowClass[] = L"TorchlightRecompGlProbe";

}  // namespace torchlight::platform::gl_probe
