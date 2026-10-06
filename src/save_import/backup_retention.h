// Retention of the save backups the SDK makes before deleting a save container (patch 12:
// <user_data_root>/save-backups/<UTC yyyymmdd-hhmmss>-<package>[-N]/). At start-up, before the guest
// runs, a backup is removed only when it is both outside the newest kKeptNewestBackups and older
// than kKeptBackupAge (so the newest ten always stay, and nothing from the last 30 days goes).
// Only folders with exactly that name are considered; anything else in save-backups/ is left alone.

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace torchlight::save_import {

inline constexpr size_t kKeptNewestBackups = 10;
inline constexpr std::chrono::days kKeptBackupAge{30};

// The time a backup folder name states (UTC), or none if the name is not a backup's.
std::optional<std::chrono::system_clock::time_point> BackupTime(const std::string& name);

// Which of these folder names to remove at `now`.
std::vector<std::string> BackupsToRemove(const std::vector<std::string>& names,
                                         std::chrono::system_clock::time_point now);

// Removes them from <user_data_root>/save-backups/; `log` gets a line per removal or failure.
// Failures do not stop anything. Returns the names removed.
std::vector<std::string> PruneSaveBackups(const std::filesystem::path& user_data_root,
                                          std::chrono::system_clock::time_point now,
                                          const std::function<void(const std::string&)>& log);

}  // namespace torchlight::save_import
