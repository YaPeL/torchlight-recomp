// The save safety net: when to back up (including the first time mods appear), the copy of the
// save containers, the backup folder's name and the fingerprint record. Synthetic files only.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "mods/save_safety.h"

namespace fs = std::filesystem;
using namespace torchlight::mods;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}
void Write(const fs::path& path, const std::string& text) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << text;
}
}  // namespace

int main() {
  // When.
  Check(!ShouldBackUpSaves(std::nullopt, ""), "never recorded, no mods: nothing to protect");
  Check(ShouldBackUpSaves(std::nullopt, "a|1|x\n"), "never recorded, mods appear: back up");
  Check(ShouldBackUpSaves(std::string(""), "a|1|x\n"), "no mods before, mods now: back up");
  Check(!ShouldBackUpSaves(std::string("a|1|x\n"), "a|1|x\n"), "same set: no backup");
  Check(ShouldBackUpSaves(std::string("a|1|x\n"), "a|2|x\n"), "priority changed: back up");
  Check(ShouldBackUpSaves(std::string("a|1|x\n"), ""), "mods removed: back up");

  const fs::path root =
      fs::temp_directory_path() / ("tl_save_safety_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::remove_all(root);
  const auto now = std::chrono::sys_days{std::chrono::year{2026} / 10 / 7} + std::chrono::hours{13} +
                   std::chrono::minutes{5} + std::chrono::seconds{9};
  std::string log;
  auto logger = [&log](const std::string& line) { log += line + "\n"; };

  Check(!BackUpSaves(root, "TITLE", now, logger), "no saves: no backup folder");
  Write(root / "PROFILE1" / "TITLE" / "00000001" / "slot.sav", "save one");
  Write(root / "PROFILE2" / "TITLE" / "00000001" / "slot.sav", "save two");
  Write(root / "PROFILE2" / "OTHER" / "file", "not this title");
  const auto backup = BackUpSaves(root, "TITLE", now, logger);
  Check(backup && backup->filename() == "20261007-130509-mods", "name: UTC yyyymmdd-hhmmss-mods");
  if (backup) {
    Check(fs::exists(*backup / "PROFILE1" / "TITLE" / "00000001" / "slot.sav") &&
              fs::exists(*backup / "PROFILE2" / "TITLE" / "00000001" / "slot.sav"),
          "every profile's container copied");
    Check(!fs::exists(*backup / "PROFILE2" / "OTHER"), "other titles left out");
  }
  const auto second = BackUpSaves(root, "TITLE", now, logger);
  Check(second && second->filename() == "20261007-130509-mods-2", "same second: -2");
  Check(second && !fs::exists(*second / "save-backups"), "the backups folder is not copied into itself");

  // Record.
  const fs::path record = root / "state" / "mod_set.txt";
  Check(!ReadModSetRecord(record), "no record yet");
  Check(WriteModSetRecord(record, "a|1|x\n") && ReadModSetRecord(record) == std::string("a|1|x\n"), "record round-trips");
  Check(WriteModSetRecord(record, "") && ReadModSetRecord(record) == std::string(""), "an empty set is recorded too");

  fs::remove_all(root);
  if (failures) return EXIT_FAILURE;
  std::puts("save safety tests passed");
  return EXIT_SUCCESS;
}
