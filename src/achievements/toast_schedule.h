// When the unlock toast shows and hides: one notification at a time, for a fixed time, with a short
// gap between two. While a full-screen game layout (loading screen) covers the UI nothing shows;
// a toast it interrupts is shown again in full afterwards. No guest access: toast.cpp feeds it once
// per game UI update and applies the steps.
#pragma once
#include <functional>
#include <optional>
#include <string>
namespace torchlight::achievements {
class ToastSchedule {
 public:
  enum class Action { kNone, kShow, kHide };
  struct Step { Action action = Action::kNone; std::string id; };
  explicit ToastSchedule(double visible_seconds = 5.0, double gap_seconds = 0.5)
      : visible_(visible_seconds), gap_(gap_seconds) {}
  // `next` is asked for a new ID only when one can be shown right away.
  Step Update(double now_seconds, bool covered,
              const std::function<std::optional<std::string>()>& next);
  bool Showing() const { return showing_; }
 private:
  double visible_, gap_;
  std::optional<std::string> current_;
  bool showing_ = false;
  double shown_at_ = 0;
  std::optional<double> hidden_at_;
};
} // namespace torchlight::achievements
