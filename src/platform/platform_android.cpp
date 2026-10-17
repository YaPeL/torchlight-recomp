// Android implementation of the platform module (arm64-v8a, SDLActivity). Its own file, apart from
// Linux's: Android has no X11 or Wayland, no PCI GPUs to choose and no XDG folders. What it does
// today is only what needs no design: thread names, the parts that are the same on every platform
// without a choice (no GPU choice, no Direct3D 11, OGRE's static plugins). The game window
// (ANativeWindow), the folders, the lifecycle and the input are stage A1 of docs/android-port.md;
// until then each of them says it is not there (empty folders, no game window), never pretends.

#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/user_folders.h"

#include <pthread.h>

#include <cstring>
#include <filesystem>
#include <system_error>

namespace torchlight::platform {

bool NativeGameWindow(SDL_Window*, NativeWindow& window) {
  window = NativeWindow();
  return false;  // stage A1: the ANativeWindow
}

std::string EmbeddingVideoError() {
  return "the backend does not draw in Android windows yet (docs/android-port.md, stage A1)";
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

bool HasDisplay() { return false; }  // stage A1

void SetCurrentThreadName(const char* name) {
  char shortened[16] = {};  // the kernel keeps 15 characters, as on Linux
  std::strncpy(shortened, name, sizeof(shortened) - 1);
  pthread_setname_np(pthread_self(), shortened);
}

std::string ShaderCacheDir() { return UserDir(UserFolderKind::kCache, "ogre"); }

std::string CacheDir(const std::string& name) { return UserDir(UserFolderKind::kCache, name); }

std::string ConfigDir() { return UserDir(UserFolderKind::kConfig); }

std::string DataDir() { return UserDir(UserFolderKind::kData); }

std::string GameDataDir() {
  const std::filesystem::path folder = UserFolder(UserFolderKind::kGameData);
  return folder.empty() ? "" : folder.string() + "/";
}

std::string LogDir() { return UserDir(UserFolderKind::kState, "logs"); }

// The app is a library inside the APK, not a file the game's data sits next to (stage A1: the
// APK's assets and the app's storage).
std::string ExecutableDir() { return ""; }

std::string ResourceDir() { return ""; }

// OGRE is static on Android: its render system and codec are linked in (backend/ogre_plugins.h).
std::string OgrePluginDir() { return ""; }

std::string OgreMediaDir() { return ""; }  // stage A1: from the APK's assets

void BringToFront() {}

void BlackGameWindowBackground() {}

std::vector<std::pair<std::string, std::string>> OgreWindowParams(const NativeWindow&) {
  return {};  // stage A1: OGRE's Android EGL window on the ANativeWindow
}

// OGRE's Android window draws only in a window the app gives it.
std::unique_ptr<OgreTopLevelWindow> OgreTopLevelWindow::Create(const std::string&, uint32_t,
                                                               uint32_t, bool) {
  return nullptr;
}

// Stage A1 decides where the surface's work runs (Android's UI thread hands it over); until then
// the work runs where it is asked, as on Linux.
void RunOnWindowThread(const std::function<void()>& work) { work(); }

void WaitServingWindowThread(const std::function<bool()>&) {}

bool OgreWindowFollowsGameWindow() { return false; }

std::string OgreRenderSystemDir(const std::string& plugin_dir, const NativeWindow*) {
  return plugin_dir;
}

bool HasDirect3D11(const std::string&) { return false; }

// Stage A1 (the auto fps cap reads it): unknown, so false.
bool OnBattery() { return false; }

bool IsSteamDeck() { return false; }

// GLES, OGRE's only render system here: nothing to probe or fall back to.
bool CanCreateGl33Context() { return true; }

// One GPU, the system's: nothing to choose.
std::vector<Gpu> Gpus() { return {}; }

std::vector<Gpu> AllGpus() { return {}; }

std::vector<Gpu> SelectableGpus(const std::vector<Gpu>&) { return {}; }

std::vector<std::pair<std::string, std::string>> GpuEnvironment(const std::vector<Gpu>&,
                                                                const std::string&) {
  return {};
}

bool SelectGpu(const std::string& id, std::string& error) {
  if (id.empty()) return true;
  error = "Android has no GPU choice";
  return false;
}

bool GpuChoiceNeedsDirect3D11() { return false; }

std::string RenderingDeviceForGpu(const std::string&, const std::vector<std::string>&) {
  return "";
}

// No backend top-level windows on Android (OgreTopLevelWindow), so no keys to read from one.
std::unique_ptr<KeyReader> KeyReader::ForOgreWindow(
    const std::function<void(const char* name, void* out)>&) {
  return nullptr;
}

}  // namespace torchlight::platform
