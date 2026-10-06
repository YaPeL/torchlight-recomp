// Wires the live mode into the app: --native_live=off|parallel|only.

#pragma once

#include <filesystem>

#include "settings/host_settings.h"

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

namespace torchlight::live {

// Whether --native_live=only is on (the native backend is the only renderer).
bool OnlyMode();
// The runtime's own files in the user's folders (platform.h), not in the executable's directory,
// which may be read-only (an AppImage): its config in the configuration folder; its user data
// (and the cache below it) in the data folder unless --user_data_root (--cache_root) came; its log
// in the log folder, as <app>_NNN.log numbered like the runtime's own, unless --log_file came on
// the command line (a log_file in the config still wins: it is read afterwards); the game's files
// in platform::GameDataDir unless --game_data_root came; and license_mask 1 (the full game, not
// the demo) unless --license_mask came. OnConfigurePaths.
void ConfigurePaths(const std::string& app_name, rex::PathConfig& paths);
// In the app creator, before the window exists (SDL video is already up by then).
void PreCreate();
// Before the runtime sets up graphics and input (OnPreSetup).
void PreSetup(rex::RuntimeConfig& config);
// What the runtime's dialogs (XamShowKeyboardUI, XamShowMessageBoxUI) need in only mode, where
// the backend draws them (ui_overlay.h): the app's UI thread, window and runtime, and the fonts
// and style the app gives its own ImGui drawer.
struct DialogHost {
  rex::ui::WindowedAppContext* app_context = nullptr;
  rex::ui::Window* window = nullptr;
  rex::Runtime* runtime = nullptr;
  rex::ui::ImGuiDrawer::FontSetupCallback fonts;
  rex::ui::ImGuiDrawer::StyleSetupCallback style;
};

// Only mode, at the start of OnPostSetup (the window exists, the guest has not run): the video
// mode the runtime reports for the aspect setting (settings::Aspect, automatic = the game
// display's), unless --video_mode_width/height came on the command line.
void ApplyAspect();
// After the runtime is set up (OnPostSetup, UI thread).
void Install(const std::filesystem::path& game_data_root, const DialogHost& dialogs);
// The game window's pixel size changed (UI thread).
void OnWindowPixelSizeChanged(uint32_t width, uint32_t height);
// The language pack in use (translations/<code>/ of a language the game does not pick by itself),
// empty for English and the game's own translations. Set before the guest runs.
std::string LanguagePack();

// Only mode, after Install: the immediate drawer of the runtime's dialogs (the UiOverlay the backend
// draws), for ImGui dialogs that need textures (the achievement list's icons). Null otherwise.
// UI thread, like the dialogs.
rex::ui::ImmediateDrawer* DialogImmediateDrawer();

// The host settings while running (only mode, after Install), for the video settings in the
// game's menu. Any thread.
// What this machine offers.
settings::Capabilities HostCapabilities();
// The settings in effect, and as they were at startup (what needs a restart compares to these).
settings::HostSettings CurrentHostSettings();
settings::HostSettings StartupHostSettings();
// New settings: what applies while running (render resolution, vsync, frame rate cap) is applied,
// and the file is saved.
void ChangeHostSettings(const settings::HostSettings& changed);
// At shutdown (UI thread).
void Shutdown();

}  // namespace torchlight::live
