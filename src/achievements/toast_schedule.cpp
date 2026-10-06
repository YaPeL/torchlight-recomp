#include "achievements/toast_schedule.h"
namespace torchlight::achievements {
ToastSchedule::Step ToastSchedule::Update(double now, bool covered,
    const std::function<std::optional<std::string>()>& next) {
  if (showing_) {
    if (covered) {  // keep current_: it is shown again, in full, once uncovered
      showing_ = false; hidden_at_ = now;
      return {Action::kHide, *current_};
    }
    if (now - shown_at_ < visible_) return {};
    showing_ = false; hidden_at_ = now;
    Step step{Action::kHide, *current_};
    current_.reset();
    return step;
  }
  if (covered || (hidden_at_ && now - *hidden_at_ < gap_)) return {};
  if (!current_) current_ = next();
  if (!current_) return {};
  showing_ = true; shown_at_ = now;
  return {Action::kShow, *current_};
}
} // namespace torchlight::achievements
