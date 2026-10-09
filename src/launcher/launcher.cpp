#include "launcher/launcher.h"

#include <utility>

#include "game_setup/game_files.h"
#include "launcher/folder_listing.h"

namespace torchlight::launcher {

namespace {

namespace fs = std::filesystem;
using game_setup::Source;

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}
fs::path FromUtf8(const std::string& text) {
  return fs::path(std::u8string(text.begin(), text.end()));
}

std::string Text(const game_setup::Translate& tr, std::string_view english) {
  return game_setup::Render({std::string(english), {}}, tr);
}

}  // namespace

LauncherServices SystemServices(const fs::path& game_dir, const game_setup::Translate& tr) {
  LauncherServices services;
  services.pick = [tr](Source source, std::string& path, std::string& error) {
    const platform::PickResult picked =
        source == Source::kPackage
            ? platform::PickFile(Text(tr, game_setup::kTextPackagePicker), path, error)
            : platform::PickFolder(Text(tr, game_setup::kTextFolderPicker), path, error);
    switch (picked) {
      case platform::PickResult::kChosen: return PickOutcome::kChosen;
      case platform::PickResult::kCancelled: return PickOutcome::kCancelled;
      case platform::PickResult::kFailed: break;
    }
    return PickOutcome::kFailed;
  };
  services.list_folder = ListFolderOnDisk;
  services.places = platform::BrowsePlaces();
  services.install = [game_dir](Source source, const fs::path& from,
                                const game_setup::PhaseProgress& progress) {
    if (source == Source::kPackage) {
      const auto facts = game_setup::ReadPackageFacts(from);
      game_setup::Message problem = facts ? game_setup::CheckPackage(*facts)
                                          : game_setup::Message{
                                                std::string(game_setup::kTextNotPackage), {}};
      if (!problem.empty()) return game_setup::InstallResult{std::move(problem), {}};
    }
    return game_setup::Install(source, from, game_dir, game_setup::GameFiles(), progress);
  };
  services.save_achievements = [](settings::AchievementSet set, std::string& error) {
    const std::string dir = platform::ConfigDir();
    if (dir.empty()) {
      error = "no config folder";
      return false;
    }
    const std::string path = dir + std::string(settings::kFileName);
    settings::HostSettings host;
    std::vector<std::string> warnings;
    if (!settings::Load(path, host, warnings, error)) return false;
    host.achievements = set;
    return settings::Save(path, host, error);
  };
  return services;
}

Launcher::Launcher(platform::LauncherWindow& window, const Start& start, LauncherServices services,
                   game_setup::Translate tr, std::string game_dir,
                   std::vector<platform::StartupMessage>& log)
    : window_(window),
      model_(start),
      services_(std::move(services)),
      tr_(std::move(tr)),
      game_dir_(std::move(game_dir)),
      log_(log) {}

Launcher::~Launcher() {
  if (worker_.joinable()) {
    cancel_ = true;
    worker_.join();
  }
}

std::optional<Outcome> Launcher::Step() {
  const bool close = window_.NewFrame();
  const ViewResult view = view_.Draw(model_, browser_.get(), tr_, game_dir_, window_.scale());
  window_.Present();
  // After the frame is shown: a picker blocks.
  std::optional<Outcome> outcome;
  if (close) outcome = Do(model_.Close());
  if (!outcome) outcome = Do(PollInstall());
  if (!outcome && view.button) outcome = Do(model_.Press(*view.button));
  if (!outcome && view.chosen) {
    outcome = Do(model_.Picked(PickOutcome::kChosen, Utf8(*view.chosen)));
  }
  return outcome;
}

Action Launcher::PollInstall() {
  if (!worker_.joinable()) return Action::kNone;
  if (!finished_) {
    std::lock_guard lock(mutex_);
    model_.Progress(phase_, done_, total_);
    return Action::kNone;
  }
  worker_.join();
  const std::string from = model_.install_path();
  if (game_setup::Cancelled(result_.error)) {
    log_.push_back({false, "launcher: install from " + from + " cancelled"});
  } else if (!result_.error.empty()) {
    log_.push_back({true, "launcher: install from " + from + " failed: " +
                              game_setup::Render(result_.error)});
  } else {
    if (!result_.moved_to.empty()) {
      log_.push_back({false, "launcher: the previous " + game_dir_ + " was moved to " +
                                 result_.moved_to});
    }
    log_.push_back({false, "launcher: installed the game files from " + from + " to " +
                               game_dir_});
  }
  return model_.Finished(result_);
}

std::optional<Outcome> Launcher::Do(Action action) {
  switch (action) {
    case Action::kNone:
      break;
    case Action::kPickPackage:
    case Action::kPickFolder: {
      std::string path, error;
      const PickOutcome picked = services_.pick(
          action == Action::kPickPackage ? Source::kPackage : Source::kFolder, path, error);
      if (picked == PickOutcome::kFailed) {
        log_.push_back({true, "launcher: no file chooser (" + error + "); the launcher's browser"});
      }
      return Do(model_.Picked(picked, path, error));
    }
    case Action::kBrowse: {
      const fs::path start = browse_folder_.empty() && !services_.places.empty()
                                 ? services_.places.front()
                                 : browse_folder_;
      browser_ = std::make_unique<FileBrowserModel>(
          model_.browse_source() == Source::kPackage ? FileBrowserModel::Mode::kFile
                                                     : FileBrowserModel::Mode::kFolder,
          services_.list_folder, start, services_.places);
      break;
    }
    case Action::kInstall: {
      cancel_ = false;
      finished_ = false;
      phase_ = game_setup::Phase::kCopying;
      done_ = total_ = 0;
      const Source source = model_.install_source();
      const fs::path from = FromUtf8(model_.install_path());
      worker_ = std::thread([this, source, from] {
        result_ = services_.install(source, from,
                                    [this](game_setup::Phase phase, uint64_t done, uint64_t total) {
                                      std::lock_guard lock(mutex_);
                                      phase_ = phase, done_ = done, total_ = total;
                                      return !cancel_.load();
                                    });
        finished_ = true;
      });
      break;
    }
    case Action::kCancelInstall:
      cancel_ = true;
      break;
    case Action::kSaveAchievements: {
      std::string error;
      const settings::AchievementSet set = *model_.achievements();
      if (services_.save_achievements(set, error)) {
        log_.push_back({false, "launcher: achievements " +
                                   std::string(settings::AchievementSetName(set))});
      } else {
        log_.push_back({true, "launcher: cannot save the achievement set: " + error});
      }
      break;
    }
    case Action::kPlay:
      return Outcome::kPlay;
    case Action::kQuit:
      return Outcome::kQuit;
  }
  // The browser lives while its page is shown; the next one opens where this one was.
  if (browser_ && model_.page() != Page::kBrowse) {
    browse_folder_ = browser_->folder();
    browser_.reset();
  }
  return std::nullopt;
}

std::optional<Outcome> RunLauncher(const Start& start, LauncherServices services,
                                   const game_setup::Translate& tr, const std::string& game_dir,
                                   std::vector<platform::StartupMessage>& log,
                                   std::string& error) {
  auto window = platform::LauncherWindow::Open(Text(tr, game_setup::kTextTitle), error);
  if (!window) return std::nullopt;
  std::optional<Outcome> outcome;
  {
    Launcher launcher(*window, start, std::move(services), tr, game_dir, log);
    while (!(outcome = launcher.Step())) {
    }
  }
  return outcome;
}

}  // namespace torchlight::launcher
