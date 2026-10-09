// Tests for the start's decision (first_start.h, PlanStart) over made-up facts, without the game or
// a window: the first start, files but no achievement set, everything there with and without
// --launcher, a half install, --game_data_root (checked, never installed into), no folder for the
// game's files, and a settings.toml that cannot be read.

#include <cstdio>
#include <string>

#include "game_setup/game_files.h"
#include "launcher/first_start.h"

namespace {

using namespace torchlight::launcher;
using torchlight::game_setup::Message;
using torchlight::settings::AchievementSet;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

const Message kMissing{std::string(torchlight::game_setup::kTextMissing), {{"file", "pak.zip"}}};

StartFacts Whole() {
  StartFacts facts;
  facts.achievements = AchievementSet::kXbox;
  return facts;
}

void TestOurFolder() {
  StartFacts first;
  first.files = kMissing;
  StartPlan plan = PlanStart(first);
  Check(plan.step == StartStep::kLauncher && !plan.start.game_installed &&
            !plan.start.achievements && plan.start.can_install,
        "first start: the launcher, to install");

  StartFacts no_set = Whole();
  no_set.achievements.reset();
  plan = PlanStart(no_set);
  Check(plan.step == StartStep::kLauncher && plan.start.game_installed && !plan.start.achievements,
        "the files but no achievement set: the launcher, for the set");

  plan = PlanStart(Whole());
  Check(plan.step == StartStep::kGame, "everything there: the game, no window");

  StartFacts on_demand = Whole();
  on_demand.launcher_flag = true;
  plan = PlanStart(on_demand);
  Check(plan.step == StartStep::kLauncher && plan.start.on_demand && plan.start.game_installed &&
            plan.start.can_install,
        "--launcher: the launcher with nothing missing");

  StartFacts half = Whole();
  half.files = {std::string(torchlight::game_setup::kTextOtherSize), {{"file", "pak.zip"}}};
  plan = PlanStart(half);
  Check(plan.step == StartStep::kLauncher && !plan.start.game_installed,
        "a file missing or of another size: not installed, the launcher");
}

void TestCustomFolder() {
  StartFacts custom = Whole();
  custom.custom_game_dir = true;
  StartPlan plan = PlanStart(custom);
  Check(plan.step == StartStep::kGame, "--game_data_root, whole: the game");
  custom.launcher_flag = true;
  plan = PlanStart(custom);
  Check(plan.step == StartStep::kLauncher && !plan.start.can_install,
        "--game_data_root with --launcher: no install offered");
  custom.files = kMissing;
  Check(PlanStart(custom).step == StartStep::kStop, "--game_data_root, not whole: stop");
}

void TestNoFolder() {
  StartFacts none = Whole();
  none.game_dir_known = false;
  none.files = kMissing;
  StartPlan plan = PlanStart(none);
  Check(plan.step == StartStep::kGame, "no folder for the game's files: nothing to install");
  none.achievements.reset();
  plan = PlanStart(none);
  Check(plan.step == StartStep::kLauncher && plan.start.game_installed &&
            !plan.start.can_install,
        "and the set still asked, without an install");
}

void TestUnreadableSettings() {
  StartFacts facts = Whole();
  facts.achievements.reset();
  facts.settings_read = false;
  const StartPlan plan = PlanStart(facts);
  Check(plan.step == StartStep::kGame && plan.start.achievements == AchievementSet::kXbox,
        "settings.toml unreadable: the set not asked, Xbox 360");
}

}  // namespace

int main() {
  TestOurFolder();
  TestCustomFolder();
  TestNoFolder();
  TestUnreadableSettings();
  if (failures) return 1;
  std::printf("first start test: ok\n");
  return 0;
}
