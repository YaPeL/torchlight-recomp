// Platform module: everything that depends on the host's window system or video layer lives here,
// behind this interface (one implementation file per platform, chosen by CMake; no platform
// #ifdefs elsewhere). Today: Linux (SDL on X11 or Wayland, whichever SDL chose; OGRE GL on the
// same window system) and Windows (SDL's Win32 windows; OGRE GL drawing on the game window).
//
// Two sides use it: the app (the game window SDL owns, see live/install.cpp) and the render
// backend (the window OGRE creates, see backend/backend.cpp).

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace torchlight::platform {

// A native window, as the backend's C API carries it (backend_api.h, tl_native_window; same
// fields and values). X11: `window` is the window id (OGRE opens its own display connection).
// Wayland: `window` is the wl_surface and `display` the wl_display, both SDL's. Win32: `window`
// is the HWND.
struct NativeWindow {
  enum System : uint32_t { kNone = 0, kX11 = 1, kWayland = 2, kWin32 = 3 };
  uint32_t system = kNone;
  uint64_t window = 0;
  uint64_t display = 0;
};
// "x11", "wayland" or "win32", for the log.
const char* SystemName(const NativeWindow& window);

// ---- the game window (SDL) -------------------------------------------------------------------

// In the app creator, before the game window exists (SDL video is already up): whether the backend
// can render into the windows of SDL's video driver. Empty when it can, else the reason.
std::string EmbeddingVideoError();
// The SDL video driver in use.
std::string VideoDriver();
// In the app creator, before the game window exists: the game window shows black (not undefined
// content) until the backend draws in it. X11: SDL creates its windows without a background.
void BlackGameWindowBackground();
// The game window (the app's only SDL window): its native handle and size in pixels.
bool FindGameWindow(NativeWindow& window, uint32_t& width, uint32_t& height, std::string& error);
// The display the game window is on (UI thread, SDL video up): its desktop size in pixels, its
// refresh rate (0 if unknown) and the fullscreen modes it reports, in pixels. Zero sizes when there
// is no game window.
struct DisplayInfo {
  uint32_t width = 0, height = 0;
  uint32_t refresh_hz = 0;
  std::vector<std::pair<uint32_t, uint32_t>> modes;
};
DisplayInfo GameDisplay();
// Gamepad input keeps reaching the game while another window (the backend's) has the focus.
void AllowBackgroundGamepad();

// The first connected gamepad, read directly (UI thread, where SDL pumps its events): for the
// host's own UI while the guest's input is blocked (the runtime's input drivers stop updating
// then).
struct GamepadState {
  bool connected = false;
  bool a = false, b = false, x = false, y = false, start = false, back = false;
  bool up = false, down = false, left = false, right = false;
  float left_x = 0, left_y = 0;  // -1 to 1, y positive down
};
GamepadState ReadGamepad();

// Whether there is a display to open windows on (tests that need a GL context skip without one).
// Linux: $DISPLAY or $WAYLAND_DISPLAY set. Windows: always.
bool HasDisplay();

// ---- first start ------------------------------------------------------------------------------
// The game's files are installed before the runtime's window exists (game_setup/first_run.h): plain
// SDL3 message boxes, the system's file and folder pickers and a small progress window. Main
// thread.

// A message box with `buttons`; the index of the one chosen, -1 when it was closed.
int AskChoice(const std::string& title, const std::string& message,
              const std::vector<std::string>& buttons);
void ShowError(const std::string& title, const std::string& message);
enum class PickResult { kChosen, kCancelled, kFailed };
// The system's picker for one file or one folder (blocks until it closes); kFailed with the
// reason when no picker could be shown (e.g. no desktop portal in a gamescope session).
PickResult PickFile(const std::string& title, std::string& path, std::string& error);
PickResult PickFolder(const std::string& title, std::string& path, std::string& error);
// The system's preferred languages, most preferred first, as lower-case codes ("de", "pt-br").
std::vector<std::string> PreferredLanguages();
// A small window with a progress bar and a line of text (drawn with SDL's ASCII debug font: letters
// with accents are shown without them). Software rendering: no graphics library is loaded, which
// would keep the GPU from being chosen afterwards.
class ProgressWindow {
 public:
  static std::unique_ptr<ProgressWindow> Open(const std::string& title);
  virtual ~ProgressWindow() = default;
  // Draws `fraction` (0-1) and `text`; false once the user has closed the window.
  virtual bool Show(double fraction, const std::string& text) = 0;
};

// ---- files -----------------------------------------------------------------------------------

// The user's folders are named TorchlightRecomp (user_folders.h, which also moves the ones named
// torchlight before). Every directory below is created if missing and ends in a separator; empty
// when there is none.

// The backend's generated shaders (OGRE RTSS). Linux: $XDG_CACHE_HOME/TorchlightRecomp/ogre/, else
// ~/.cache/TorchlightRecomp/ogre/. Windows: %LOCALAPPDATA%\TorchlightRecomp\ogre\.
std::string ShaderCacheDir();
// Other files the host generates for this machine (`name` below the cache folder).
std::string CacheDir(const std::string& name);
// Directory of the running executable, ending in a separator (the host's data is installed next to
// it); empty when unknown.
std::string ExecutableDir();
// The host's own settings (settings/host_settings.h) and the runtime's config. Linux:
// $XDG_CONFIG_HOME/TorchlightRecomp/, else ~/.config/TorchlightRecomp/. Windows:
// %APPDATA%\TorchlightRecomp\.
std::string ConfigDir();
// The runtime's user data (its default user_data_root: saves, save-backups, profiles, its cache).
// Linux: $XDG_DATA_HOME/TorchlightRecomp/, else ~/.local/share/TorchlightRecomp/. Windows:
// %USERPROFILE%\Saved Games\TorchlightRecomp\ (the Saved Games known folder, not Documents,
// which OneDrive often syncs).
std::string DataDir();
// Where the game's files are installed (user_folders.h kGameData), not created (the first start
// installs it whole): Linux $XDG_DATA_HOME/TorchlightRecomp/game/, else
// ~/.local/share/TorchlightRecomp/game/; Windows %LOCALAPPDATA%\TorchlightRecomp\game\. Empty
// when there is none.
std::string GameDataDir();
// The logs (the runtime's and OGRE's). Linux: $XDG_STATE_HOME/TorchlightRecomp/logs/, else
// ~/.local/state/TorchlightRecomp/logs/. Windows: %LOCALAPPDATA%\TorchlightRecomp\logs\.
std::string LogDir();
// OGRE's plugins (RenderSystem_GL3Plus, Codec_STBI, the Wayland GL build in wayland/) and the
// media the backend uses (Main, RTShaderLib): installed next to the executable, in ogre/plugins/
// and ogre/media/ (the build stages them there too), so the files can live anywhere. Ending in a
// separator; empty when the executable's directory is unknown.
std::string OgrePluginDir();
std::string OgreMediaDir();

// ---- GPU choice ------------------------------------------------------------------------------
// Linux: the GPU the backend renders with is picked by the graphics libraries when they load (the
// EGL/GL vendor libraries read the driver's variables), so it is set before anything loads them.
// Windows: DXGI's adapters; Direct3D 11 renders on the one chosen (the backend passes it to OGRE,
// RenderingDeviceForGpu), OpenGL on the one Windows gives the process; with no choice, Windows
// decides (automatic).

struct Gpu {
  std::string id;      // stable id (Linux: the PCI slot, "0000:01:00.0"; Windows: the PCI ids and
                       // an ordinal, gpu_adapters.h)
  std::string name;    // "NVIDIA GeForce GTX 1050 Ti Mobile"
  std::string driver;  // Linux: kernel driver ("i915", "nvidia", "amdgpu"...); Windows: vendor
  bool boot = false;   // the GPU the system uses by default (Windows: none, automatic)
};
// Every GPU of the machine (Windows: hardware adapters, the high performance one first).
std::vector<Gpu> Gpus();
// Also the software ones (Windows: WARP), for testing only (replay --list_gpus, --gpu).
std::vector<Gpu> AllGpus();
// The ones the backend can be pointed at: the boot GPU, plus PRIME offload targets (NVIDIA's
// proprietary driver; a Mesa driver when the boot GPU's is Mesa too).
std::vector<Gpu> SelectableGpus(const std::vector<Gpu>& all);
// What the graphics libraries need to use `id` (none for the boot GPU or an unknown id). Linux,
// verified through OGRE GL3+ on EGL with an Intel boot GPU and an NVIDIA one:
// __NV_PRIME_RENDER_OFFLOAD=1 selects NVIDIA's driver; DRI_PRIME=pci-... selects a Mesa GPU, and
// pointed at an NVIDIA proprietary GPU falls back to software (llvmpipe), so it is never used for one.
std::vector<std::pair<std::string, std::string>> GpuEnvironment(const std::vector<Gpu>& all,
                                                                const std::string& id);
// Applies GpuEnvironment for `id` (empty: the default GPU). False with the reason when the choice
// cannot stick: graphics libraries already loaded in the process, or an unknown or unselectable id.
bool SelectGpu(const std::string& id, std::string& error);
// Whether the GPU choice takes effect only with the Direct3D 11 render system (Windows).
bool GpuChoiceNeedsDirect3D11();
// The value of OGRE's Direct3D 11 "Rendering Device" option for the GPU `id`, among the values the
// render system offers (`ogre_devices`); empty when none is that GPU (the automatic choice stays).
std::string RenderingDeviceForGpu(const std::string& id,
                                  const std::vector<std::string>& ogre_devices);

// ---- the backend's window (OGRE) -------------------------------------------------------------

// OGRE's window creation parameters for drawing inside a native window: X11, a child window
// ("parentWindowHandle"); Wayland, directly on the window's surface ("externalWlDisplay",
// "externalWlSurface"; Wayland has no child windows); Win32, directly on the window too
// ("parentWindowHandle", which OGRE's Win32 GL window takes as its own, creating no child).
std::vector<std::pair<std::string, std::string>> OgreWindowParams(const NativeWindow& window);
// The directory of the OGRE GL render system that renders into that kind of window, under the
// install's plugin directory: OGRE builds its GL window support for X11 or for Wayland
// (OGRE_USE_WAYLAND), so the Wayland build lives in a subdirectory. A null window (an OGRE
// top-level window) uses the default build.
std::string OgreRenderSystemDir(const std::string& plugin_dir, const NativeWindow* window);

// Whether OGRE's Direct3D 11 render system is installed under the plugin directory (Windows only),
// offered in the video menu next to GL3+.
bool HasDirect3D11(const std::string& plugin_dir);
// Whether OGRE's GL3+ render system can run here: an OpenGL 3.3 core context can be created (Windows:
// a probe with a hidden window, released before returning; without a GPU driver Windows has only
// OpenGL 1.1, on which OGRE crashes). Linux and macOS: true without probing, GL3+ being the only
// render system there.
bool CanCreateGl33Context();

// Keys typed in a top-level window the backend created: OGRE does not read the keyboard.
class KeyReader {
 public:
  // `custom_attribute` is the window's RenderWindow::getCustomAttribute. Null when the platform
  // cannot read keys from that window.
  static std::unique_ptr<KeyReader> ForOgreWindow(
      const std::function<void(const char* name, void* out)>& custom_attribute);
  virtual ~KeyReader() = default;
  // Whether F9 was pressed since the last call (events are consumed).
  virtual bool TakeF9() = 0;
};

}  // namespace torchlight::platform
