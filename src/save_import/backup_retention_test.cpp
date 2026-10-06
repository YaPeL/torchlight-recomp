// Tests for the save backup retention: the name format, the rule (remove only when both outside
// the newest ten and older than 30 days) and the folder pass, with a fixed "now".

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "save_import/backup_retention.h"

namespace {

using namespace torchlight::save_import;
using namespace std::chrono;
namespace fs = std::filesystem;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

const system_clock::time_point kNow = sys_days(year(2026) / October / 6) + hours(12);

// A backup name made `days_ago` days (and `seconds_ago` seconds) before kNow.
std::string Name(int days_ago, int seconds_ago = 0, const std::string& suffix = "") {
  const auto time = floor<seconds>(kNow - days(days_ago) - seconds(seconds_ago));
  const auto day_point = floor<days>(time);
  const year_month_day date{day_point};
  const hh_mm_ss clock{time - day_point};
  char text[32];
  std::snprintf(text, sizeof(text), "%04d%02u%02u-%02ld%02ld%02ld", int(date.year()),
                unsigned(date.month()), unsigned(date.day()), long(clock.hours().count()),
                long(clock.minutes().count()), long(clock.seconds().count()));
  return std::string(text) + "-torchlight.sav" + suffix;
}

std::vector<std::string> Sorted(std::vector<std::string> v) {
  std::sort(v.begin(), v.end());
  return v;
}

void TestNames() {
  Check(BackupTime("20261005-024717-torchlight.sav") ==
            sys_days(year(2026) / October / 5) + hours(2) + minutes(47) + seconds(17),
        "parses the UTC time");
  Check(BackupTime("20261005-024717-torchlight.sav-2").has_value(), "suffix -N");
  for (const char* bad : {"20261332-024717-torchlight.sav", "20261005-254717-torchlight.sav",
                          "2026100-024717-torchlight.sav", "20261005_024717-torchlight.sav",
                          "20261005-024717-", "notes", "20261005-02471x-torchlight.sav"}) {
    Check(!BackupTime(bad), std::string("not a backup: ") + bad);
  }
}

void TestRule() {
  // Twelve old ones: the two oldest go.
  std::vector<std::string> old;
  for (int i = 0; i < 12; ++i) old.push_back(Name(40 + i));
  Check(Sorted(BackupsToRemove(old, kNow)) == Sorted({Name(50), Name(51)}), "12 old: the 2 oldest");

  // Fifteen recent ones: none, even past ten.
  std::vector<std::string> recent;
  for (int i = 0; i < 15; ++i) recent.push_back(Name(i));
  Check(BackupsToRemove(recent, kNow).empty(), "15 recent: none");

  // Eight recent and six old: the newest ten are the eight and two old; the other four old go.
  std::vector<std::string> mixed = recent;
  mixed.resize(8);
  for (int i = 0; i < 6; ++i) mixed.push_back(Name(100 + i));
  Check(Sorted(BackupsToRemove(mixed, kNow)) == Sorted({Name(102), Name(103), Name(104), Name(105)}),
        "mixed: only old ones outside the newest ten");

  // Old ones among the newest ten stay.
  std::vector<std::string> few;
  for (int i = 0; i < 5; ++i) few.push_back(Name(400 + i));
  Check(BackupsToRemove(few, kNow).empty(), "fewer than ten: none");

  // Exactly 30 days is not older than 30 days.
  std::vector<std::string> edge;
  for (int i = 0; i < 10; ++i) edge.push_back(Name(i));
  edge.push_back(Name(30));
  edge.push_back(Name(30, 1));
  Check(BackupsToRemove(edge, kNow) == std::vector<std::string>{Name(30, 1)}, "30 days kept, a second more goes");

  // Same second: the -2 one is the newer one, so it takes the tenth place.
  std::vector<std::string> same;
  for (int i = 0; i < 9; ++i) same.push_back(Name(i));
  same.push_back(Name(60));
  same.push_back(Name(60, 0, "-2"));
  Check(BackupsToRemove(same, kNow) == std::vector<std::string>{Name(60)}, "-2 counts as newer");

  // Names that are not backups never count, never go.
  std::vector<std::string> others = old;
  others.push_back("my-notes");
  others.push_back("torchlight.sav");
  Check(Sorted(BackupsToRemove(others, kNow)) == Sorted({Name(50), Name(51)}), "other names ignored");
}

void TestFolder() {
  std::random_device random;
  const fs::path root = fs::temp_directory_path() / ("backup_retention_test_" + std::to_string(random()));
  const fs::path backups = root / "save-backups";
  fs::create_directories(backups);
  for (int i = 0; i < 12; ++i) {
    fs::create_directories(backups / Name(40 + i) / "torchlight.sav");
    std::ofstream(backups / Name(40 + i) / "torchlight.sav" / "0.TSV") << "save";
  }
  fs::create_directories(backups / "keep-me");
  std::ofstream(backups / (Name(90) + ".zip")) << "a file with a backup's name";

  std::vector<std::string> log;
  const auto removed = PruneSaveBackups(root, kNow, [&](const std::string& line) { log.push_back(line); });
  Check(Sorted(removed) == Sorted({Name(50), Name(51)}), "removed the two oldest");
  Check(!fs::exists(backups / Name(51)) && fs::exists(backups / Name(49) / "torchlight.sav" / "0.TSV"),
        "on disk");
  Check(fs::exists(backups / "keep-me") && fs::exists(backups / (Name(90) + ".zip")), "others untouched");
  Check(log.size() == 2 && log[0].find("removed the old save backup") == 0, "logged");

  // Nothing to do without the folder or the root.
  Check(PruneSaveBackups(root / "missing", kNow, [](const std::string&) {}).empty(), "no folder");
  Check(PruneSaveBackups({}, kNow, [](const std::string&) {}).empty(), "no root");
  fs::remove_all(root);
}

}  // namespace

int main() {
  TestNames();
  TestRule();
  TestFolder();
  std::puts("save_import backup_retention: ok");
  return 0;
}
