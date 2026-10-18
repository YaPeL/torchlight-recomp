// Android implementation of the platform module (arm64-v8a, SDLActivity). Its own file, apart from
// Linux's: Android has no X11 or Wayland, no PCI GPUs to choose and no XDG folders. What it does
// today is only what needs no design: thread names, the parts that are the same on every platform
// without a choice (no GPU choice, no Direct3D 11, OGRE's static plugins), the folders, which are
// the app's storage (user_folders_android.cpp), and the window: the activity's one surface, an
// ANativeWindow that SDL holds, which OGRE's Android EGL window draws in, and the files the APK
// carries (OGRE's media, data/ui), copied to internal storage, and the surface's lifecycle: the
// activity's surface goes away in the background and the backend lets go of it first
// (ServeSurfaceChanges, with TorchlightSurface.java). The input is the rest of stage A1 of
// docs/android-port.md.

#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/user_folders.h"

#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_system.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>
#include <vector>

namespace torchlight::platform {

namespace {
// The activity's ANativeWindow, which SDL keeps in the window's properties (null while the app has
// no surface, in the background).
uint64_t AndroidWindowOf(SDL_Window* window) {
  return uint64_t(uintptr_t(SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                   SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr)));
}
}  // namespace

bool NativeGameWindow(SDL_Window* game, NativeWindow& window) {
  window = NativeWindow();
  window.system = NativeWindow::kAndroid;
  window.window = AndroidWindowOf(game);
  return window.window != 0;
}

std::string EmbeddingVideoError() {
  const char* current = SDL_GetCurrentVideoDriver();
  if (current && std::strcmp(current, "android") == 0) return "";
  return std::string("SDL video driver ") + (current ? current : "none") +
         ": the backend draws only in the android one here";
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

bool HasDisplay() { return true; }  // the activity's surface

void SetCurrentThreadName(const char* name) {
  char shortened[16] = {};  // the kernel keeps 15 characters, as on Linux
  std::strncpy(shortened, name, sizeof(shortened) - 1);
  pthread_setname_np(pthread_self(), shortened);
}

std::string ShaderCacheDir() { return UserDir(UserFolderKind::kCache, "ogre"); }

std::string ShaderCompilerVersion() { return {}; }

std::string CacheDir(const std::string& name) { return UserDir(UserFolderKind::kCache, name); }

std::string ConfigDir() { return UserDir(UserFolderKind::kConfig); }

std::string DataDir() { return UserDir(UserFolderKind::kData); }

std::string GameDataDir() {
  const std::filesystem::path folder = UserFolder(UserFolderKind::kGameData);
  return folder.empty() ? "" : folder.string() + "/";
}

std::string LogDir() { return UserDir(UserFolderKind::kState, "logs"); }

// The app is a library inside the APK, not a file the game's data sits next to.
std::string ExecutableDir() { return ""; }

namespace {

// An asset of the APK, whole (SDL reads the APK's assets by their relative path, after looking in
// internal storage: nothing of the same path may be there, CopyResources).
bool ReadAsset(const std::string& name, std::string& data) {
  size_t size = 0;
  void* bytes = SDL_LoadFile(name.c_str(), &size);
  if (!bytes) return false;
  data.assign(static_cast<const char*>(bytes), size);
  SDL_free(bytes);
  return true;
}

// The files the APK carries for the host (OGRE's media under ogre/media/, data/ui), which the
// backend and the menus read as plain files: tools/android/build_apk.sh puts them in the assets
// under resources/, listed in resources.txt after a stamp of their content. They are copied to
// <internal storage>/resources/ on the first start of each version (the owner's decision,
// 2026-10-17): the stamp is written last, so a copy cut short is made again. Empty when there is
// none (an APK without them, or a copy that failed).
std::string CopyResources() {
  const char* internal = SDL_GetAndroidInternalStoragePath();
  std::string list;
  if (!internal || !ReadAsset("resources.txt", list)) return "";
  std::istringstream lines(list);
  std::string stamp;
  std::getline(lines, stamp);
  const std::filesystem::path root = std::filesystem::path(internal) / "resources";
  const std::filesystem::path stamp_file = root / ".stamp";
  {
    std::ifstream in(stamp_file);
    std::string copied;
    if (std::getline(in, copied) && copied == stamp && !stamp.empty()) return root.string() + "/";
  }
  // The old copy goes first: SDL looks for a relative path in internal storage before the APK's
  // assets, so a file left behind would be read back as the new version.
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  if (ec) return "";
  for (std::string path; std::getline(lines, path);) {
    if (path.empty()) continue;
    std::string data;
    if (!ReadAsset("resources/" + path, data)) return "";
    const std::filesystem::path file = root / path;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(data.data(), std::streamsize(data.size()));
    if (!out) return "";
  }
  std::ofstream(stamp_file, std::ios::trunc) << stamp << "\n";
  return root.string() + "/";
}

std::string ResourcesRoot() {
  static std::once_flag once;
  static std::string root;
  std::call_once(once, [] { root = CopyResources(); });
  return root;
}

}  // namespace

std::string ResourceDir() { return ResourcesRoot(); }

// OGRE is static on Android: its render system and codec are linked in (backend/ogre_plugins.h).
std::string OgrePluginDir() { return ""; }

std::string OgreMediaDir() {
  const std::string root = ResourcesRoot();
  return root.empty() ? "" : root + "ogre/media/";
}

void BringToFront() {}

void BlackGameWindowBackground() {}

std::vector<std::pair<std::string, std::string>> OgreWindowParams(const NativeWindow& window) {
  // OGRE's Android EGL window on the activity's surface. preserveContext: when the surface goes
  // (the app in the background) OGRE keeps its EGL context, with the textures, buffers and programs
  // the backend mirrors from the guest, and only makes the surface again (docs/android-port.md,
  // section 4).
  return {{"externalWindowHandle", std::to_string(window.window)}, {"preserveContext", "true"}};
}

namespace {

// OGRE's Android window draws only in a window the app gives it, and an Android app has one: the
// activity's surface, which SDL makes its window (full screen whatever the size asked). A backend
// with its own window (the replay) draws there, hidden or not.
class SdlTopLevelWindow final : public OgreTopLevelWindow {
 public:
  explicit SdlTopLevelWindow(SDL_Window* window) : window_(window) {}
  ~SdlTopLevelWindow() override { SDL_DestroyWindow(window_); }
  NativeWindow native() const override {
    NativeWindow native;
    native.system = NativeWindow::kAndroid;
    native.window = AndroidWindowOf(window_);
    return native;
  }

 private:
  SDL_Window* window_;
};

}  // namespace

std::unique_ptr<OgreTopLevelWindow> OgreTopLevelWindow::Create(const std::string& title,
                                                               uint32_t width, uint32_t height,
                                                               bool) {
  if (!SDL_WasInit(SDL_INIT_VIDEO) && !SDL_InitSubSystem(SDL_INIT_VIDEO)) return nullptr;
  SDL_Window* window =
      SDL_CreateWindow(title.c_str(), int(width), int(height), SDL_WINDOW_FULLSCREEN);
  if (!window) return nullptr;
  if (!AndroidWindowOf(window)) {  // no surface yet (the app not in the foreground)
    SDL_DestroyWindow(window);
    return nullptr;
  }
  return std::make_unique<SdlTopLevelWindow>(window);
}

// Stage A1 decides where the surface's work runs (Android's UI thread hands it over); until then
// the work runs where it is asked, as on Linux.
void RunOnWindowThread(const std::function<void()>& work) { work(); }

void WaitServingWindowThread(const std::function<bool()>&) {}

bool OgreWindowFollowsGameWindow() { return false; }

namespace {

// The activity's surface, as the Java side reports it (TorchlightSurface.java) and the backend
// serves it (ServeSurfaceChanges). Present: the backend draws in it. Going: Android is destroying
// it and its UI thread waits until the backend let go (released). Gone: none; a new one arrives as
// new_window (an ANativeWindow reference of ours). Ending: the activity is finishing.
enum class SurfaceState { kPresent, kGoing, kGone, kEnding };
struct Surface {
  std::mutex mutex;
  std::condition_variable changed;
  SurfaceState state = SurfaceState::kPresent;
  bool released = false;
  ANativeWindow* new_window = nullptr;
};
Surface& TheSurface() {
  static Surface surface;
  return surface;
}

// How long Android's UI thread waits for the backend to let go before it goes on (a backend that
// is not presenting, a guest loading). Past it the backend lets go at its next present, of a
// surface Android has already taken back: EGL keeps its own reference to the window, so that
// present fails instead of crashing. Android itself reports an unresponsive app after 5 s.
constexpr auto kSurfaceReleaseWait = std::chrono::seconds(2);

}  // namespace

bool ServeSurfaceChanges(const std::function<void()>& lost,
                         const std::function<bool(const NativeWindow&)>& restored) {
  Surface& s = TheSurface();
  std::unique_lock lock(s.mutex);
  if (s.state == SurfaceState::kPresent) return true;
  if (s.state == SurfaceState::kEnding) return false;
  if (!s.released) {
    lock.unlock();
    SDL_Log("surface: going; the backend lets go of it");
    lost();
    lock.lock();
    s.released = true;
    s.changed.notify_all();
  }
  while (true) {
    s.changed.wait(lock, [&] { return s.new_window || s.state == SurfaceState::kEnding; });
    if (s.state == SurfaceState::kEnding) return false;
    ANativeWindow* window = s.new_window;
    s.new_window = nullptr;
    s.state = SurfaceState::kPresent;
    lock.unlock();
    NativeWindow native;
    native.system = NativeWindow::kAndroid;
    native.window = uint64_t(uintptr_t(window));
    SDL_Log("surface: back (%dx%d); the backend draws in it again", ANativeWindow_getWidth(window),
            ANativeWindow_getHeight(window));
    const bool made = restored(native);
    ANativeWindow_release(window);  // EGL holds its own reference to the window it draws in
    if (made) return true;
    SDL_Log("surface: nothing could be made on it; waiting for the next one");
    lock.lock();
    // Back to no surface, unless Android has already taken this one and given another.
    if (s.state == SurfaceState::kPresent) s.state = SurfaceState::kGone;
    s.released = true;
    s.changed.notify_all();  // a UI thread waiting in nativeSurfaceDestroying goes on
  }
}

std::string OgreRenderSystemDir(const std::string& plugin_dir, const NativeWindow*) {
  return plugin_dir;
}

std::pair<uint32_t, uint32_t> PreferredRenderResolution() { return {1280, 800}; }

bool HasSystemBackButton() { return true; }

// Android's picker (SDL's, through the Storage Access Framework) gives a document URI
// (content://...), which SDL opens but plain file I/O does not: the document is copied to the
// app's cache first, with a progress window (docs/android-port.md, A1.2: the player's game).
PickResult OpenablePickedFile(const std::string& title, std::string& path, std::string& error) {
  if (path.rfind("content://", 0) != 0) return PickResult::kChosen;
  const std::string dir = CacheDir("picked");
  if (dir.empty()) {
    error = "no cache folder for the chosen file";
    return PickResult::kUncopied;
  }
  SDL_IOStream* in = SDL_IOFromFile(path.c_str(), "rb");
  if (!in) {
    error = std::string("cannot open the chosen file: ") + SDL_GetError();
    return PickResult::kUncopied;
  }
  const Sint64 size = SDL_GetIOSize(in);
  const std::string copy = dir + "picked_file";
  std::ofstream out(copy, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    SDL_CloseIO(in);
    error = "cannot create " + copy;
    return PickResult::kUncopied;
  }
  auto progress = ProgressWindow::Open(title);
  std::vector<char> buffer(4 << 20);
  Sint64 done = 0;
  PickResult result = PickResult::kChosen;
  while (true) {
    const size_t got = SDL_ReadIO(in, buffer.data(), buffer.size());
    if (got == 0) {
      if (SDL_GetIOStatus(in) != SDL_IO_STATUS_EOF) {
        error = std::string("cannot read the chosen file: ") + SDL_GetError();
        result = PickResult::kUncopied;
      }
      break;
    }
    out.write(buffer.data(), std::streamsize(got));
    if (!out) {
      error = "cannot write a copy of the chosen file (is the device full?)";
      result = PickResult::kUncopied;
      break;
    }
    done += Sint64(got);
    if (progress) {
      const double fraction = size > 0 ? double(done) / double(size) : 0;
      const std::string text = std::to_string(done >> 20) + " / " +
                               (size > 0 ? std::to_string(size >> 20) : std::string("?")) + " MB";
      if (!progress->Show(fraction, text)) {
        result = PickResult::kCancelled;
        break;
      }
    }
  }
  SDL_CloseIO(in);
  out.close();
  if (result != PickResult::kChosen) {
    std::error_code ec;
    std::filesystem::remove(copy, ec);
    return result;
  }
  path = copy;
  return PickResult::kChosen;
}

bool CanPickFolder() { return false; }

void ForgetPickedFiles() {
  const std::filesystem::path folder = UserFolder(UserFolderKind::kCache);
  if (folder.empty()) return;
  std::error_code ec;
  std::filesystem::remove_all(folder / "picked", ec);
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

// No keyboard on the backend's window here: input comes through SDL (stage A1.4).
std::unique_ptr<KeyReader> KeyReader::ForOgreWindow(
    const std::function<void(const char* name, void* out)>&) {
  return nullptr;
}

}  // namespace torchlight::platform

// The Java side of the surface's lifecycle (packaging/android/java/.../TorchlightSurface.java and
// TorchlightActivity.java), on Android's UI thread.
extern "C" {

JNIEXPORT void JNICALL Java_io_github_yapel_tlrecomp_TorchlightSurface_nativeSurfaceDestroying(
    JNIEnv*, jclass) {
  using namespace torchlight::platform;
  Surface& s = TheSurface();
  std::unique_lock lock(s.mutex);
  if (s.state == SurfaceState::kGoing || s.state == SurfaceState::kGone) {
    // A surface that came back and goes again before the backend took it: drop it.
    if (s.new_window) ANativeWindow_release(s.new_window);
    s.new_window = nullptr;
    s.state = SurfaceState::kGone;
    return;
  }
  if (s.state != SurfaceState::kPresent) return;
  s.state = SurfaceState::kGoing;
  s.released = false;
  s.changed.notify_all();
  if (!s.changed.wait_for(lock, kSurfaceReleaseWait, [&] { return s.released; })) {
    SDL_Log("surface: destroyed before the backend let go of it (not presenting)");
  }
  if (s.state == SurfaceState::kGoing) s.state = SurfaceState::kGone;
}

JNIEXPORT void JNICALL Java_io_github_yapel_tlrecomp_TorchlightSurface_nativeSurfaceReady(
    JNIEnv* env, jclass, jobject java_surface) {
  using namespace torchlight::platform;
  Surface& s = TheSurface();
  std::lock_guard lock(s.mutex);
  // The first surface, and a surface that only changed size, reach the backend through SDL's
  // window as before; only one that replaces a surface gone is handed over here.
  if (s.state != SurfaceState::kGoing && s.state != SurfaceState::kGone) return;
  ANativeWindow* window = ANativeWindow_fromSurface(env, java_surface);
  if (!window) return;
  if (s.new_window) ANativeWindow_release(s.new_window);
  s.new_window = window;
  s.changed.notify_all();
}

JNIEXPORT void JNICALL Java_io_github_yapel_tlrecomp_TorchlightActivity_nativeAppEnding(JNIEnv*,
                                                                                        jclass) {
  using namespace torchlight::platform;
  Surface& s = TheSurface();
  std::lock_guard lock(s.mutex);
  s.state = SurfaceState::kEnding;
  s.changed.notify_all();
}

}  // extern "C"
