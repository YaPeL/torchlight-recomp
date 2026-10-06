// Tests for the Windows platform module: the GPU list and choice, the user folders' known-folder
// bases and the directories below them, the executable's directory, and F9 read from a top-level
// window (a plain Win32 one standing in for OGRE's).

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "platform/gl_probe_win.h"
#include "platform/platform.h"
#include "platform/user_folders.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace {

namespace platform = torchlight::platform;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

bool StartsWith(const std::string& s, const std::string& prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string KnownFolder(REFKNOWNFOLDERID folder) {
  PWSTR path = nullptr;
  std::string utf8;
  if (SUCCEEDED(SHGetKnownFolderPath(folder, 0, nullptr, &path))) {
    const int size = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    utf8.resize(size_t(size));
    WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8.data(), size, nullptr, nullptr);
    utf8.resize(utf8.size() - 1);  // the terminator
  }
  CoTaskMemFree(path);
  return utf8;
}

// This machine's DXGI adapters (the rules themselves: gpu_adapters_test).
void TestGpus() {
  const std::vector<platform::Gpu> all = platform::AllGpus();
  const std::vector<platform::Gpu> hardware = platform::Gpus();
  for (const platform::Gpu& gpu : all) {
    std::printf("  %s %s (%s)\n", gpu.id.c_str(), gpu.name.c_str(), gpu.driver.c_str());
    Check(!gpu.boot, "no boot GPU: the default is automatic");
  }
  bool warp = false;
  for (const platform::Gpu& gpu : all) warp = warp || gpu.driver == "software";
  Check(warp, "WARP is listed for testing");
  for (const platform::Gpu& gpu : hardware) Check(gpu.driver != "software", "not to the player");
  Check(platform::SelectableGpus(hardware).size() == hardware.size(), "Direct3D 11 takes any");
  Check(platform::GpuChoiceNeedsDirect3D11(), "the choice is Direct3D 11's");
  std::string error;
  Check(platform::SelectGpu("", error), "automatic");
  for (const platform::Gpu& gpu : all) Check(platform::SelectGpu(gpu.id, error), "a GPU by id");
  Check(platform::SelectGpu("0000:01:00.0", error),
        "an unknown id is left to the backend (automatic there)");
  Check(platform::RenderingDeviceForGpu("0000:01:00.0", {"(default)"}).empty(),
        "an unknown id has no OGRE device");
}

// The platform's own bases (read only: nothing is created there).
void TestUserFolderBases() {
  using platform::UserFolderKind;
  auto base = [](UserFolderKind kind) {
    const std::u8string u8 = platform::PlatformUserFolderBase(kind).u8string();
    return std::string(u8.begin(), u8.end());
  };
  Check(base(UserFolderKind::kConfig) == KnownFolder(FOLDERID_RoamingAppData),
        "config: %APPDATA% (roaming)");
  Check(base(UserFolderKind::kCache) == KnownFolder(FOLDERID_LocalAppData),
        "cache: %LOCALAPPDATA%");
  Check(base(UserFolderKind::kState) == KnownFolder(FOLDERID_LocalAppData),
        "state (logs): %LOCALAPPDATA%");
  Check(base(UserFolderKind::kData) == KnownFolder(FOLDERID_SavedGames),
        "data (saves): Saved Games, not Documents");
  Check(base(UserFolderKind::kGameData) == KnownFolder(FOLDERID_LocalAppData),
        "game data (installed game files): %LOCALAPPDATA%");
}

// Every directory comes from UserFolder(kind) (below the tests' folder under ctest), created and
// ending in a separator.
void TestDirs() {
  using platform::UserFolderKind;
  auto check = [](const std::string& dir, UserFolderKind kind, const char* sub, const char* what) {
    std::filesystem::path expected = platform::UserFolder(kind);
    if (*sub) expected /= sub;
    const std::filesystem::path got(std::u8string(dir.begin(), dir.end()));
    Check(!dir.empty() && dir.back() == '\\' && got == expected / "", what);
    Check(std::filesystem::is_directory(got), what);
    // The code also makes paths from these strings directly (the process code page is UTF-8).
    Check(std::filesystem::path(dir) == got, what);
  };
  check(platform::ConfigDir(), UserFolderKind::kConfig, "", "ConfigDir: the config folder");
  check(platform::ShaderCacheDir(), UserFolderKind::kCache, "ogre", "ShaderCacheDir: cache\\ogre");
  check(platform::CacheDir("layouts"), UserFolderKind::kCache, "layouts", "CacheDir: cache\\name");
  check(platform::LogDir(), UserFolderKind::kState, "logs", "LogDir: state\\logs");
  check(platform::DataDir(), UserFolderKind::kData, "", "DataDir: the data folder");
  // Not created: the first start installs it whole.
  const std::string game = platform::GameDataDir();
  const std::filesystem::path game_path(std::u8string(game.begin(), game.end()));
  Check(!game.empty() && game.back() == '\\' &&
            game_path == platform::UserFolder(UserFolderKind::kGameData) / "" &&
            platform::UserFolder(UserFolderKind::kGameData).filename() == "game",
        "GameDataDir: <game data base>\\TorchlightRecomp\\game\\");

  const std::string exe = platform::ExecutableDir();
  char module[MAX_PATH];
  GetModuleFileNameA(nullptr, module, MAX_PATH);
  Check(!exe.empty() && exe.back() == '\\' && StartsWith(module, exe),
        "executable dir: the test's own directory, ending in a separator");
  Check(platform::OgrePluginDir() == exe + "ogre\\plugins\\", "OGRE plugins next to the exe");
  Check(platform::OgreMediaDir() == exe + "ogre\\media\\", "OGRE media next to the exe");
}

// The same below a folder with accents and ñ (a Windows account such as Martín's).
void TestNonAsciiDirs() {
  const char* name = platform::kTestUserFoldersVariable;
  const char* previous = std::getenv(name);
  const std::string saved = previous ? previous : "";
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() /
      std::filesystem::path(u8"tl_platform_Martín_ñandú");
  std::filesystem::remove_all(root);
  const std::u8string root_u8 = root.u8string();
  _putenv_s(name, std::string(root_u8.begin(), root_u8.end()).c_str());
  Check(platform::UserFolder(platform::UserFolderKind::kConfig) ==
            root / "config" / "TorchlightRecomp",
        "a non-ASCII base resolves");
  TestDirs();
  _putenv_s(name, saved.c_str());
  std::filesystem::remove_all(root);
}

void TestKeyReader() {
  WNDCLASSW window_class = {};
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"torchlight_platform_win_test";
  RegisterClassW(&window_class);
  HWND window = CreateWindowExW(0, window_class.lpszClassName, L"test", WS_OVERLAPPEDWINDOW, 0, 0,
                                64, 64, nullptr, nullptr, window_class.hInstance, nullptr);
  Check(window != nullptr, "test window");
  auto reader = platform::KeyReader::ForOgreWindow([window](const char* name, void* out) {
    if (std::string(name) == "WINDOW") *static_cast<HWND*>(out) = window;
  });
  Check(reader != nullptr, "a key reader for a Win32 window");
  Check(!reader->TakeF9(), "no F9 yet");
  PostMessageW(window, WM_KEYDOWN, VK_F10, 0);
  Check(!reader->TakeF9(), "another key is not F9");
  PostMessageW(window, WM_KEYDOWN, VK_F9, 0);
  Check(reader->TakeF9(), "F9 pressed");
  Check(!reader->TakeF9(), "and consumed");
  MSG message;
  Check(!PeekMessageW(&message, window, 0, 0, PM_NOREMOVE), "the window's messages are pumped");
  DestroyWindow(window);
  auto none = platform::KeyReader::ForOgreWindow([](const char*, void*) {});
  Check(none == nullptr, "no reader without a window");
}

}  // namespace

// The OpenGL 3.3 probe: every step made to fail returns false and leaves nothing behind (no window
// or window class, no current context, no GDI objects). Without failures the result is the
// machine's (no GPU driver: false), and the cleanup is the same.
void TestGlProbe() {
  namespace probe = platform::gl_probe;
  const HANDLE process = GetCurrentProcess();
  // opengl32 and the driver keep objects of their own from their first use on: counted after it.
  probe::CanCreateGl33Context();
  const DWORD gdi_before = GetGuiResources(process, GR_GDIOBJECTS);
  for (int round = 0; round < 20; ++round) {
    for (int step = 0; step <= int(probe::Step::kNone); ++step) {
      const bool ok = probe::CanCreateGl33Context(probe::Step(step));
      if (probe::Step(step) != probe::Step::kNone) Check(!ok, "a failed step makes the probe false");
      WNDCLASSEXW info = {};
      info.cbSize = sizeof(info);
      Check(!FindWindowW(probe::kWindowClass, nullptr), "no probe window left");
      Check(!GetClassInfoExW(GetModuleHandleW(nullptr), probe::kWindowClass, &info),
            "the probe's window class unregistered");
      Check(!wglGetCurrentContext(), "no GL context left current");
    }
  }
  Check(GetGuiResources(process, GR_GDIOBJECTS) == gdi_before, "no GDI objects left");
  const bool gl33 = platform::CanCreateGl33Context();
  std::printf("platform_win_test: OpenGL 3.3 here: %s\n", gl33 ? "yes" : "no");
}

int main() {
  TestGlProbe();
  TestGpus();
  TestUserFolderBases();
  TestDirs();
  TestNonAsciiDirs();
  TestKeyReader();
  std::printf("platform_win_test: ok\n");
  return 0;
}
