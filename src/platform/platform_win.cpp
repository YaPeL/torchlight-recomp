// Windows implementation of the platform module: the game window is SDL's (its "windows" video
// driver); OGRE GL (WGL) draws on that window itself (on Win32 OGRE takes a parentWindowHandle as
// its own window, as an externalWindowHandle, and creates no child). The user's folders come from
// user_folders.h (user_folders_win.cpp: known folders), in UTF-16 from the system and UTF-8 to the
// rest of the code. GPUs are DXGI's adapters (gpu_adapters.h): Direct3D 11 renders on the one
// chosen, OpenGL on the one Windows gives the process (no per-GPU lever for it; docs/
// windows-port.md).

#include "platform/gl_probe_win.h"
#include "platform/gpu_adapters.h"
#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/user_folders.h"

#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

// Asks the NVIDIA (Optimus) and AMD (switchable graphics) drivers of a machine with an integrated
// and a dedicated GPU to run this process on the dedicated one, for OpenGL and Direct3D alike: they
// read these exports from the executable when it starts. It is the only lever OpenGL has on
// Windows; Windows' own per-application graphics setting, when the user sets one, takes precedence.
// Not verified on a hybrid machine yet (docs/windows-port.md).
extern "C" {
__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

namespace torchlight::platform {

namespace {

std::string Utf8(const std::wstring& wide) {
  if (wide.empty()) return "";
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string utf8(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), int(wide.size()), utf8.data(), size, nullptr,
                      nullptr);
  return utf8;
}

// <user folder of `kind`>\<sub>, created if missing, in UTF-8 and ending in a separator; empty
// when there is none.
std::string UserDir(UserFolderKind kind, const std::filesystem::path& sub = {}) {
  const std::filesystem::path folder = UserFolder(kind);
  if (folder.empty()) return "";
  const std::filesystem::path dir = sub.empty() ? folder : folder / sub;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return "";
  return Utf8(dir.wstring()) + "\\";
}

// DXGI's adapters in EnumAdapters1 order (OGRE's Direct3D 11 order), and their LUIDs in the high
// performance order (IDXGIFactory6::EnumAdapterByGpuPreference, Windows 10 1803 and later; empty
// before that).
void DxgiAdapters(std::vector<gpu_adapters::Adapter>& adapters, std::vector<uint64_t>& preference) {
  using Microsoft::WRL::ComPtr;
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return;
  auto luid = [](const LUID& l) { return uint64_t(uint32_t(l.HighPart)) << 32 | l.LowPart; };
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
    DXGI_ADAPTER_DESC1 desc;
    if (FAILED(adapter->GetDesc1(&desc))) continue;
    adapters.push_back({Utf8(desc.Description), desc.VendorId, desc.DeviceId, desc.SubSysId,
                        desc.Revision, (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0,
                        luid(desc.AdapterLuid)});
  }
  ComPtr<IDXGIFactory6> factory6;
  if (FAILED(factory.As(&factory6))) return;
  for (UINT i = 0; factory6->EnumAdapterByGpuPreference(
                       i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) !=
                   DXGI_ERROR_NOT_FOUND;
       ++i) {
    DXGI_ADAPTER_DESC1 desc;
    if (SUCCEEDED(adapter->GetDesc1(&desc))) preference.push_back(luid(desc.AdapterLuid));
  }
}

std::string VendorName(uint32_t vendor) {
  switch (vendor) {
    case 0x10de: return "nvidia";
    case 0x1002: return "amd";
    case 0x8086: return "intel";
  }
  return "";
}

std::vector<Gpu> ListDxgiGpus(bool include_software) {
  std::vector<gpu_adapters::Adapter> adapters;
  std::vector<uint64_t> preference;
  DxgiAdapters(adapters, preference);
  std::vector<Gpu> gpus;
  for (const auto& gpu : gpu_adapters::ListGpus(adapters, preference, include_software)) {
    uint32_t vendor = 0;
    std::sscanf(gpu.id.c_str(), "pci:%x", &vendor);
    // No boot GPU: the default is automatic (Windows' choice), not one of the list.
    gpus.push_back({gpu.id, gpu.name, gpu.software ? "software" : VendorName(vendor), false});
  }
  return gpus;
}

}  // namespace

bool NativeGameWindow(SDL_Window* game, NativeWindow& window) {
  window = NativeWindow();
  window.system = NativeWindow::kWin32;
  window.window = uint64_t(uintptr_t(SDL_GetPointerProperty(
      SDL_GetWindowProperties(game), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr)));
  return window.window != 0;
}

std::string EmbeddingVideoError() {
  const std::string current = VideoDriver();
  if (current == "windows") return "";
  return "SDL video driver " + current + ": the backend draws only in windows (Win32) windows";
}

// SDL fills its Win32 windows with black itself on WM_ERASEBKGND (SDL_windowsevents.c, by default
// on the first erase: SDL_HINT_WINDOWS_ERASE_BACKGROUND_MODE).
void BlackGameWindowBackground() {}

bool HasDisplay() { return true; }

std::string ShaderCacheDir() { return UserDir(UserFolderKind::kCache, "ogre"); }

std::string CacheDir(const std::string& name) {
  // `name` is UTF-8 (a std::string path would be read in the ANSI code page).
  return UserDir(UserFolderKind::kCache,
                 std::filesystem::path(std::u8string(name.begin(), name.end())));
}

std::string ConfigDir() { return UserDir(UserFolderKind::kConfig); }

std::string DataDir() { return UserDir(UserFolderKind::kData); }

std::string GameDataDir() {
  const std::filesystem::path folder = UserFolder(UserFolderKind::kGameData);
  return folder.empty() ? "" : Utf8(folder.wstring()) + "\\";
}

std::string LogDir() { return UserDir(UserFolderKind::kState, "logs"); }

std::string ExecutableDir() {
  std::wstring path(MAX_PATH, L'\0');
  for (;;) {
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (size == 0) return "";
    if (size < path.size()) {
      path.resize(size);
      break;
    }
    path.resize(path.size() * 2);
  }
  return Utf8(std::filesystem::path(path).parent_path().wstring()) + "\\";
}

std::string OgrePluginDir() {
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre\\plugins\\";
}

std::string OgreMediaDir() {
  const std::string exe = ExecutableDir();
  return exe.empty() ? "" : exe + "ogre\\media\\";
}

std::vector<Gpu> Gpus() { return ListDxgiGpus(false); }

std::vector<Gpu> AllGpus() { return ListDxgiGpus(true); }

// Direct3D 11 renders on any of them (the backend passes it to OGRE).
std::vector<Gpu> SelectableGpus(const std::vector<Gpu>& all) { return all; }

std::vector<std::pair<std::string, std::string>> GpuEnvironment(const std::vector<Gpu>&,
                                                                const std::string&) {
  return {};
}

// Nothing to set before the graphics libraries load: the backend applies the choice when it
// creates its render system (RenderingDeviceForGpu), and a GPU no longer present falls back to
// the automatic choice there (settings::Normalize drops it from the settings).
bool SelectGpu(const std::string&, std::string&) { return true; }

bool GpuChoiceNeedsDirect3D11() { return true; }

std::string RenderingDeviceForGpu(const std::string& id,
                                  const std::vector<std::string>& ogre_devices) {
  std::vector<gpu_adapters::Adapter> adapters;
  std::vector<uint64_t> preference;
  DxgiAdapters(adapters, preference);
  return gpu_adapters::RenderingDevice(adapters, id, ogre_devices);
}

std::vector<std::pair<std::string, std::string>> OgreWindowParams(const NativeWindow& window) {
  return {{"parentWindowHandle", std::to_string(window.window)}};
}

std::string OgreRenderSystemDir(const std::string& plugin_dir, const NativeWindow*) {
  return plugin_dir;
}

bool HasDirect3D11(const std::string& plugin_dir) {
  // OGRE appends _d to the plugin's name in its Debug build.
  std::error_code ec;
  return !plugin_dir.empty() &&
         (std::filesystem::exists(plugin_dir + "RenderSystem_Direct3D11.dll", ec) ||
          std::filesystem::exists(plugin_dir + "RenderSystem_Direct3D11_d.dll", ec));
}

bool CanCreateGl33Context() { return gl_probe::CanCreateGl33Context(); }

namespace {

// Win32: nobody else pumps the messages of a top-level window the backend created (OGRE leaves it
// to the application), so reading its keys also dispatches its pending messages, as OGRE's
// WindowEventUtilities::messagePump would; without that Windows marks the window as not
// responding.
class Win32KeyReader : public KeyReader {
 public:
  explicit Win32KeyReader(HWND window) : window_(window) {}
  bool TakeF9() override {
    bool f9 = false;
    MSG message;
    while (PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) {
      if (message.message == WM_KEYDOWN && message.wParam == VK_F9) f9 = true;
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    return f9;
  }

 private:
  HWND window_;
};

}  // namespace

std::unique_ptr<KeyReader> KeyReader::ForOgreWindow(
    const std::function<void(const char* name, void* out)>& custom_attribute) {
  HWND window = nullptr;
  custom_attribute("WINDOW", &window);
  if (!window) return nullptr;
  return std::make_unique<Win32KeyReader>(window);
}

}  // namespace torchlight::platform
