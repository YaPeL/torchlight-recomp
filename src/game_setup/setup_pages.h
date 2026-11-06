// The first start's pages in the setup wizard (desktop): the game's files from a package or an
// extracted folder, then the achievement set, then the start. Closing the window before the end
// writes nothing of what was still being asked.

#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "game_setup/first_run_steps.h"
#include "game_setup/game_files.h"
#include "game_setup/install.h"
#include "game_setup/setup_text.h"
#include "platform/portable_copy.h"
#include "platform/user_folders.h"
#include "settings/host_settings.h"
#include "wizard/browser_page.h"
#include "wizard/wizard.h"
#include "wizard/wizard_ui.h"

namespace torchlight::game_setup {

struct SetupConfig {
  OpenQuestions questions;
  std::optional<StorageQuestion> storage;
  // Applies a storage answer; what is open then. Needed with `storage`.
  std::function<OpenQuestions(bool portable)> after_storage;
  std::span<const GameFile> files;
  Translate translate;
  bool own_browser_only = false;       // never the system's picker (a script's run)
  std::filesystem::path browse_from;   // the browser's first folder; empty: the home folder
  // A test's slower work, to see its page: each step of it waits until the working page has been
  // drawn this many more frames (frames, not time: a loaded machine cannot skip the page).
  int test_install_frames = 0;
  // A Steam Deck: with no pad the game would hear, the Ready page says how to play and closes
  // instead of starting the game (completed(), start_game() false).
  bool steam_deck = false;
  // Whether a pad the game would hear is connected; empty: SDL's gamepads, the script's simulated
  // one left out. A test's own answer, so a pad on the machine running it does not change the page.
  std::function<bool()> real_pad_connected;
};

class SetupPages : public wizard::Pages {
 public:
  explicit SetupPages(SetupConfig config);
  ~SetupPages() override;

  void Attach(const wizard::Fonts& fonts, SDL_Window* window) override;
  bool Frame() override;
  std::string Name() const override;
  std::string Detail() const override;
  bool InView() const override { return ui_.in_view(); }
  bool Busy() const override;

  // Every question answered and the player chose to start.
  bool completed() const { return completed_; }
  // Completed with the game to start now; false: answered and closed (SetupConfig::steam_deck).
  bool start_game() const { return start_game_; }
  bool installed() const { return installed_; }
  std::optional<settings::AchievementSet> achievements() const { return achievements_; }
  std::optional<bool> portable() const { return portable_; }
  // For the runtime's log: what each attempt did (true: an error).
  const std::vector<platform::StartupMessage>& log() const { return log_; }

 private:
  enum Page { kStorage, kSourceBrowser, kGame, kBrowser, kWorking, kAchievements, kReady };
  struct PickState;

  std::string Tr(std::string_view english) const;
  void StoragePage();
  void SourceBrowserPage();
  void StartCopy(std::vector<platform::CopySource> sources);
  void WaitTestFrames();
  void ApplyStorage(bool portable, bool way_back);
  void GamePage();
  void BrowserPage();
  void WorkingPage();
  void AchievementsPage();
  void ReadyPage();
  void AfterGame();
  void OpenBrowser();
  void Pick(bool folder);
  void PollPick();
  void Chosen(const std::filesystem::path& path, Source source);

  SetupConfig config_;
  SDL_Window* window_ = nullptr;
  wizard::Ui ui_;
  wizard::BrowserPage browser_{true};
  wizard::BrowserPage source_browser_{false};
  std::optional<bool> portable_;
  std::string error_;
  std::shared_ptr<PickState> pick_;

  std::thread worker_;
  std::atomic<bool> cancel_{false}, finished_{false};
  std::atomic<int> phase_{0};
  std::atomic<int> working_frames_{0};  // frames the working page drew (test_install_frames)
  std::atomic<uint64_t> done_{0}, total_{0};
  InstallResult result_;
  bool copying_ = false;  // the work is the copy, not the install
  platform::CopyResult copy_result_ = platform::CopyResult::kDone;
  std::string copy_error_;
  Source source_ = Source::kFolder;
  std::string from_;

  bool quit_ = false;
  bool completed_ = false, installed_ = false, start_game_ = true;
  std::optional<settings::AchievementSet> achievements_;
  std::vector<platform::StartupMessage> log_;
};

}  // namespace torchlight::game_setup
