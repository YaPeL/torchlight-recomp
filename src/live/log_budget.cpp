#include "live/log_budget.h"

#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

namespace torchlight::live {
namespace {

struct RunFiles {
  std::vector<std::filesystem::path> logs, reports;
  uint64_t bytes = 0;
};

bool AllDigits(std::string_view s) {
  return !s.empty() && s.find_first_not_of("0123456789") == std::string_view::npos;
}

// The run number of one of a run's files, and whether it is a crash report.
std::optional<int> RunOf(std::string_view name, std::string_view prefix, bool& is_report) {
  if (!name.starts_with(prefix)) return std::nullopt;
  name.remove_prefix(prefix.size());
  const size_t digits = name.find_first_not_of("0123456789");
  if (digits == 0 || digits > 9 || digits == std::string_view::npos) return std::nullopt;
  const std::string_view number = name.substr(0, digits);
  const std::string_view rest = name.substr(digits);
  is_report = rest == "_crash.txt" || rest == "_crash.dmp";
  const bool is_log = rest == ".log" || rest == "_ogre.log" ||
                      (rest.starts_with(".") && rest.ends_with(".log") &&
                       AllDigits(rest.substr(1, rest.size() - 5)));
  if (!is_log && !is_report) return std::nullopt;
  return std::stoi(std::string(number));
}

}  // namespace

int RotatedFilesPerRun(const LogBudget& budget) {
  const uint64_t files = budget.file_bytes ? budget.run_bytes / budget.file_bytes : 1;
  return files > 1 ? int(files - 1) : 0;
}

size_t PruneLogFolder(const std::filesystem::path& dir, const std::string& app,
                      const LogBudget& budget) {
  const std::string prefix = app + "_";
  std::map<int, RunFiles> runs;  // oldest first
  uint64_t total = 0;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (!entry.is_regular_file(ec)) continue;
    bool is_report = false;
    const auto run = RunOf(entry.path().filename().string(), prefix, is_report);
    if (!run) continue;
    const uint64_t bytes = entry.file_size(ec);
    if (ec) continue;
    RunFiles& files = runs[*run];
    (is_report ? files.reports : files.logs).push_back(entry.path());
    files.bytes += bytes;
    total += bytes;
  }

  // The runs whose reports stay: the newest ones that have any.
  std::vector<int> kept_reports;
  for (auto it = runs.rbegin(); it != runs.rend() && kept_reports.size() < budget.crash_reports_kept;
       ++it) {
    if (!it->second.reports.empty()) kept_reports.push_back(it->first);
  }
  auto keeps_reports = [&](int run) {
    for (int kept : kept_reports) {
      if (kept == run) return true;
    }
    return false;
  };

  const uint64_t target =
      budget.folder_bytes > budget.run_bytes ? budget.folder_bytes - budget.run_bytes : 0;
  size_t removed = 0;
  auto remove = [&](const std::filesystem::path& file) {
    const uint64_t bytes = std::filesystem::file_size(file, ec);
    if (!ec && std::filesystem::remove(file, ec)) {
      total -= bytes;
      ++removed;
    }
  };
  for (auto& [run, files] : runs) {
    if (total <= target) break;
    for (const auto& file : files.logs) remove(file);
    if (!keeps_reports(run)) {
      for (const auto& file : files.reports) remove(file);
    }
  }
  return removed;
}

}  // namespace torchlight::live
