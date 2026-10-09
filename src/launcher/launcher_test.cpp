// Tests for the whole launcher (launcher.h) on its real window, with fake services and keys sent
// as SDL events (so they go through ImGui's SDL3 backend as a user's would): the first start from
// the package to Play, a rejected package, Cancel, closing while installing, the fallback browser
// when the picker fails, --launcher's Ready page, and Play waiting for the key to be released.
// Without a display (CI) on SDL's offscreen video driver.

#include <chrono>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_video.h>

#include "game_setup/game_files.h"
#include "launcher/launcher.h"

namespace {

using namespace torchlight::launcher;
namespace game_setup = torchlight::game_setup;
namespace platform = torchlight::platform;
using torchlight::settings::AchievementSet;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

struct Fakes {
  enum class Install { kSucceed, kReject, kUntilCancelled };
  Install install = Install::kSucceed;
  std::vector<std::pair<PickOutcome, std::string>> picks;  // in order; then cancelled
  size_t picks_made = 0;
  std::string installed_from;
  std::optional<AchievementSet> saved;
};

ListFolder FakeTree() {
  static const std::map<std::string, std::vector<FolderEntry>> tree = {
      {"/", {{"games", true, 0}}},
      {"/games", {{"LIVEPKG", false, 1234}}},
  };
  return [](const std::filesystem::path& folder, std::vector<FolderEntry>& entries,
            std::string& error) {
    const auto it = tree.find(folder.generic_string());
    if (it == tree.end()) {
      error = "Permission denied";
      return false;
    }
    entries = it->second;
    return true;
  };
}

LauncherServices Services(Fakes& fakes) {
  LauncherServices services;
  services.pick = [&fakes](game_setup::Source, std::string& path, std::string& error) {
    if (fakes.picks_made >= fakes.picks.size()) return PickOutcome::kCancelled;
    const auto& [outcome, text] = fakes.picks[fakes.picks_made++];
    (outcome == PickOutcome::kFailed ? error : path) = text;
    return outcome;
  };
  services.list_folder = FakeTree();
  services.places = {"/"};
  services.install = [&fakes](game_setup::Source, const std::filesystem::path& from,
                              const game_setup::PhaseProgress& progress) {
    fakes.installed_from = from.generic_string();
    game_setup::InstallResult result;
    switch (fakes.install) {
      case Fakes::Install::kSucceed:
        progress(game_setup::Phase::kCopying, 1, 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        progress(game_setup::Phase::kChecking, 2, 2);
        break;
      case Fakes::Install::kReject:
        result.error = {std::string(game_setup::kTextNotPackage), {}};
        break;
      case Fakes::Install::kUntilCancelled:
        while (progress(game_setup::Phase::kCopying, 1, 4)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        result.error = {std::string(game_setup::kCancelled), {}};
        break;
    }
    return result;
  };
  services.save_achievements = [&fakes](AchievementSet set, std::string&) {
    fakes.saved = set;
    return true;
  };
  return services;
}

// Drives a launcher: keys, frames, and the outcome once there is one.
class Driver {
 public:
  Driver(const Start& start, Fakes& fakes) {
    std::string error;
    window_ = platform::LauncherWindow::Open("launcher_test", error);
    if (!window_) {
      std::fprintf(stderr, "FAIL: the window: %s\n", error.c_str());
      ++failures;
      return;
    }
    launcher_.emplace(*window_, start, Services(fakes), game_setup::Translate{}, "/data/game",
                      log_);
    Frames(2);
  }
  ~Driver() {
    launcher_.reset();
    window_.reset();
  }
  bool ok() const { return launcher_.has_value(); }
  const LauncherModel& model() const { return launcher_->model(); }
  const FileBrowserModel* browser() const { return launcher_->browser(); }
  const std::optional<Outcome>& outcome() const { return outcome_; }

  void Frames(int count) {
    for (int i = 0; i < count && !outcome_; ++i) outcome_ = launcher_->Step();
  }
  // A key pressed and released, then a frame for the focus to settle.
  void Press(SDL_Keycode key, SDL_Scancode scancode) {
    Send(key, scancode, true);
    Frames(1);
    Send(key, scancode, false);
    Frames(2);
  }
  void Enter() { Press(SDLK_RETURN, SDL_SCANCODE_RETURN); }
  void EnterDown() { Send(SDLK_RETURN, SDL_SCANCODE_RETURN, true); }
  void EnterUp() { Send(SDLK_RETURN, SDL_SCANCODE_RETURN, false); }
  void Down() { Press(SDLK_DOWN, SDL_SCANCODE_DOWN); }
  void Escape() { Press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE); }
  void Push(SDL_EventType type) {
    SDL_Event event{};
    event.type = type;
    SDL_PushEvent(&event);
  }
  // Frames until `done` (at most about 5 seconds).
  bool Until(const std::function<bool()>& done) {
    for (int i = 0; i < 300 && !outcome_; ++i) {
      if (done()) return true;
      Frames(1);
    }
    return done();
  }
  bool Logged(const std::string& part) const {
    for (const auto& message : log_) {
      if (message.text.find(part) != std::string::npos) return true;
    }
    return false;
  }

 private:
  void Send(SDL_Keycode key, SDL_Scancode scancode, bool down) {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    const SDL_WindowID id = windows && count > 0 ? SDL_GetWindowID(windows[0]) : 0;
    SDL_free(windows);
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = id;
    event.key.key = key;
    event.key.scancode = scancode;
    event.key.down = down;
    SDL_PushEvent(&event);
  }

  std::unique_ptr<platform::LauncherWindow> window_;
  std::vector<platform::StartupMessage> log_;
  std::optional<Launcher> launcher_;
  std::optional<Outcome> outcome_;
};

void TestFirstStart() {
  Fakes fakes;
  fakes.picks = {{PickOutcome::kChosen, "/games/LIVEPKG"}};
  Driver d({false, std::nullopt, false}, fakes);
  if (!d.ok()) return;
  Check(d.model().page() == Page::kInstall, "first start: Install");
  d.Enter();
  Check(d.Until([&] { return d.model().page() == Page::kAchievements; }),
        "Enter on the first button: the package, installed");
  Check(fakes.installed_from == "/games/LIVEPKG" && d.Logged("installed the game files"),
        "from the chosen package, logged");
  d.Frames(1);
  d.Down();
  d.Enter();
  Check(fakes.saved == AchievementSet::kPc && d.model().page() == Page::kReady,
        "Down and Enter: PC, saved; Ready");
  d.Frames(1);
  d.Enter();
  Check(d.outcome() == Outcome::kPlay, "Enter on Play: play");
}

void TestRejected() {
  Fakes fakes;
  fakes.picks = {{PickOutcome::kChosen, "/games/other"}};
  fakes.install = Fakes::Install::kReject;
  Driver d({false, std::nullopt, false}, fakes);
  if (!d.ok()) return;
  d.Enter();
  Check(d.Until([&] { return d.model().page() == Page::kInstall && !d.model().error().empty(); }),
        "a rejected package: back to Install");
  Check(d.model().error().text == game_setup::kTextNotPackage && d.Logged("failed"),
        "with the reason, logged");
}

void TestCancel() {
  Fakes fakes;
  fakes.picks = {{PickOutcome::kChosen, "/games/LIVEPKG"}};
  fakes.install = Fakes::Install::kUntilCancelled;
  Driver d({false, std::nullopt, false}, fakes);
  if (!d.ok()) return;
  d.Enter();
  Check(d.model().page() == Page::kInstalling, "installing");
  d.Frames(1);
  d.Enter();
  Check(d.Until([&] { return d.model().page() == Page::kInstall; }) && d.model().error().empty() &&
            d.Logged("cancelled"),
        "Enter on Cancel: back to Install, no error");
}

void TestCloseWhileInstalling() {
  Fakes fakes;
  fakes.picks = {{PickOutcome::kChosen, "/games/LIVEPKG"}};
  fakes.install = Fakes::Install::kUntilCancelled;
  Driver d({false, std::nullopt, false}, fakes);
  if (!d.ok()) return;
  d.Enter();
  d.Push(SDL_EVENT_QUIT);
  d.Until([] { return false; });
  Check(d.outcome() == Outcome::kQuit && d.Logged("cancelled"),
        "closing while installing: cancelled, then quit");
}

void TestBrowser() {
  Fakes fakes;
  fakes.picks = {{PickOutcome::kFailed, "no portal"}};
  Driver d({false, std::nullopt, false}, fakes);
  if (!d.ok()) return;
  d.Enter();
  Check(d.model().page() == Page::kBrowse && d.browser() &&
            d.browser()->folder().generic_string() == "/" && !d.model().note().empty(),
        "no picker: the browser at the first place, with the note");
  d.Enter();
  Check(d.browser() && d.browser()->folder().generic_string() == "/games",
        "Enter on a folder opens it");
  d.Escape();
  Check(d.browser() && d.browser()->folder().generic_string() == "/", "Escape: up a folder");
  d.Escape();
  Check(d.model().page() == Page::kInstall && !d.browser(), "Escape at the top: Install");
  d.Enter();
  Check(fakes.picks_made == 1 && d.model().page() == Page::kBrowse && d.browser() &&
            d.browser()->folder().generic_string() == "/",
        "after the failure, straight to the browser, where it was");
  d.Enter();
  d.Down();
  d.Enter();
  Check(d.Until([&] { return d.model().page() == Page::kAchievements; }) &&
            fakes.installed_from == "/games/LIVEPKG",
        "a file chosen in the browser: installed");
}

// Play waits until the key that chose it is released, so it does not reach the game.
void TestPlayWaitsForRelease() {
  Fakes fakes;
  Driver d({true, AchievementSet::kXbox, true}, fakes);
  if (!d.ok()) return;
  d.EnterDown();
  d.Frames(10);
  Check(!d.outcome(), "Enter held on Play: the launcher waits");
  d.EnterUp();
  d.Frames(2);
  Check(d.outcome() == Outcome::kPlay, "released: play");
}

void TestOnDemand() {
  Fakes fakes;
  Driver d({true, AchievementSet::kXbox, true}, fakes);
  if (!d.ok()) return;
  Check(d.model().page() == Page::kReady, "--launcher: Ready");
  d.Down();
  d.Enter();
  Check(d.model().page() == Page::kAchievements, "Change achievements");
  d.Frames(1);
  d.Escape();
  Check(d.model().page() == Page::kReady && !fakes.saved, "Escape: back, nothing saved");
  d.Frames(1);
  d.Down();
  d.Down();
  d.Down();
  d.Enter();
  Check(d.outcome() == Outcome::kQuit, "Quit, the last button");
}

}  // namespace

int main() {
  if (!platform::HasDisplay()) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    std::printf("no display: SDL's offscreen video driver\n");
  }
  TestFirstStart();
  TestRejected();
  TestCancel();
  TestCloseWhileInstalling();
  TestBrowser();
  TestOnDemand();
  TestPlayWaitsForRelease();
  SDL_Quit();
  if (failures) return 1;
  std::printf("launcher test: ok\n");
  return 0;
}
