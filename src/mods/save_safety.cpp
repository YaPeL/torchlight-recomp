#include "mods/save_safety.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace torchlight::mods {

namespace fs = std::filesystem;

bool ShouldBackUpSaves(const std::optional<std::string>& previous, const std::string& current) {
  if (!previous) return !current.empty();  // first record: back up when mods appear
  return *previous != current;
}

SaveBackup BackUpSaves(const fs::path& user_data_root, const std::string& title_folder,
                       std::chrono::system_clock::time_point now,
                       const std::function<void(const std::string&)>& log, const std::string& reason) {
  std::error_code ec;
  std::vector<fs::path> containers;  // <profile>/<title_folder>
  if (!user_data_root.empty() && fs::is_directory(user_data_root, ec)) {
    for (const auto& profile : fs::directory_iterator(user_data_root, ec)) {
      std::error_code type_ec;
      if (!profile.is_directory(type_ec) || profile.path().filename() == "save-backups") continue;
      const fs::path title = profile.path() / title_folder;
      if (fs::is_directory(title, type_ec)) containers.push_back(title);
    }
  }
  if (containers.empty()) {
    if (log) log(reason + ": no saves to back up");
    return {};
  }
  using namespace std::chrono;
  const auto day = floor<days>(now);
  const year_month_day date{day};
  const hh_mm_ss time{floor<seconds>(now - day)};
  char stamp[32];
  std::snprintf(stamp, sizeof(stamp), "%04d%02u%02u-%02d%02d%02d-", int(date.year()), unsigned(date.month()),
                unsigned(date.day()), int(time.hours().count()), int(time.minutes().count()),
                int(time.seconds().count()));
  const std::string name = std::string(stamp) + reason;
  const fs::path backups = user_data_root / "save-backups";
  fs::path target = backups / name;
  for (int n = 2; fs::exists(target, ec); ++n) target = backups / (name + "-" + std::to_string(n));
  for (const fs::path& container : containers) {
    const fs::path destination = target / container.parent_path().filename() / title_folder;
    fs::create_directories(destination, ec);
    if (!ec) fs::copy(container, destination, fs::copy_options::recursive, ec);
    if (ec) {
      if (log) log(reason + ": save backup failed (" + container.string() + "): " + ec.message());
      return {SaveBackup::Result::kFailed, {}};
    }
  }
  if (log) log(reason + ": saves backed up to " + target.string());
  return {SaveBackup::Result::kDone, target};
}

std::optional<std::string> ReadModSetRecord(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return std::nullopt;
  return std::string(std::istreambuf_iterator<char>(in), {});
}

bool WriteModSetRecord(const fs::path& file, const std::string& fingerprint) {
  std::error_code ec;
  fs::create_directories(file.parent_path(), ec);
  const fs::path temp = file.string() + ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << fingerprint;
    if (!out) return false;
  }
  fs::rename(temp, file, ec);
  return !ec;
}

}  // namespace torchlight::mods
