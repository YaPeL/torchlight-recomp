// Linux implementation of the platform module: the game window is SDL's, on X11 or Wayland
// (whichever driver SDL chose); OGRE GL (EGL) draws in it with the build for that window system.
// Top-level OGRE windows (the replay's) are X11 ones.

#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/user_folders.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <tuple>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_video.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

namespace torchlight::platform {

namespace {
// The OGRE GL build for Wayland windows, under the plugin directory.
constexpr const char* kWaylandRenderSystemSubdir = "/wayland";
}  // namespace

bool NativeGameWindow(SDL_Window* game, NativeWindow& window) {
  SDL_PropertiesID props = SDL_GetWindowProperties(game);
  window = NativeWindow();
  if (VideoDriver() == "wayland") {
    window.system = NativeWindow::kWayland;
    window.window = uint64_t(uintptr_t(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr)));
    window.display = uint64_t(uintptr_t(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr)));
  } else {
    window.system = NativeWindow::kX11;
    window.window = uint64_t(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
  }
  return window.window && (window.system != NativeWindow::kWayland || window.display);
}

std::string EmbeddingVideoError() {
  const char* current = SDL_GetCurrentVideoDriver();
  if (current && (std::strcmp(current, "x11") == 0 || std::strcmp(current, "wayland") == 0)) {
    return "";
  }
  return std::string("SDL video driver ") + (current ? current : "none") +
         ": the backend draws only in x11 or wayland windows";
}

namespace {
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
}  // namespace

bool HasDisplay() { return std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY"); }

std::string ShaderCacheDir() {
  return UserDir(UserFolderKind::kCache, "ogre");
}

std::string CacheDir(const std::string& name) {
  return UserDir(UserFolderKind::kCache, name);
}

namespace {

std::string ReadLine(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::string line;
  std::getline(in, line);
  return line;
}

// "Vendor [Device]" from the PCI ids database: the bracketed marketing name when there is one.
std::string PciName(uint32_t vendor, uint32_t device) {
  const char* vendor_name = vendor == 0x8086   ? "Intel"
                            : vendor == 0x10DE ? "NVIDIA"
                            : vendor == 0x1002 ? "AMD"
                                               : nullptr;
  std::string device_name;
  for (const char* db : {"/usr/share/hwdata/pci.ids", "/usr/share/misc/pci.ids"}) {
    std::ifstream in(db);
    if (!in) continue;
    std::string line;
    bool in_vendor = false;
    char vendor_hex[8], device_hex[8];
    std::snprintf(vendor_hex, sizeof(vendor_hex), "%04x", vendor);
    std::snprintf(device_hex, sizeof(device_hex), "%04x", device);
    while (std::getline(in, line)) {
      if (line.empty() || line[0] == '#') continue;
      if (line[0] != '\t') {
        if (in_vendor) break;
        in_vendor = line.rfind(vendor_hex, 0) == 0;
      } else if (in_vendor && line.size() > 1 && line[1] != '\t' &&
                 line.compare(1, 4, device_hex) == 0) {
        device_name = line.substr(7);
        break;
      }
    }
    if (!device_name.empty()) break;
  }
  if (auto open = device_name.find('['), close = device_name.rfind(']');
      open != std::string::npos && close != std::string::npos && close > open) {
    device_name = device_name.substr(open + 1, close - open - 1);
  }
  if (device_name.empty()) {
    char hex[16];
    std::snprintf(hex, sizeof(hex), "%04x:%04x", vendor, device);
    device_name = hex;
  }
  return vendor_name ? std::string(vendor_name) + " " + device_name : device_name;
}

bool IsMesaDriver(const std::string& driver) {
  return driver == "i915" || driver == "xe" || driver == "amdgpu" || driver == "radeon" ||
         driver == "nouveau";
}

// Whether the process already mapped a graphics library (then the choice would not stick).
bool GraphicsLibrariesLoaded(std::string& which) {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    for (const char* lib : {"libEGL", "libGLX", "libGL.so", "libOpenGL", "libvulkan"}) {
      if (line.find(lib) != std::string::npos) {
        which = line.substr(line.rfind(' ') + 1);
        return true;
      }
    }
  }
  return false;
}

}  // namespace

std::vector<Gpu> Gpus() {
  std::vector<Gpu> gpus;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos) continue;
    const std::filesystem::path device = entry.path() / "device";
    Gpu gpu;
    const std::string uevent_slot = [&] {
      std::ifstream in(device / "uevent");
      std::string line;
      while (std::getline(in, line)) {
        if (line.rfind("PCI_SLOT_NAME=", 0) == 0) return line.substr(14);
      }
      return std::string();
    }();
    if (uevent_slot.empty()) continue;
    gpu.id = uevent_slot;
    gpu.driver = std::filesystem::read_symlink(device / "driver", ec).filename().string();
    gpu.boot = ReadLine(device / "boot_vga") == "1";
    const uint32_t vendor = uint32_t(std::strtoul(ReadLine(device / "vendor").c_str(), nullptr, 16));
    const uint32_t product = uint32_t(std::strtoul(ReadLine(device / "device").c_str(), nullptr, 16));
    gpu.name = PciName(vendor, product);
    gpus.push_back(gpu);
  }
  std::sort(gpus.begin(), gpus.end(), [](const Gpu& a, const Gpu& b) {
    return a.boot != b.boot ? a.boot : a.id < b.id;
  });
  return gpus;
}

std::vector<Gpu> SelectableGpus(const std::vector<Gpu>& all) {
  const Gpu* boot = nullptr;
  for (const Gpu& gpu : all) {
    if (gpu.boot) boot = &gpu;
  }
  std::vector<Gpu> selectable;
  for (const Gpu& gpu : all) {
    if (gpu.boot || gpu.driver == "nvidia" ||
        (IsMesaDriver(gpu.driver) && boot && IsMesaDriver(boot->driver))) {
      selectable.push_back(gpu);
    }
  }
  return selectable;
}

std::vector<std::pair<std::string, std::string>> GpuEnvironment(const std::vector<Gpu>& all,
                                                                const std::string& id) {
  for (const Gpu& gpu : SelectableGpus(all)) {
    if (gpu.id != id || gpu.boot) continue;
    if (gpu.driver == "nvidia") return {{"__NV_PRIME_RENDER_OFFLOAD", "1"}};
    // Mesa's form of a PCI slot: "pci-0000_01_00_0".
    std::string slot = "pci-" + gpu.id;
    for (char& c : slot) {
      if (c == ':' || c == '.') c = '_';
    }
    return {{"DRI_PRIME", slot}};
  }
  return {};
}

bool SelectGpu(const std::string& id, std::string& error) {
  std::string loaded;
  if (GraphicsLibrariesLoaded(loaded)) {
    error = "graphics library already loaded (" + loaded + "); the GPU choice would not stick";
    return false;
  }
  if (id.empty()) return true;
  const std::vector<Gpu> all = Gpus();
  bool known = false;
  for (const Gpu& gpu : SelectableGpus(all)) known = known || gpu.id == id;
  if (!known) {
    error = "GPU " + id + " is not one the backend can be pointed at";
    return false;
  }
  for (const auto& [name, value] : GpuEnvironment(all, id)) setenv(name.c_str(), value.c_str(), 1);
  return true;
}

std::string ExecutableDir() {
  std::error_code ec;
  const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
  if (ec) return "";
  return exe.parent_path().string() + "/";
}

std::string ConfigDir() { return UserDir(UserFolderKind::kConfig); }

std::string DataDir() { return UserDir(UserFolderKind::kData); }

std::string GameDataDir() {
  const std::filesystem::path folder = UserFolder(UserFolderKind::kGameData);
  return folder.empty() ? "" : folder.string() + "/";
}

std::string LogDir() {
  return UserDir(UserFolderKind::kState, "logs");
}

std::string OgrePluginDir() {
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre/plugins/";
}

std::string OgreMediaDir() {
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre/media/";
}

void BlackGameWindowBackground() {
  if (VideoDriver() != "x11") return;  // Wayland: nothing shows before the first buffer
  // SDL creates X11 windows with background None (SDL_x11window.c), so until something draws the
  // server shows whatever is there. The first event of a window gives it a black background.
  SDL_AddEventWatch(
      [](void*, SDL_Event* event) {
        static bool done = false;
        if (done || event->type < SDL_EVENT_WINDOW_FIRST || event->type > SDL_EVENT_WINDOW_LAST) {
          return true;
        }
        SDL_Window* window = SDL_GetWindowFromID(event->window.windowID);
        if (!window) return true;
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        auto* display = static_cast<Display*>(
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
        auto id = Window(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        if (display && id) {
          XSetWindowBackground(display, id, BlackPixel(display, DefaultScreen(display)));
          XClearWindow(display, id);
          XFlush(display);
          done = true;
        }
        return true;
      },
      nullptr);
}

std::vector<std::pair<std::string, std::string>> OgreWindowParams(const NativeWindow& window) {
  if (window.system == NativeWindow::kWayland) {
    // OGRE draws on SDL's own surface: in only mode nothing else draws there (no presenter), as
    // with SDL's own GL windows. SDL keeps the surface's input and window management.
    return {{"externalWlDisplay", std::to_string(window.display)},
            {"externalWlSurface", std::to_string(window.window)}};
  }
  return {{"parentWindowHandle", std::to_string(window.window)}};
}

std::string OgreRenderSystemDir(const std::string& plugin_dir, const NativeWindow* window) {
  if (window && window->system == NativeWindow::kWayland) {
    return plugin_dir + kWaylandRenderSystemSubdir;
  }
  return plugin_dir;
}

bool HasDirect3D11(const std::string&) { return false; }

// Not probed: GL3+ is the only render system, so there is nothing to fall back to.
bool CanCreateGl33Context() { return true; }

std::vector<Gpu> AllGpus() { return Gpus(); }

bool GpuChoiceNeedsDirect3D11() { return false; }

std::string RenderingDeviceForGpu(const std::string&, const std::vector<std::string>&) {
  return "";
}

namespace {

// X11: OGRE's message handling selects window events only; key presses are added to the window's
// event mask and read here.
class X11KeyReader : public KeyReader {
 public:
  X11KeyReader(Display* display, Window window) : display_(display), window_(window) {
    XWindowAttributes attributes;
    XGetWindowAttributes(display_, window_, &attributes);
    XSelectInput(display_, window_, attributes.your_event_mask | KeyPressMask);
  }
  bool TakeF9() override {
    bool f9 = false;
    XEvent event;
    while (XCheckWindowEvent(display_, window_, KeyPressMask, &event)) {
      if (XLookupKeysym(&event.xkey, 0) == XK_F9) f9 = true;
    }
    return f9;
  }

 private:
  Display* display_;
  Window window_;
};

}  // namespace

std::unique_ptr<KeyReader> KeyReader::ForOgreWindow(
    const std::function<void(const char* name, void* out)>& custom_attribute) {
  Display* display = nullptr;
  Window window = 0;
  custom_attribute("XDISPLAY", &display);
  custom_attribute("WINDOW", &window);
  if (!display || !window) return nullptr;
  return std::make_unique<X11KeyReader>(display, window);
}

}  // namespace torchlight::platform
