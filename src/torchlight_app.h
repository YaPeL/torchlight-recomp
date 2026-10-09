// torchlight - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

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
#include "game_menu/video_menu.h"
#include "hooks/guest_copy_hooks.h"
#include "hooks/guest_d3d_skip.h"
#include "hooks/bucket_cull_hooks.h"
#include "hooks/video_mode_hooks.h"
#include "live/install.h"
#include "launcher/first_start.h"
#include "platform/platform.h"
#include "platform/user_folders.h"
#include "settings/host_settings.h"

class TorchlightApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    // First of all, before anything opens or creates the user's folders: the ones with the old
    // name move to the new one (logged once logging is up).
    startup_log() = torchlight::platform::MigrateUserFolders();
    // The game's files (installed from the user's package on the first start, REL.4; checked
    // in --game_data_root) and the achievement set (asked once, before the host settings are
    // read): the launcher when something is missing or --launcher asks for it (docs/launcher.md).
    switch (torchlight::launcher::RunFirstStart(startup_log())) {
      case torchlight::launcher::FirstStart::kPlay:
        break;
      case torchlight::launcher::FirstStart::kQuit:
        std::exit(EXIT_SUCCESS);  // the user quit; nothing is running yet
      case torchlight::launcher::FirstStart::kFailed:
        std::exit(EXIT_FAILURE);
    }
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
    {
      torchlight::settings::HostSettings host;
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
    // The player's achievement progress, in the user data folder (none: native mode stays off).
    const std::string data_dir = torchlight::platform::DataDir();
    torchlight::achievements::Install(
        data_dir.empty() ? std::filesystem::path() : std::filesystem::path(data_dir) / "achievements");
    // Before the guest runs: the video mode for the aspect setting, then the frame for an aspect
    // ratio the game has no mode for.
    torchlight::live::ApplyAspect();
    torchlight::hooks::InstallVideoMode();
    torchlight::hooks::LogGuestCopyMode();
    torchlight::hooks::InstallGuestD3DSkip(torchlight::live::OnlyMode());
    torchlight::hooks::InstallBucketCull(torchlight::live::OnlyMode());
    auto* graphics = runtime() ? runtime()->graphics_system() : nullptr;
    torchlight::capture::Install(graphics ? graphics->presenter() : nullptr);
    // The video menu changes what the native backend draws: only mode.
    if (torchlight::live::OnlyMode()) torchlight::game_menu::Install(runtime(), game_data_root());
    // PC saves from the game files' import/ folder: the part confirmed before is applied here,
    // before the guest runs; old save backups are pruned too.
    torchlight::game_menu::InstallSaveImport(runtime(), game_data_root(), user_data_root());
    torchlight::live::Install(
        game_data_root(),
        {&app_context(), window(), runtime(),
         [this](ImFontAtlas* atlas) { OnConfigureFonts(atlas); },
         [this](ImGuiStyle& imgui_style, rex::ui::Style& ui_style) {
           OnConfigureStyle(imgui_style, ui_style);
         }});
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
