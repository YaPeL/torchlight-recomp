// torchlight - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <chrono>
#include <cstdio>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/overlay/achievement_notification.h>

#include "build_info.h"

#include "capture/install.h"
#include "achievements/achievement_list.h"
#include "achievements/runtime.h"
#include "game_menu/save_import_menu.h"
#include "game_menu/mods_install.h"
#include "mods/save_units_install.h"
#include "mods/unit_index_install.h"
#include "game_menu/video_menu.h"
#include "hooks/guest_copy_hooks.h"
#include "hooks/guest_d3d_skip.h"
#include "hooks/bucket_cull_hooks.h"
#include "hooks/frustum_hooks.h"
#include "hooks/rtss_listener_hooks.h"
#include "hooks/video_mode_hooks.h"
#include "live/install.h"
#include "dev/script_input.h"
#include "game_setup/first_run.h"
#include "live/system_back_button.h"
#include "live/touch_input.h"
#include "platform/platform.h"
#include "platform/user_folders.h"
#include "settings/host_settings.h"

// The time a step before the guest runs takes, logged when it ends ("startup: <what> in N ms"):
// with mods some of them read many files, and nothing is drawn meanwhile.
class StartupStep {
 public:
  explicit StartupStep(const char* what) : what_(what), start_(std::chrono::steady_clock::now()) {}
  ~StartupStep() {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_);
    REXLOG_INFO("startup: {} in {} ms", what_, ms.count());
  }

 private:
  const char* what_;
  std::chrono::steady_clock::time_point start_;
};

class TorchlightApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    // First of all, before anything opens or creates the user's folders: the ones with the old
    // name move to the new one (logged once logging is up).
    startup_log() = torchlight::platform::MigrateUserFolders();
    // The game's files: installed from the user's package on the first start (REL.4), unless
    // --game_data_root points at them.
    if (rex::cvar::Query<std::string>("game_data_root").empty()) {
      const std::string game_dir = torchlight::platform::GameDataDir();
      if (!game_dir.empty() &&
          !torchlight::game_setup::EnsureGameData(game_dir, startup_log())) {
        std::exit(EXIT_SUCCESS);  // the user quit the setup; nothing is running yet
      }
    }
    // The achievement set, asked once (before the host settings are read).
    torchlight::game_setup::EnsureAchievementChoice(startup_log());
    torchlight::live::PreCreate();
    return std::unique_ptr<TorchlightApp>(new TorchlightApp(ctx, "torchlight",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    torchlight::live::PreSetup(config);
  }

  void OnPostSetup() override {
    // Achievement set (settings.toml "achievements"): Xbox by default, also when the key is missing
    // or invalid (the settings log reports an invalid value).
    // Read here from the file (as live's PreCreate did): live's startup settings are only filled
    // later, when only mode finishes its setup. live already logs any bad value.
    torchlight::settings::HostSettings host;
    {
      std::vector<std::string> warnings;
      std::string error;
      const std::string config_dir = torchlight::platform::ConfigDir();
      if (!config_dir.empty()) {
        torchlight::settings::Load(config_dir + std::string(torchlight::settings::kFileName), host,
                                   warnings, error);
      }
      const auto set = torchlight::settings::EffectiveAchievements(host);
      if (!host.achievements) {
        REXLOG_INFO("achievements: no valid \"achievements\" setting; using \"xbox\"");
      }
      torchlight::achievements::ApplyAchievementSet(set == torchlight::settings::AchievementSet::kPc);
    }
    // The game's Achievements button opens our list of the set in use, on the UI thread.
    torchlight::achievements::SetAchievementListHandler([this] {
      app_context().CallInUIThread([this] {
        // Icons: the SDK's immediate drawer in Xenos, the dialogs' overlay in only mode.
        rex::ui::ImmediateDrawer* icons = immediate_drawer();
        if (!icons) icons = torchlight::live::DialogImmediateDrawer();
        torchlight::achievements::OpenAchievementList(runtime(), icons);
      });
    });
    // The player's folders in use: the platform's, or below --user_data_root / --cache_root, so a
    // run with its own roots never touches the player's mods, caches or progress.
    const torchlight::settings::FoldersInUse folders = torchlight::live::UserFoldersInUse(*runtime());
    // The player's achievement progress, in the user data folder (none: native mode stays off).
    torchlight::achievements::Install(folders.achievements);
    // Before the guest runs: the video mode for the aspect setting, then the frame for an aspect
    // ratio the game has no mode for.
    torchlight::live::ApplyAspect();
    torchlight::hooks::InstallVideoMode();
    torchlight::hooks::LogGuestCopyMode();
    torchlight::hooks::InstallFrustumTest();
    torchlight::hooks::InstallGuestD3DSkip(torchlight::live::OnlyMode());
    torchlight::hooks::InstallBucketCull(torchlight::live::OnlyMode());
    torchlight::hooks::InstallRtssListener(torchlight::live::OnlyMode());
    auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
    torchlight::capture::Install(graphics ? graphics->presenter() : nullptr);
    // The video menu changes what the native backend draws: only mode.
    if (torchlight::live::OnlyMode()) torchlight::game_menu::Install(runtime(), game_data_root());
    // PC saves from the game files' import/ folder: the part confirmed before is applied here,
    // before the guest runs; old save backups are pruned too.
    {
      const StartupStep step("save import");
      torchlight::game_menu::InstallSaveImport(runtime(), game_data_root(), user_data_root());
    }
    // The player's PC mods (any mode): mounted and their saves protected before the guest runs.
    {
      const StartupStep step("mods");
      torchlight::game_menu::InstallMods(runtime(), folders.data, folders.generated, user_data_root(),
                                         game_data_root() / "pak.zip");
    }
    // The pad prompts in the style of the player's pad (settings.toml "pad_prompts",
    // "swap_confirm_cancel"), before the guest loads its imagesets.
    {
      const StartupStep step("pad prompts");
      using torchlight::settings::PadPrompts;
      std::optional<torchlight::mods::PadFamily> chosen;
      if (host.pad_prompts == PadPrompts::kXbox) chosen = torchlight::mods::PadFamily::kXbox;
      if (host.pad_prompts == PadPrompts::kPlayStation) chosen = torchlight::mods::PadFamily::kPlayStation;
      if (host.pad_prompts == PadPrompts::kNintendo) chosen = torchlight::mods::PadFamily::kNintendo;
      torchlight::game_menu::InstallPadPrompts(runtime(), folders.generated, game_data_root() / "pak.zip",
                                               chosen, host.swap_confirm_cancel);
    }
    // Their units in the game's unit index (cached in the user's folder when mods bring any).
    // The mods' unit definitions, listed once for both steps below (their sizes and times, the
    // files not opened).
    std::vector<torchlight::mods::ModUnitFile> unit_files;
    if (const torchlight::mods::ModPlan* plan = torchlight::game_menu::MountedModPlan()) {
      const StartupStep step("mods' unit files");
      unit_files = torchlight::mods::ScanModUnitFiles(torchlight::game_menu::ModsFolder(), *plan);
    }
    {
      const StartupStep step("unit index");
      torchlight::mods::InstallUnitIndex(runtime(), unit_files, folders.cache, game_data_root() / "pak.zip");
    }
    // Items and creatures the game would not know (a removed mod's) out of the saves, backed up
    // first, so a save never stops the game.
    {
      const StartupStep step("saves' units");
      torchlight::mods::InstallSaveUnits(folders.data, unit_files, folders.cache, game_data_root() / "pak.zip",
                                         user_data_root(), torchlight::game_menu::MountedModPlan(),
                                         torchlight::mods::UnitIndexBuiltInGame());
    }
    // Android: the system's Back button as the pad's Back (before live::Install, as the script).
    torchlight::live::InstallSystemBackButton(runtime());
    // The touch controls (settings.toml "touch_controls"): a controller of their own fed by the
    // fingers (docs/android-port.md, A4).
    torchlight::live::InstallTouchControls(
        runtime(), torchlight::settings::EffectiveTouchControls(host, torchlight::platform::TouchControlsByDefault()));
    // Development builds: a controller script (before live::Install sets the input's active
    // callback, which the script's controller follows too).
    torchlight::dev::InstallInputScript(runtime(), [this] {
      app_context().CallInUIThread([this] {
        if (window()) window()->RequestClose();
      });
    });
    torchlight::live::Install(
        game_data_root(),
        {&app_context(), window(), runtime(),
         [this](ImFontAtlas* atlas) { OnConfigureFonts(atlas); },
         [this](ImGuiStyle& imgui_style, rex::ui::Style& ui_style) {
           OnConfigureStyle(imgui_style, ui_style);
         },
         // The renderer could not start: a message box, then the game closes (a black window
         // otherwise).
         [this](const std::string& reason) {
           app_context().CallInUIThread([this, reason] {
             REXLOG_ERROR("live: the renderer could not start; telling the player and closing");
             torchlight::game_setup::ShowRendererError(reason);
             if (window()) window()->RequestClose();
           });
         }},
        TORCHLIGHT_BUILD_COMMIT);
  }

  void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override {
    torchlight::live::OnWindowPixelSizeChanged(pixel_width, pixel_height);
  }

  bool OnWindowCloseRequested() override {
    // SDK's accepted-window-close path hard-exits and skips OnShutdown.
    torchlight::achievements::PrepareForClose();
    return true;
  }

  void OnShutdown() override {
    torchlight::achievements::Shutdown();
    torchlight::live::Shutdown();
  }

  // The runtime's config and log in the user's folders (the executable's may be read-only).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    torchlight::live::ConfigurePaths(GetName(), paths);
  }

  // Logs of every worktree and build share one folder: the first line says which one wrote it.
  void OnPostInitLogging() override {
    REXLOG_INFO("torchlight: {} (build {})", rex::filesystem::GetExecutablePath().string(),
                TORCHLIGHT_BUILD_COMMIT);
    const auto log_file = rex::cvar::Query<std::string>("log_file");
    if (!log_file.empty()) std::printf("torchlight: log %s\n", log_file.c_str());
    for (const auto& message : startup_log()) {
      if (message.warning) {
        REXLOG_WARN("{}", message.text);
      } else {
        REXLOG_INFO("{}", message.text);
      }
      std::printf("torchlight: %s\n", message.text.c_str());
    }
  }

 private:
  // What happened before logging was up: user folders moved, the game's files installed.
  static std::vector<torchlight::platform::StartupMessage>& startup_log() {
    static std::vector<torchlight::platform::StartupMessage> messages;
    return messages;
  }

 public:

  // Xbox set: the game's own unlocks go through the SDK; announce them with the game UI toast
  // (achievements/toast.cpp) once the kernel exists. The PC set suppresses the Xbox path instead.
  void OnPreLaunchModule() override {
    if (!torchlight::achievements::NativeEnabled() && runtime() && runtime()->kernel_state()) {
      torchlight::achievements::UseXboxAchievements(runtime()->kernel_state()->achievements());
    }
  }

  // Our toast replaces the SDK's ImGui one in every mode (only mode has no SDK overlays anyway).
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog()
      override {
    return nullptr;
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
