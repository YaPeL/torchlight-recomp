// The game's settings files (settings.txt, local_settings.txt): reading, editing and the import
// rules. The port of tools/save_convert/settings.py, which documents the format and the choice of
// keys: the bytes FF FE, then "KEY :value" lines in UTF-16 (big-endian with '\n' on the 360,
// little-endian with "\r\n" on PC); lines that are not changed are written back byte for byte.

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "save_import/save_tree.h"

namespace torchlight::save_import {

inline constexpr std::array<const char*, 2> kSettingsFiles = {"settings.txt", "local_settings.txt"};

class SettingsFile {
 public:
  // Parses a settings file; false with `error` set (naming `what`) if it is not one.
  static bool Parse(std::span<const uint8_t> data, std::string_view what, SettingsFile& out,
                    std::string& error);
  // An empty file the way the 360 writes them.
  static SettingsFile New360();

  Bytes ToBytes() const;
  std::optional<std::string> Get(std::string_view key) const;
  void Set(std::string_view key, std::string_view value);
  bool big_endian() const { return big_endian_; }
  const std::vector<std::string>& lines() const { return lines_; }  // ASCII, without '\n'

 private:
  bool big_endian_ = true;
  std::vector<std::string> lines_;
};

struct SettingsChange {
  std::string key;
  std::optional<std::string> old_value;  // none: the key was not in the recomp file
  std::string new_value;
};

struct SettingsPlan {
  std::vector<SettingsChange> changes;
  std::vector<SettingsChange> skipped;  // preferences already set differently in the recomp
  std::vector<std::string> problems;
};

// The changes for the recomp's settings file `name` (one of kSettingsFiles), taking each key from
// the first PC file that has it. `recomp` is null if the recomp file does not exist yet. Without
// one every imported key is taken; with one, differing preferences only with `force`, and the
// progress flag when the PC value is higher.
SettingsPlan PlanSettingsImport(std::string_view name, const std::vector<SettingsFile>& pc,
                                const SettingsFile* recomp, bool force);

}  // namespace torchlight::save_import
