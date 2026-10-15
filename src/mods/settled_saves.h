// The saves the start's checks (save_units.h RepairModListSaves and ProtectSaves) found settled,
// remembered so the next start does not read them again while nothing they depend on changed
// (docs/mods.md 7d). A save is settled when a check has nothing to do with it: no unknown unit of
// the player's, nothing to repair, or a file that does not read and cannot be repaired (logged
// once, not at every start). A save left alone with unknown units is not settled: it is checked,
// and told to the player, at every start.
//
// What the checks depend on besides the file is the context: the units the game is expected to
// know (the game data and the mods), the names a broken mod list may hold, and the checks' own
// rules (kRulesVersion). A different context forgets every save. A save is unchanged when its size
// and modification time are the ones recorded; one written since (by the game, a repair or a
// removal, or put back from a backup) is read again. No guest, OGRE or platform types.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "mods/save_units.h"

namespace torchlight::mods {

class SettledSaves {
 public:
  // Bumped when a check's rules change, so saves settled under the old ones are read again.
  static constexpr int kRulesVersion = 1;

  // The context of the checks, as a digest (hex).
  static std::string Context(const KnownUnits& known, const std::vector<std::u16string>& repair_names);

  // The saves settled under `context` at the last start, from `file` (none when it is missing,
  // unreadable or of another context). Paths are kept relative to `root`.
  static SettledSaves Load(const std::filesystem::path& file, const std::filesystem::path& root,
                           const std::string& context);
  SettledSaves(std::filesystem::path root, std::string context) : root_(std::move(root)), context_(std::move(context)) {}

  // Settled last time and not changed since (same size and modification time).
  bool Unchanged(const std::filesystem::path& save) const;
  // Records `save` as settled, with its size and modification time now.
  void Settle(const std::filesystem::path& save);
  size_t size() const { return entries_.size(); }

  // Written whole and renamed into place; false when it cannot be written (the next start then
  // checks every save).
  bool Store(const std::filesystem::path& file) const;

 private:
  struct Stamp {
    uintmax_t size = 0;
    int64_t time = 0;
    bool operator==(const Stamp&) const = default;
  };
  static bool StampOf(const std::filesystem::path& path, Stamp& out);
  std::string Key(const std::filesystem::path& save) const;

  std::filesystem::path root_;
  std::string context_;
  std::map<std::string, Stamp> entries_;
};

}  // namespace torchlight::mods
