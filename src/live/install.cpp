#include "live/install.h"
#include "live/frame_counter.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/graphics_system.h>
#include <rex/input/input_system.h>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>
#include <rex/ui/presenter.h>

#include "hooks/video_mode.h"
#include "live/deferred_check.h"
#include "live/live_mode.h"
#include "live/log_budget.h"
#include "live/ui_gamepad.h"
#include "live/ui_overlay.h"
#include "platform/platform.h"
#include "settings/host_settings.h"

REXCVAR_DEFINE_STRING(native_live, "only", "Torchlight",
                      "Renderer: only (the default: the native OGRE backend is the only renderer, "
                      "Xenos off, GPU plugin rexgpu-null); off (the emulated Xenos GPU; also chosen "
                      "by an explicit --gpu_plugin other than null when this flag is not given); "
                      "parallel (Xenos, and every frame also drawn by the native backend in a "
                      "second window). parallel_nodraw is for diagnostics: frames recorded and "
                      "consumed, nothing drawn (no GL)");

REXCVAR_DEFINE_STRING(live_record, "", "Torchlight",
                      "Live mode: record every frame the native backend consumes to this file "
                      "(session recording, replay --session); empty does not record");
REXCVAR_DEFINE_BOOL(native_producer_timing, false, "Torchlight",
                    "Live mode: time the recording cost per hook and section and log it (live "
                    "producer, live measurements); off by default, it costs the guest's render "
                    "thread about 2 %. The frame time statistics are always on");
REXCVAR_DEFINE_UINT32(live_record_max_mb, 4096, "Torchlight",
                      "Live mode: the session recording stops (well formed) at this size in MiB");

namespace torchlight::live {

namespace {

// OGRE's names of the render systems the backend draws with
// (settings::HostSettings::render_system).
constexpr const char* kGl3PlusRenderSystemName = "OpenGL 3+ Rendering Subsystem";
constexpr const char* kD3D11RenderSystemName = "Direct3D11 Rendering Subsystem";

// --native_live, "only" by default. An explicit --gpu_plugin other than null (command line or
// config) without an explicit --native_live asks for the emulated GPU: the commands from before
// only mode became the default keep running Xenos.
std::string Mode() {
  const rex::cvar::FlagEntry* live = rex::cvar::GetFlagInfo("native_live");
  const rex::cvar::FlagEntry* gpu = rex::cvar::GetFlagInfo("gpu_plugin");
  if (live && live->source == rex::cvar::Source::kDefault && gpu &&
      gpu->source != rex::cvar::Source::kDefault) {
    const std::string plugin = rex::cvar::Query<std::string>("gpu_plugin");
    if (!plugin.empty() && plugin != "null") return "off";
  }
  return REXCVAR_GET(native_live);
}
bool Parallel() { return Mode() == "parallel"; }
bool ParallelNoDraw() { return Mode() == "parallel_nodraw"; }
bool Only() { return Mode() == "only"; }

// Only mode: the runtime's ImGui dialogs, which without a presenter would be answered headless
// (the SDK's XAM UI completes them at once). A drawer published to the runtime, whose immediate
// drawer is the UiOverlay the live consumer reads. Created, drawn and destroyed on the UI thread
// (ImGuiDrawer has no locks); the consumer only asks for a frame.
struct Dialogs {
  DialogHost host;
  std::unique_ptr<UiOverlay> overlay;
  std::unique_ptr<rex::ui::ImGuiDrawer> drawer;
  std::atomic<bool> frame_requested{false};
  bool drawing = false;  // UI thread
};
std::unique_ptr<Dialogs> g_dialogs;
// The guest's input (pad and keyboard drivers) is off while a dialog has it (ui_gamepad.h). Read
// by the input drivers on the guest's threads, written on the UI thread; static storage, so the
// drivers' callback never outlives it.
std::atomic<bool> g_guest_input_blocked{false};

void InstallDialogs(const DialogHost& host) {
  if (!host.app_context || !host.window || !host.runtime) {
    REXLOG_ERROR("live: only mode without the app's window or runtime; dialogs stay headless");
    return;
  }
  g_dialogs = std::make_unique<Dialogs>();
  g_dialogs->host = host;
  g_dialogs->overlay = std::make_unique<UiOverlay>();
  // The same z order, fonts and style as the SDK's own drawer (ReXApp::SetupOverlays).
  g_dialogs->drawer =
      std::make_unique<rex::ui::ImGuiDrawer>(host.window, 64, host.fonts, host.style);
  g_dialogs->drawer->SetPresenterAndImmediateDrawer(nullptr, g_dialogs->overlay.get());
  host.runtime->set_imgui_drawer(g_dialogs->drawer.get());
  // As ReXApp does for its own overlays: the input drivers stop feeding the guest while the
  // dialogs have the input.
  if (auto* input = static_cast<rex::input::InputSystem*>(host.runtime->input_system())) {
    input->SetActiveCallback([] { return !g_guest_input_blocked.load(); });
  }
  REXLOG_INFO("live: only mode, runtime dialogs drawn by the native backend");
}

// Consumer thread, once per presented frame: the UI thread draws the dialogs into the overlay (or
// clears the UI once they are gone). At most one request is pending.
void RequestDialogFrame() {
  Dialogs* dialogs = g_dialogs.get();
  if (!dialogs || dialogs->frame_requested.exchange(true)) return;
  dialogs->host.app_context->CallInUIThread([] {
    Dialogs* d = g_dialogs.get();
    if (!d) return;
    d->frame_requested = false;
    // The frame counter (frame_counter.h) is drawn but is not a dialog that takes the input: it is
    // left out of "a dialog is open".
    rex::ui::ImGuiDialog* counter = FrameCounterDialog();
    if (counter) d->drawer->RemoveDialog(counter);
    const bool open = d->drawer->HasDialogs();
    if (counter) d->drawer->AddDialog(counter);
    const platform::GamepadState pad = platform::ReadGamepad();
    g_guest_input_blocked = BlockGuestInput(open, g_guest_input_blocked.load(), pad);
    if (open || counter) {
      // Gamepad navigation (the drawer itself only takes keyboard, mouse and touch), only for the
      // dialogs.
      ImGuiIO& io = d->drawer->GetIO();
      if (open) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
      } else {
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
      }
      if (open && pad.connected) {
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
        for (const UiGamepadKey& k : GamepadToImGui(pad)) {
          io.AddKeyAnalogEvent(k.key, k.down, k.value);
        }
      } else {
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
      }
      rex::ui::Window* window = d->host.window;
      rex::ui::AppUIDrawContext context(window->GetActualPhysicalWidth(),
                                        window->GetActualPhysicalHeight());
      d->drawer->Draw(context);
      d->drawing = true;
    } else if (d->drawing) {
      d->overlay->PublishEmpty();
      d->drawing = false;
    }
  });
}

// Xenos (off, parallel): the SDK draws the runtime's dialogs, but its input callback only covers
// its own overlays (ReXApp: input off while one of them has the mouse), so the pad kept reaching
// the game under a XAM dialog. Replaced by one that also covers XAM dialogs (xeXamIsUIActive).
void BlockGuestInputUnderXamDialogs(rex::Runtime* runtime) {
  auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  if (!input) return;
  input->SetActiveCallback([runtime] {
    if (rex::kernel::xam::xeXamIsUIActive()) return false;
    rex::ui::ImGuiDrawer* drawer = runtime->imgui_drawer();
    return !(drawer && drawer->GetIO().WantCaptureMouse);
  });
}

void ShutdownDialogs() {
  if (!g_dialogs) return;
  g_dialogs->host.runtime->set_imgui_drawer(nullptr);
  // As ReXApp::OnDestroy does with its own: unlinked from the immediate drawer first (its font
  // texture goes back to the overlay), then destroyed.
  g_dialogs->drawer->SetPresenterAndImmediateDrawer(nullptr, nullptr);
  g_dialogs->drawer.reset();
  g_dialogs->overlay.reset();
  g_dialogs.reset();
  g_guest_input_blocked = false;
}

// The GPU plugin of --native_live=only: the guest's GPU protocol (ring, fences, interrupts,
// vblank) without drawing or presenting.
constexpr const char* kOnlyGpuPlugin = "null";
// The emulated GPU of --native_live=off and parallel.
constexpr const char* kXenosGpuPlugin = "xenos";
// Why the backend cannot draw in SDL's windows, found in PreCreate and logged in PreSetup:
// PreCreate runs before the log file exists.
std::string g_video_error;

// The host settings (settings/host_settings.h), read in PreCreate, before anything loads GL; what
// went wrong is logged in PreSetup.
struct HostSettingsFile {
  std::string path;
  settings::HostSettings values;
  std::vector<std::string> warnings;
  std::string error;
};
HostSettingsFile g_host_settings;
// Only mode: the GPU choice, applied in PreCreate before anything loads the graphics libraries;
// what went wrong (the choice then cannot be offered) is logged in PreSetup.
std::string g_gpu_error;

// The settings while running (only mode): what the machine offers, the settings normalized against
// it at Install and as changed since by the menu.
// Whether OpenGL 3.3, which GL3+ needs, can run here (platform::CanCreateGl33Context, timed): at
// startup when the session would use GL3+, else when the settings menu is built (HostCapabilities).
DeferredCheck g_gl33([] {
  const auto start = std::chrono::steady_clock::now();
  const bool gl33 = platform::CanCreateGl33Context();
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  REXLOG_INFO("live: OpenGL 3.3 {} ({:.1f} ms to check)", gl33 ? "available" : "not available", ms);
  return gl33;
});

struct RunningSettings {
  std::mutex mutex;
  settings::Capabilities caps;
  settings::HostSettings startup, current;
  rex::Runtime* runtime = nullptr;
  bool ready = false;
};
RunningSettings g_running;

void LoadHostSettings() {
  const std::string dir = platform::ConfigDir();
  if (dir.empty()) {
    g_host_settings.error = "no configuration directory; using the defaults";
    return;
  }
  g_host_settings.path = dir + std::string(settings::kFileName);
  settings::Load(g_host_settings.path, g_host_settings.values, g_host_settings.warnings,
                 g_host_settings.error);
}

void LogHostSettings() {
  const HostSettingsFile& file = g_host_settings;
  if (!file.error.empty()) REXLOG_ERROR("settings: {}", file.error);
  for (const std::string& warning : file.warnings) {
    REXLOG_WARN("settings: {}: {}", file.path, warning);
  }
  REXLOG_INFO("settings: {} (fps cap {}, vsync {}, language {})", file.path,
              file.values.fps_cap ? std::to_string(file.values.fps_cap) : "unlimited",
              file.values.vsync ? "on" : "off",
              file.values.language.empty() ? "default" : file.values.language);
}

// A language pack in use (settings::NativeLanguage false), set before the guest runs.
std::string g_language_pack;

// The game's language, only languages the game data has (settings::InstalledLanguages). The
// game's own translations (de, fr, es) through the console language the runtime reports
// (user_language cvar; a --user_language on the command line wins); language packs through the
// game's "Translate File" (game_menu/language_pack.cpp), the console language left as it is.
void ApplyLanguage(const std::string& language, const std::filesystem::path& game_data_root) {
  if (language.empty()) return;
  const std::vector<std::string> installed = settings::InstalledLanguages(game_data_root);
  if (std::find(installed.begin(), installed.end(), language) == installed.end()) {
    REXLOG_WARN("settings: language \"{}\" is not in the game data; default language", language);
    return;
  }
  if (!settings::NativeLanguage(language)) {
    g_language_pack = language;
    REXLOG_INFO("settings: language pack {} (translations/{}/)", language, language);
    return;
  }
  const std::optional<uint32_t> id = settings::ConsoleLanguageId(language);
  if (!id) return;
  const rex::cvar::FlagEntry* flag = rex::cvar::GetFlagInfo("user_language");
  if (!flag) {
    REXLOG_ERROR("settings: the runtime has no user_language cvar; language not applied");
    return;
  }
  if (flag->source == rex::cvar::Source::kCommandLine) {
    REXLOG_INFO("settings: language left to --user_language={} from the command line",
                rex::cvar::Query<uint32_t>("user_language"));
    return;
  }
  rex::cvar::SetFlagByName("user_language", std::to_string(*id));
  REXLOG_INFO("settings: language {} (console language {})", language, *id);
}

// Frame rate cap = the guest's vblank: with the runtime's --vsync cvar on, its vblank thread runs
// at SetGuestVblankRate (patches/rexglue-guest-vblank-rate.patch); off, unthrottled (no cap). Both
// are read by the vblank thread every tick. A --vsync given on the command line wins. The cvar
// lives in the GPU plugin, loaded after PreSetup: applied in Install, before the guest runs.
void ApplyFpsCap(uint32_t fps_cap, rex::Runtime* runtime) {
  const rex::cvar::FlagEntry* flag = rex::cvar::GetFlagInfo("vsync");
  // Both GPU plugins (xenos, null) derive their graphics system from rex::graphics::GraphicsSystem;
  // its type info lives in the plugin, so no dynamic_cast from here.
  auto* graphics = runtime ? static_cast<rex::graphics::GraphicsSystem*>(
                                 runtime->graphics_system())
                           : nullptr;
  if (!flag || !graphics) {
    REXLOG_ERROR("settings: no runtime vsync cvar or graphics system; fps cap not applied");
    return;
  }
  if (flag->source == rex::cvar::Source::kCommandLine) {
    REXLOG_INFO("settings: fps cap left to --vsync={} from the command line",
                rex::cvar::Query<bool>("vsync"));
    return;
  }
  graphics->SetGuestVblankRate(fps_cap);
  rex::cvar::SetFlagByName("vsync", fps_cap == 0 ? "false" : "true");
}

// What the video settings in the game's menu offer: the game display's modes, the languages in the
// game data, the render systems validated with replay (GL3+), any vblank rate (patch 10), and the
// GPUs the backend can be pointed at (when PreCreate could make the choice stick).
void InitRunningSettings(const std::filesystem::path& game_data_root, rex::Runtime* runtime) {
  settings::Capabilities caps;
  const platform::DisplayInfo display = platform::GameDisplay();
  caps.display = {display.width, display.height};
  for (const auto& [w, h] : display.modes) caps.display_modes.push_back({w, h});
  caps.display_hz = display.refresh_hz;
  // The first one offered is the automatic choice: Direct3D 11 where it is installed (Windows; less
  // CPU than OpenGL there, docs/windows-port.md), else GL3+.
  caps.render_systems = {kGl3PlusRenderSystemName};
  if (platform::HasDirect3D11(platform::OgrePluginDir())) {
    caps.render_systems.insert(caps.render_systems.begin(), kD3D11RenderSystemName);
  }
  // GL3+ needs OpenGL 3.3 and OGRE crashes without it (Windows with no GPU driver): it stays
  // installed, a saved choice of it is kept, and the session uses another (EffectiveRenderSystem).
  // The probe costs ~150-180 ms with a GPU driver, so it runs here only when the session would use
  // GL3+ and could fall back; otherwise it runs when the game builds its settings menu, which asks
  // for the capabilities to list the render systems (HostCapabilities; while the title screen
  // loads, not when the player opens Settings).
  if (settings::StartupNeedsRenderSystemCheck(caps, g_host_settings.values.render_system,
                                              kGl3PlusRenderSystemName)) {
    if (!g_gl33.Result()) caps.unavailable_render_systems = {kGl3PlusRenderSystemName};
  } else {
    REXLOG_INFO("live: OpenGL 3.3 check deferred to the settings menu");
  }
  for (const platform::Gpu& gpu : platform::SelectableGpus(platform::Gpus())) {
    caps.gpus.push_back({gpu.id, gpu.name, gpu.boot});
  }
  if (platform::GpuChoiceNeedsDirect3D11()) caps.gpu_render_systems = {kD3D11RenderSystemName};
  caps.gpu_selectable = g_gpu_error.empty();
  caps.any_vblank_rate = true;
  caps.languages = settings::InstalledLanguages(game_data_root);
  std::lock_guard lock(g_running.mutex);
  g_running.caps = caps;
  g_running.startup = settings::Normalize(g_host_settings.values, caps);
  const settings::HostSettings& stored = g_host_settings.values;
  if (!stored.gpu.empty() && g_running.startup.gpu.empty() && g_gpu_error.empty()) {
    bool present = false;
    for (const auto& gpu : caps.gpus) present = present || gpu.id == stored.gpu;
    if (present) {
      REXLOG_INFO("settings: GPU {} ({}): no GPU choice on this machine; automatic",
                  stored.gpu_name, stored.gpu);
    } else {
      REXLOG_WARN("settings: GPU {} ({}) not in this machine; automatic",
                  stored.gpu_name.empty() ? "?" : stored.gpu_name, stored.gpu);
    }
  } else if (!g_running.startup.gpu.empty() &&
             !settings::GpuChoiceApplies(caps, g_running.startup.render_system)) {
    REXLOG_INFO("settings: GPU {} kept but not used: the render system takes the system's GPU",
                g_running.startup.gpu_name);
  }
  g_running.current = g_running.startup;
  g_running.runtime = runtime;
  g_running.ready = true;
  REXLOG_INFO("settings: display {}x{} at {} Hz, {} modes, languages {}", display.width,
              display.height, display.refresh_hz, display.modes.size(), caps.languages.size());
}
}  // namespace

bool OnlyMode() { return Only(); }

namespace {
// The next <app>_NNN.log in `dir`, after the highest number there (the runtime's own naming).
std::filesystem::path NextLogPath(const std::filesystem::path& dir, const std::string& app) {
  const std::string prefix = app + "_";
  int highest = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    const std::string name = entry.path().filename().string();
    if (!name.starts_with(prefix) || !name.ends_with(".log")) continue;
    const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - 4);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) continue;
    highest = std::max(highest, std::atoi(digits.c_str()));
  }
  char file[64];
  std::snprintf(file, sizeof(file), "%s%03d.log", prefix.c_str(), highest + 1);
  return dir / file;
}
}  // namespace

void ConfigurePaths(const std::string& app_name, rex::PathConfig& paths) {
  if (const std::string dir = platform::ConfigDir(); !dir.empty()) {
    paths.config_path = std::filesystem::path(dir) / (app_name + ".toml");
  }
  if (rex::cvar::Query<std::string>("game_data_root").empty()) {
    if (const std::string game = platform::GameDataDir(); !game.empty()) {
      paths.game_data_root = game;
    }
  }
  // The published game runs as the full game (docs/release-pipeline.md, decision 1).
  if (const auto* license = rex::cvar::GetFlagInfo("license_mask");
      license && license->source != rex::cvar::Source::kCommandLine) {
    rex::cvar::SetFlagByName("license_mask", "1");
  }
  const std::string data = platform::DataDir();
  if (!data.empty() && rex::cvar::Query<std::string>("user_data_root").empty()) {
    paths.user_data_root = data;
    if (rex::cvar::Query<std::string>("cache_root").empty()) {
      paths.cache_root = paths.user_data_root / "cache";
    }
  }
  // A run's log rotates within the budget (the runtime's rotating file sink; on bd833a2 through
  // SDK patch 26, which brought it back).
  const LogBudget budget;
  const auto set_unless_command_line = [](const char* name, const std::string& value) {
    const rex::cvar::FlagEntry* entry = rex::cvar::GetFlagInfo(name);
    if (entry && entry->source != rex::cvar::Source::kCommandLine) {
      rex::cvar::SetFlagByName(name, value);
    }
  };
  set_unless_command_line("log_max_file_size_mb", std::to_string(budget.file_bytes >> 20));
  set_unless_command_line("log_max_files", std::to_string(RotatedFilesPerRun(budget)));

  const rex::cvar::FlagEntry* flag = rex::cvar::GetFlagInfo("log_file");
  if (!flag || flag->source == rex::cvar::Source::kCommandLine) return;
  const std::string dir = platform::LogDir();
  if (dir.empty()) return;  // the runtime's default, next to the executable
  if (const size_t removed = PruneLogFolder(dir, app_name, budget); removed > 0) {
    std::printf("torchlight: removed %zu old log files to keep the log folder under %llu MB\n",
                removed, static_cast<unsigned long long>(budget.folder_bytes >> 20));
  }
  rex::cvar::SetFlagByName("log_file", NextLogPath(dir, app_name).string());
}

void ApplyAspect() {
  if (!Only()) return;
  const rex::cvar::FlagEntry* width = rex::cvar::GetFlagInfo("video_mode_width");
  const rex::cvar::FlagEntry* height = rex::cvar::GetFlagInfo("video_mode_height");
  if (!width || !height) {
    REXLOG_ERROR("settings: the runtime has no video_mode cvars; aspect not applied");
    return;
  }
  if (width->source == rex::cvar::Source::kCommandLine ||
      height->source == rex::cvar::Source::kCommandLine) {
    REXLOG_INFO("settings: aspect left to --video_mode_width/height from the command line");
    return;
  }
  const settings::Aspect aspect = g_host_settings.values.aspect;
  const platform::DisplayInfo display = platform::GameDisplay();
  const hooks::FrameSize mode =
      hooks::VideoModeForAspect(settings::AspectRatio(aspect, {display.width, display.height}));
  rex::cvar::SetFlagByName("video_mode_width", std::to_string(mode.width));
  rex::cvar::SetFlagByName("video_mode_height", std::to_string(mode.height));
  REXLOG_INFO("settings: aspect {} (display {}x{}): video mode {}x{}",
              settings::AspectName(aspect), display.width, display.height, mode.width,
              mode.height);
}

void PreCreate() {
  LoadHostSettings();
  if (!Only()) return;
  if (!platform::SelectGpu(g_host_settings.values.gpu, g_gpu_error) && g_gpu_error.empty()) {
    g_gpu_error = "GPU choice failed";
  }
  // The backend draws inside the game window: its video driver must allow that.
  g_video_error = platform::EmbeddingVideoError();
  platform::BlackGameWindowBackground();
}

void PreSetup(rex::RuntimeConfig& config) {
  LogHostSettings();
  if (!Only()) {
    // The emulated GPU (--native_live=off or parallel): Xenos unless a plugin was given (an empty
    // plugin would run without GPU emulation).
    if (config.gpu_plugin.empty() || config.gpu_plugin == kOnlyGpuPlugin) {
      config.gpu_plugin = kXenosGpuPlugin;
    }
    REXLOG_INFO("live: --native_live={}, GPU plugin '{}'", Mode(), config.gpu_plugin);
  }
  if (Only()) {
    if (!config.gpu_plugin.empty() && config.gpu_plugin != kOnlyGpuPlugin) {
      REXLOG_WARN("live: only mode, GPU plugin forced from '{}' to '{}' (Xenos needs "
                  "--native_live=off)",
                  config.gpu_plugin, kOnlyGpuPlugin);
    }
    config.gpu_plugin = kOnlyGpuPlugin;
    if (!g_video_error.empty()) {
      REXLOG_ERROR("live: only mode, {}", g_video_error);
    } else {
      REXLOG_INFO("live: only mode, video driver {}", platform::VideoDriver());
    }
    if (!g_gpu_error.empty()) {
      REXLOG_WARN("settings: {}", g_gpu_error);
    } else {
      std::vector<settings::Capabilities::Gpu> gpus;
      for (const platform::Gpu& gpu : platform::Gpus()) gpus.push_back({gpu.id, gpu.name, gpu.boot});
      REXLOG_INFO("settings: GPU {}", settings::GpuLogLabel(g_host_settings.values, gpus));
    }
  }
  if (!Parallel() && !Only()) return;
  // The backend window may hold focus; gamepad input must not be dropped as background input.
  platform::AllowBackgroundGamepad();
}

void Install(const std::filesystem::path& game_data_root, const DialogHost& dialogs) {
  // Before the guest runs, in every mode.
  ApplyLanguage(g_host_settings.values.language, game_data_root);
  InstallFrameCounter(dialogs.runtime);
  if (!Only()) BlockGuestInputUnderXamDialogs(dialogs.runtime);
  std::string mode = Mode();
  if (mode == "off") return;
  if (!Parallel() && !ParallelNoDraw() && !Only()) {
    REXLOG_ERROR("live: unknown --native_live={} (off, parallel, only or parallel_nodraw); live "
                 "mode not started",
                 mode);
    return;
  }
  LiveOptions options;
  options.game_data_root = game_data_root;
  options.draw = !ParallelNoDraw();
  options.only = Only();
  options.record_path = REXCVAR_GET(live_record);
  options.producer_timing = REXCVAR_GET(native_producer_timing);
  // OGRE's log next to the runtime's (ConfigurePaths): <log>_ogre.log.
  if (const auto log_file = rex::cvar::Query<std::string>("log_file"); !log_file.empty()) {
    std::filesystem::path ogre_log(log_file);
    ogre_log.replace_filename(ogre_log.stem().string() + "_ogre.log");
    options.ogre_log_path = ogre_log.string();
  }
  options.record_max_bytes = uint64_t(REXCVAR_GET(live_record_max_mb)) << 20;
  if (!options.record_path.empty() && ParallelNoDraw()) {
    REXLOG_WARN("live: --live_record is ignored in parallel_nodraw (nothing is consumed)");
  }
  if (Only()) {
    // The backend draws inside the game window, which keeps focus, input and window management.
    std::string error;
    if (!platform::FindGameWindow(options.parent_window, options.window_width,
                                  options.window_height, error)) {
      REXLOG_ERROR("live: only mode needs the game window ({}); live mode not started", error);
      return;
    }
    // The guest's vblank is the frame rate cap (with Xenos, the runtime's own presentation keeps
    // its --vsync).
    ApplyFpsCap(g_host_settings.values.fps_cap, dialogs.runtime);
    InitRunningSettings(game_data_root, dialogs.runtime);
    // The game's display: vertical sync and internal resolution from the host settings.
    options.vsync = g_host_settings.values.vsync;
    options.render_resolution = g_host_settings.values.render_resolution;
    // The render system chosen in the Video column; empty: the first one offered. One this machine
    // cannot run gives way to the first that can for this session, and stays the saved choice.
    std::string render_system =
        settings::EffectiveRenderSystem(g_running.caps, g_running.startup.render_system);
    if (render_system.empty()) render_system = g_running.caps.render_systems.front();
    if (!g_running.startup.render_system.empty() &&
        render_system != g_running.startup.render_system) {
      REXLOG_WARN("live: {} cannot run here; using {} this session (the saved choice stays {})",
                  g_running.startup.render_system, render_system, g_running.startup.render_system);
    }
    options.render_system = render_system == kD3D11RenderSystemName ? TL_RENDER_SYSTEM_D3D11
                                                                     : TL_RENDER_SYSTEM_GL3PLUS;
    // The GPU chosen in the Video column, where the render system takes one (Windows: Direct3D 11;
    // Linux: applied before the graphics libraries loaded, platform::SelectGpu).
    if (platform::GpuChoiceNeedsDirect3D11() &&
        settings::GpuChoiceApplies(g_running.caps, g_running.startup.render_system)) {
      options.gpu = g_running.startup.gpu;
    }
    REXLOG_INFO("live: backend draws inside the game window ({} 0x{:X}, {}x{}), vsync {}",
                platform::SystemName(options.parent_window), options.parent_window.window,
                options.window_width, options.window_height, options.vsync ? "on" : "off");
    InstallDialogs(dialogs);
    if (g_dialogs) {
      options.ui = g_dialogs->overlay.get();
      options.request_ui_frame = RequestDialogFrame;
    }
  }
  LiveMode::Get().Start(options);
}

std::string LanguagePack() { return g_language_pack; }

settings::Capabilities HostCapabilities() {
  // The deferred OpenGL 3.3 check, run outside the lock, when startup did not need it: the game
  // builds its settings menu, with our video column, once while the title screen loads (game
  // thread).
  const bool gl33 = g_gl33.Result();
  std::lock_guard lock(g_running.mutex);
  if (!gl33) g_running.caps.unavailable_render_systems = {kGl3PlusRenderSystemName};
  return g_running.caps;
}

rex::ui::ImmediateDrawer* DialogImmediateDrawer() { return g_dialogs ? g_dialogs->overlay.get() : nullptr; }

settings::HostSettings CurrentHostSettings() {
  std::lock_guard lock(g_running.mutex);
  return g_running.current;
}

settings::HostSettings StartupHostSettings() {
  std::lock_guard lock(g_running.mutex);
  return g_running.startup;
}

void ChangeHostSettings(const settings::HostSettings& changed) {
  std::lock_guard lock(g_running.mutex);
  if (!g_running.ready) return;
  const settings::HostSettings before = g_running.current;
  g_running.current = changed;
  if (changed.render_resolution != before.render_resolution || changed.vsync != before.vsync) {
    LiveMode::Get().SetVideo(changed.render_resolution, changed.vsync);
  }
  if (changed.fps_cap != before.fps_cap) ApplyFpsCap(changed.fps_cap, g_running.runtime);
  if (g_host_settings.path.empty()) return;
  std::string error;
  if (!settings::Save(g_host_settings.path, changed, error)) {
    REXLOG_ERROR("settings: {}", error);
  }
}

void OnWindowPixelSizeChanged(uint32_t width, uint32_t height) {
  if (Only()) LiveMode::Get().ResizeWindow(width, height);
}

void Shutdown() {
  // The consumer first: it reads the overlay and asks for frames.
  LiveMode::Get().Stop();
  ShutdownDialogs();
}

}  // namespace torchlight::live
