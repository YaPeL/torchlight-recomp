// A development preview of the launcher (not installed): the real window, view and pickers, with
// an install that only pretends (about 6 seconds of progress, nothing read or written) and an
// achievement set that is not saved. English texts.
//
//   launcher_preview            the first start (Install)
//   launcher_preview --ready    as with --launcher: Ready
//   launcher_preview --browser  the system's picker "fails": the launcher's own browser

#include <chrono>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "game_setup/game_files.h"
#include "launcher/launcher.h"

namespace {

using namespace torchlight::launcher;
namespace game_setup = torchlight::game_setup;
namespace platform = torchlight::platform;

game_setup::InstallResult PretendInstall(const game_setup::PhaseProgress& progress) {
  constexpr uint64_t kTotal = 100;
  for (uint64_t done = 0; done <= kTotal; ++done) {
    if (!progress(game_setup::Phase::kCopying, done, kTotal)) {
      return {{std::string(game_setup::kCancelled), {}}, {}};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  for (uint64_t done = 0; done <= kTotal; done += 5) {
    if (!progress(game_setup::Phase::kChecking, done, kTotal)) {
      return {{std::string(game_setup::kCancelled), {}}, {}};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  bool ready = false, browser = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--ready") {
      ready = true;
    } else if (arg == "--browser") {
      browser = true;
    } else {
      std::fprintf(stderr, "usage: launcher_preview [--ready] [--browser]\n");
      return 2;
    }
  }
  const std::string game_dir = platform::GameDataDir();
  LauncherServices services = SystemServices(game_dir, {});
  services.install = [](game_setup::Source, const std::filesystem::path&,
                        const game_setup::PhaseProgress& progress) {
    return PretendInstall(progress);
  };
  services.save_achievements = [](torchlight::settings::AchievementSet, std::string&) {
    return true;
  };
  if (browser) {
    services.pick = [](game_setup::Source, std::string&, std::string& error) {
      error = "preview";
      return PickOutcome::kFailed;
    };
  }
  Start start;
  if (ready) start = {true, torchlight::settings::AchievementSet::kXbox, true};
  std::vector<platform::StartupMessage> log;
  std::string error;
  const auto outcome = RunLauncher(start, std::move(services), {}, game_dir, log, error);
  for (const auto& message : log) {
    std::printf("%s%s\n", message.warning ? "warning: " : "", message.text.c_str());
  }
  if (!outcome) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }
  std::printf("outcome: %s\n", *outcome == Outcome::kPlay ? "play" : "quit");
  return 0;
}
