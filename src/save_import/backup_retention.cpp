#include "save_import/backup_retention.h"

#include <algorithm>
#include <utility>

namespace torchlight::save_import {
namespace {

bool Digits(const std::string& text, size_t from, size_t count, int& value) {
  value = 0;
  for (size_t i = from; i < from + count; ++i) {
    if (i >= text.size() || text[i] < '0' || text[i] > '9') return false;
    value = value * 10 + (text[i] - '0');
  }
  return true;
}

}  // namespace

std::optional<std::chrono::system_clock::time_point> BackupTime(const std::string& name) {
  // yyyymmdd-hhmmss-<package>: 15 characters, a dash, and a package name.
  int year, month, day, hour, minute, second;
  if (name.size() < 17 || name[8] != '-' || name[15] != '-' || !Digits(name, 0, 4, year) ||
      !Digits(name, 4, 2, month) || !Digits(name, 6, 2, day) || !Digits(name, 9, 2, hour) ||
      !Digits(name, 11, 2, minute) || !Digits(name, 13, 2, second)) {
    return std::nullopt;
  }
  using namespace std::chrono;
  const year_month_day date{std::chrono::year(year), std::chrono::month(unsigned(month)),
                            std::chrono::day(unsigned(day))};
  if (!date.ok() || hour > 23 || minute > 59 || second > 59) return std::nullopt;
  return sys_days(date) + hours(hour) + minutes(minute) + seconds(second);
}

std::vector<std::string> BackupsToRemove(const std::vector<std::string>& names,
                                         std::chrono::system_clock::time_point now) {
  std::vector<std::pair<std::chrono::system_clock::time_point, std::string>> backups;
  for (const auto& name : names) {
    if (auto time = BackupTime(name)) backups.emplace_back(*time, name);
  }
  // Newest first; the same second sorts by name ("x-2" after "x", so it counts as newer).
  std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
    return a.first != b.first ? a.first > b.first : a.second > b.second;
  });
  std::vector<std::string> remove;
  for (size_t i = kKeptNewestBackups; i < backups.size(); ++i) {
    if (now - backups[i].first > kKeptBackupAge) remove.push_back(backups[i].second);
  }
  return remove;
}

std::vector<std::string> PruneSaveBackups(const std::filesystem::path& user_data_root,
                                          std::chrono::system_clock::time_point now,
                                          const std::function<void(const std::string&)>& log) {
  namespace fs = std::filesystem;
  const fs::path folder = user_data_root / "save-backups";
  std::error_code ec;
  if (user_data_root.empty() || !fs::is_directory(folder, ec)) return {};
  std::vector<std::string> names;
  for (const auto& entry : fs::directory_iterator(folder, ec)) {
    std::error_code type_ec;
    if (entry.is_directory(type_ec) && !entry.is_symlink(type_ec)) {
      const std::u8string name = entry.path().filename().u8string();
      names.emplace_back(name.begin(), name.end());
    }
  }
  std::vector<std::string> removed;
  for (const auto& name : BackupsToRemove(names, now)) {
    std::error_code remove_ec;
    fs::remove_all(folder / fs::path(std::u8string(name.begin(), name.end())), remove_ec);
    if (remove_ec) {
      log("could not remove the old save backup " + name + ": " + remove_ec.message());
    } else {
      log("removed the old save backup " + name);
      removed.push_back(name);
    }
  }
  return removed;
}

}  // namespace torchlight::save_import
