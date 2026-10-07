// The save safety net for mods (docs/mods.md, section 7b): the game does not record which mods a
// save was made with, and a save holding a mod's item may not survive the mod's removal. So, at
// startup and before the guest runs, the set of mods in effect (mod_list.h ModSetFingerprint) is
// compared with the one recorded at the previous start; when it differs, including the first time
// mods appear, the save containers are copied to <user_data_root>/save-backups/<UTC>-mods[-N]/,
// a name the runtime's backup retention keeps (save_import/backup_retention.h: yyyymmdd-hhmmss-
// <name>, the newest ten and the last 30 days stay).

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace torchlight::mods {

// Whether to back the saves up: `previous` is the recorded fingerprint (none when never recorded),
// `current` the fingerprint now ("" = no mods).
bool ShouldBackUpSaves(const std::optional<std::string>& previous, const std::string& current);

struct SaveBackup {
  enum class Result { kDone, kNothingToCopy, kFailed } result = Result::kNothingToCopy;
  std::filesystem::path folder;  // kDone: the backup
};

// Copies every <user_data_root>/<profile>/<title_folder>/ to a new
// <user_data_root>/save-backups/<UTC yyyymmdd-hhmmss>-mods[-N]/<profile>/<title_folder>/; `log` gets
// a line for the outcome.
SaveBackup BackUpSaves(const std::filesystem::path& user_data_root, const std::string& title_folder,
                       std::chrono::system_clock::time_point now,
                       const std::function<void(const std::string&)>& log);

// The recorded fingerprint (none when the file is missing) and its update.
std::optional<std::string> ReadModSetRecord(const std::filesystem::path& file);
bool WriteModSetRecord(const std::filesystem::path& file, const std::string& fingerprint);

}  // namespace torchlight::mods
