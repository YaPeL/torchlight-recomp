// Achievement-unlocked notifications: the IDs that a gameplay event newly unlocked, in order, for
// a presenter on another thread (the game UI toast, toast.cpp). Only runtime Apply pushes, so
// loading saved state, Steam reconciliation and repeated completions never produce one.
#pragma once
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
namespace torchlight::achievements {
class NotificationQueue {
 public:
  void Push(std::string id);
  std::optional<std::string> Pop();
  void Clear();
  size_t Size() const;
 private:
  mutable std::mutex mutex_;
  std::deque<std::string> ids_;
};
// local and steam-dry-run always notify (nothing else will). The real Steam backend ("steam")
// leaves it to the Steam overlay unless `with_steam` asks for ours too. Unknown modes: never.
bool NotificationsEnabled(std::string_view mode, bool with_steam);
} // namespace torchlight::achievements
