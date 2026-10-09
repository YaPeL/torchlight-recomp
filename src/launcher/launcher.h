// The launcher (docs/launcher.md, stage 1): its window, the view and the model, and what the
// model's actions do. What touches the system (the pickers, reading folders, the install, saving
// the achievement set) goes through LauncherServices, so tests drive the whole launcher with fakes.

#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "game_setup/install.h"
#include "game_setup/setup_text.h"
#include "launcher/file_browser_model.h"
#include "launcher/launcher_model.h"
#include "launcher/launcher_view.h"
#include "platform/platform.h"
#include "platform/user_folders.h"
#include "settings/host_settings.h"

namespace torchlight::launcher {

struct LauncherServices {
  // The system's picker for the package (kPackage) or a folder (kFolder); blocks until it closes.
  std::function<PickOutcome(game_setup::Source source, std::string& path, std::string& error)>
      pick;
  // The fallback browser's folders and starting points.
  ListFolder list_folder;
  std::vector<std::filesystem::path> places;
  // On the install's thread: checks the package and installs the game's files; `progress` returns
  // false once the user cancels.
  std::function<game_setup::InstallResult(game_setup::Source source,
                                          const std::filesystem::path& from,
                                          const game_setup::PhaseProgress& progress)>
      install;
  // Saves the achievement set; false with the reason.
  std::function<bool(settings::AchievementSet set, std::string& error)> save_achievements;
};

// The real ones: the platform's pickers (titles in `tr`), the disk, the package check and
// game_setup::Install into `game_dir`, settings.toml in the config folder.
LauncherServices SystemServices(const std::filesystem::path& game_dir,
                                const game_setup::Translate& tr);

enum class Outcome { kPlay, kQuit };

class Launcher {
 public:
  // `log` gets what was done, for the runtime's log (logging is not up yet).
  Launcher(platform::LauncherWindow& window, const Start& start, LauncherServices services,
           game_setup::Translate tr, std::string game_dir,
           std::vector<platform::StartupMessage>& log);
  // Stops a running install and waits for it.
  ~Launcher();

  // One frame: events, the view, the actions. The outcome once the user chose Play or Quit.
  std::optional<Outcome> Step();

  const LauncherModel& model() const { return model_; }
  const FileBrowserModel* browser() const { return browser_.get(); }

 private:
  std::optional<Outcome> Do(Action action);
  // The install's progress for the model; the action once it has ended.
  Action PollInstall();

  platform::LauncherWindow& window_;
  LauncherModel model_;
  LauncherView view_;
  LauncherServices services_;
  game_setup::Translate tr_;
  std::string game_dir_;
  std::vector<platform::StartupMessage>& log_;
  std::unique_ptr<FileBrowserModel> browser_;
  std::filesystem::path browse_folder_;  // where the browser was last, to open it there again

  // The install's thread and what it shares.
  std::thread worker_;
  std::atomic<bool> cancel_{false}, finished_{false};
  std::mutex mutex_;
  game_setup::Phase phase_ = game_setup::Phase::kCopying;
  uint64_t done_ = 0, total_ = 0;
  game_setup::InstallResult result_;
};

// Opens the launcher's window and runs the launcher until Play or Quit; nothing when the window
// cannot open (`error` says why).
std::optional<Outcome> RunLauncher(const Start& start, LauncherServices services,
                                   const game_setup::Translate& tr, const std::string& game_dir,
                                   std::vector<platform::StartupMessage>& log,
                                   std::string& error);

}  // namespace torchlight::launcher
