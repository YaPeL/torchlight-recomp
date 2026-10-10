// macOS implementation of the platform module: the game window is SDL's (Cocoa); OGRE GL3+ draws
// in its content view. Apple Silicon has one GPU, so there is no GPU choice.

#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/user_folders.h"

#include <climits>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <mach-o/dyld.h>
#include <sys/sysctl.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace torchlight::platform {

namespace {

NSWindow* CocoaWindow(SDL_Window* window) {
  return (NSWindow*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                           SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}

// <user folder of `kind`>/<sub>, created if missing, ending in "/"; empty when there is none.
std::string UserDir(UserFolderKind kind, const std::filesystem::path& sub = {}) {
  const std::filesystem::path folder = UserFolder(kind);
  if (folder.empty()) return "";
  const std::filesystem::path dir = sub.empty() ? folder : folder / sub;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return "";
  return dir.string() + "/";
}

// The chip's name ("Apple M2"), from the kernel.
std::string ChipName() {
  char name[256] = {};
  size_t size = sizeof(name) - 1;
  if (sysctlbyname("machdep.cpu.brand_string", name, &size, nullptr, 0) != 0) return "Apple GPU";
  return name;
}

}  // namespace

bool NativeGameWindow(SDL_Window* game, NativeWindow& window) {
  window = NativeWindow();
  window.system = NativeWindow::kCocoa;
  NSWindow* cocoa = CocoaWindow(game);
  window.window = cocoa ? uint64_t(uintptr_t([cocoa contentView])) : 0;
  return window.window != 0;
}

void BringToFront() {
  [NSApplication sharedApplication];
  [NSApp activateIgnoringOtherApps:YES];
}

std::string EmbeddingVideoError() {
  if (VideoDriver() == "cocoa") return "";
  return "SDL video driver " + VideoDriver() + ": the backend draws only in cocoa windows";
}

// A window server session (none over plain SSH or in a daemon).
bool HasDisplay() {
  CFDictionaryRef session = CGSessionCopyCurrentDictionary();
  if (!session) return false;
  CFRelease(session);
  return true;
}

std::string ShaderCacheDir() { return UserDir(UserFolderKind::kCache, "ogre"); }

std::string CacheDir(const std::string& name) { return UserDir(UserFolderKind::kCache, name); }

std::vector<Gpu> Gpus() { return {{"0", ChipName(), "apple", true}}; }

std::vector<Gpu> AllGpus() { return Gpus(); }

std::vector<Gpu> SelectableGpus(const std::vector<Gpu>& all) { return all; }

std::vector<std::pair<std::string, std::string>> GpuEnvironment(const std::vector<Gpu>&,
                                                                const std::string&) {
  return {};
}

bool SelectGpu(const std::string& id, std::string& error) {
  if (id.empty() || id == "0") return true;
  error = "GPU " + id + " is not one the backend can be pointed at (one GPU on this Mac)";
  return false;
}

bool GpuChoiceNeedsDirect3D11() { return false; }

std::string RenderingDeviceForGpu(const std::string&, const std::vector<std::string>&) {
  return "";
}

std::string ExecutableDir() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string path(size, '\0');
  if (_NSGetExecutablePath(path.data(), &size) != 0) return "";
  std::error_code ec;
  const std::filesystem::path exe = std::filesystem::canonical(path.c_str(), ec);
  if (ec) return "";
  return exe.parent_path().string() + "/";
}

std::string ConfigDir() { return UserDir(UserFolderKind::kConfig); }

std::string DataDir() { return UserDir(UserFolderKind::kData); }

std::string GameDataDir() {
  const std::filesystem::path folder = UserFolder(UserFolderKind::kGameData);
  return folder.empty() ? "" : folder.string() + "/";
}

// ~/Library/Logs/TorchlightRecomp/ itself: Logs is already the logs' place.
std::string LogDir() { return UserDir(UserFolderKind::kState); }

namespace {

// The app bundle's Contents/ (ending in "/") when the executable runs from one (Contents/MacOS/),
// else empty (the build tree).
std::string BundleContentsDir() {
  const std::string exe = ExecutableDir();
  const std::string macos = "/Contents/MacOS/";
  if (!exe.ends_with(macos)) return "";
  return exe.substr(0, exe.size() - std::string("MacOS/").size());
}

}  // namespace

std::string ResourceDir() {
  const std::string contents = BundleContentsDir();
  return contents.empty() ? ExecutableDir() : contents + "Resources/";
}

// In the bundle, plugins are code: Contents/PlugIns/ (packaging/macos/make_app.sh).
std::string OgrePluginDir() {
  const std::string contents = BundleContentsDir();
  if (!contents.empty()) return contents + "PlugIns/ogre/";
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre/plugins/";
}

std::string OgreMediaDir() {
  const std::string resources = ResourceDir();
  return resources.empty() ? "" : resources + "ogre/media/";
}

// SDL's Cocoa windows are drawn by the window server with the window's background colour until
// something draws in them; it is set black on the game window when it appears.
void BlackGameWindowBackground() {
  SDL_AddEventWatch(
      [](void*, SDL_Event* event) {
        static bool done = false;
        if (done || event->type < SDL_EVENT_WINDOW_FIRST || event->type > SDL_EVENT_WINDOW_LAST) {
          return true;
        }
        SDL_Window* window = SDL_GetWindowFromID(event->window.windowID);
        NSWindow* cocoa = window ? CocoaWindow(window) : nil;
        if (cocoa) {
          [cocoa setBackgroundColor:[NSColor blackColor]];
          done = true;
        }
        return true;
      },
      nullptr);
}

// OGRE's Cocoa window takes the game window's content view as its own (no child window), at the
// view's backing scale (Retina).
std::vector<std::pair<std::string, std::string>> OgreWindowParams(const NativeWindow& window) {
  NSView* view = (NSView*)uintptr_t(window.window);
  const double scale = [view window] ? [[view window] backingScaleFactor] : 1.0;
  return {{"externalWindowHandle", std::to_string(window.window)},
          {"contentScalingFactor", std::to_string(scale)}};
}

std::string OgreRenderSystemDir(const std::string& plugin_dir, const NativeWindow*) {
  return plugin_dir;
}

// The main thread runs the main dispatch queue from its run loop, in the run loop's common modes:
// while SDL waits for events, while a window is resized with the pointer and under a modal alert.
// OpenGL and its AppKit classes are deprecated on Apple; GL3+ is the render system chosen for macOS
// anyway (docs/macos-port.md, section 4).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
void RunOnWindowThread(const std::function<void()>& work) {
  if ([NSThread isMainThread]) {
    work();
    return;
  }
  // A GL context is current on one thread at a time: the caller's moves to the main thread for
  // the work, and the one the work leaves current (OGRE's, after creating the window) moves back.
  __block NSOpenGLContext* context = [[NSOpenGLContext currentContext] retain];
  [NSOpenGLContext clearCurrentContext];
  __block std::exception_ptr failure;
  dispatch_sync(dispatch_get_main_queue(), ^{
    [context makeCurrentContext];
    [context release];
    try {
      work();
    } catch (...) {
      failure = std::current_exception();  // exceptions must not cross libdispatch
    }
    context = [[NSOpenGLContext currentContext] retain];
    [NSOpenGLContext clearCurrentContext];
  });
  [context makeCurrentContext];
  [context release];
  if (failure) std::rethrow_exception(failure);
}
#pragma clang diagnostic pop

void WaitServingWindowThread(const std::function<bool()>& done) {
  if (![NSThread isMainThread]) return;  // only the main thread runs that work
  while (!done()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
}

bool OgreWindowFollowsGameWindow() { return true; }

namespace {

// A plain titled window whose content view OGRE's Cocoa GL window draws in.
class CocoaTopLevelWindow : public OgreTopLevelWindow {
 public:
  explicit CocoaTopLevelWindow(NSWindow* window) : window_(window) {}
  ~CocoaTopLevelWindow() override { [window_ close]; }
  NativeWindow native() const override {
    NativeWindow window;
    window.system = NativeWindow::kCocoa;
    window.window = uint64_t(uintptr_t([window_ contentView]));
    return window;
  }

 private:
  NSWindow* window_;  // released when closed (releasedWhenClosed, the default)
};

}  // namespace

std::unique_ptr<OgreTopLevelWindow> OgreTopLevelWindow::Create(const std::string& title,
                                                               uint32_t width, uint32_t height,
                                                               bool visible) {
  if (!HasDisplay()) return nullptr;
  [NSApplication sharedApplication];  // a window needs the application object (no-op under SDL)
  const NSRect frame = NSMakeRect(0, 0, width, height);
  NSWindow* window = [[NSWindow alloc]
      initWithContentRect:frame
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable
                  backing:NSBackingStoreBuffered
                    defer:NO];
  if (!window) return nullptr;
  [window setTitle:[NSString stringWithUTF8String:title.c_str()]];
  if (visible) {
    [window center];
    [window makeKeyAndOrderFront:nil];
  }
  return std::make_unique<CocoaTopLevelWindow>(window);
}

bool HasDirect3D11(const std::string&) { return false; }

// Not probed: GL3+ is the only render system, so there is nothing to fall back to.
bool CanCreateGl33Context() { return true; }

namespace {

// Cocoa: the key presses of OGRE's window, seen by a local event monitor (the application's own
// event loop, which OGRE's message pump runs).
class CocoaKeyReader : public KeyReader {
 public:
  explicit CocoaKeyReader(NSWindow* window) : window_(window) {
    monitor_ = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
                                                     handler:^NSEvent*(NSEvent* event) {
                                                       if ([event window] == window_ &&
                                                           [event keyCode] == kF9KeyCode) {
                                                         f9_ = true;
                                                       }
                                                       return event;
                                                     }];
  }
  ~CocoaKeyReader() override {
    if (monitor_) [NSEvent removeMonitor:monitor_];
  }
  bool TakeF9() override {
    const bool f9 = f9_;
    f9_ = false;
    return f9;
  }

 private:
  static constexpr unsigned short kF9KeyCode = 0x65;  // kVK_F9, HIToolbox/Events.h
  NSWindow* window_;
  id monitor_ = nil;
  bool f9_ = false;
};

}  // namespace

std::unique_ptr<KeyReader> KeyReader::ForOgreWindow(
    const std::function<void(const char* name, void* out)>& custom_attribute) {
  NSWindow* window = nil;
  custom_attribute("WINDOW", &window);
  if (!window) return nullptr;
  return std::make_unique<CocoaKeyReader>(window);
}

}  // namespace torchlight::platform
