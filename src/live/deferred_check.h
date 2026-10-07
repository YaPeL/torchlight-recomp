// A check made at most once, when first needed: startup asks for it when the session depends on
// it, else whatever needs it later (the settings menu, for the OpenGL 3.3 probe) does. The check
// runs outside the lock, so a slow one does not hold up a caller that only asks whether it ran.

#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace torchlight::live {

class DeferredCheck {
 public:
  explicit DeferredCheck(std::function<bool()> check) : check_(std::move(check)) {}

  // The result, running the check the first time.
  bool Result() {
    {
      std::lock_guard lock(mutex_);
      if (result_) return *result_;
    }
    const bool result = check_();
    std::lock_guard lock(mutex_);
    if (!result_) result_ = result;
    return *result_;
  }
  // Whether the check has run.
  bool Done() {
    std::lock_guard lock(mutex_);
    return result_.has_value();
  }

 private:
  std::function<bool()> check_;
  std::mutex mutex_;
  std::optional<bool> result_;
};

}  // namespace torchlight::live
