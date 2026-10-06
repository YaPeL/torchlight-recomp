#include "achievements/notifications.h"
namespace torchlight::achievements {
void NotificationQueue::Push(std::string id) {
  std::lock_guard lock(mutex_);
  ids_.push_back(std::move(id));
}
std::optional<std::string> NotificationQueue::Pop() {
  std::lock_guard lock(mutex_);
  if (ids_.empty()) return std::nullopt;
  auto id = std::move(ids_.front());
  ids_.pop_front();
  return id;
}
void NotificationQueue::Clear() { std::lock_guard lock(mutex_); ids_.clear(); }
size_t NotificationQueue::Size() const { std::lock_guard lock(mutex_); return ids_.size(); }
bool NotificationsEnabled(std::string_view mode, bool with_steam) {
  if (mode == "local" || mode == "steam-dry-run") return true;
  return mode == "steam" && with_steam;
}
} // namespace torchlight::achievements
