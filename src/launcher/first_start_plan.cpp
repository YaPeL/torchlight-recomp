#include "launcher/first_start.h"

namespace torchlight::launcher {

StartPlan PlanStart(const StartFacts& facts) {
  StartPlan plan;
  if (facts.custom_game_dir && !facts.files.empty()) {
    plan.step = StartStep::kStop;
    return plan;
  }
  plan.start.game_installed = !facts.game_dir_known || facts.files.empty();
  plan.start.achievements =
      facts.settings_read ? facts.achievements : settings::AchievementSet::kXbox;
  plan.start.on_demand = facts.launcher_flag;
  plan.start.can_install = facts.game_dir_known && !facts.custom_game_dir;
  plan.step = Needed(plan.start) ? StartStep::kLauncher : StartStep::kGame;
  return plan;
}

}  // namespace torchlight::launcher
