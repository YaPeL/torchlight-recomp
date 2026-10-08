// macOS implementation of the platform module: the game window is SDL's (Cocoa); OGRE GL3+ draws
// in its content view. Apple Silicon has one GPU, so there is no GPU choice.

#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/user_folders.h"

#include <climits>
#include <cstdlib>
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

std::string OgrePluginDir() {
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre/plugins/";
}

std::string OgreMediaDir() {
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre/media/";
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
