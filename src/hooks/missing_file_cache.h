// The game's files that do not exist, remembered so the guest's existence checks do not ask the
// kernel again for them. When the game creates a monster it tests about ten optional files that
// are not there (sound variants such as ...SPAWN1_2.WAV.CMP), the same ones for every monster of a
// wave and every wave: 210 checks and about 22 ms in each wave of the combat benchmark on the POCO
// X7 (docs/performance-profile.md).
//
// Only the game's own files (the GAME: device, read only during a session): never the user's data
// (SAVE:), the mods' devices, the texture replacements (tlrepl:) or our files (tlhost:), whose
// contents can change while the game runs. A file added to the game's folder while the game runs
// is not seen until the game starts again. Searches with wildcards are not remembered.
//
// The answer kept is the kernel's own status for that path, given back unchanged, so the guest's
// error handling (its last error, from the status) is the same as without the cache. Only "does
// not exist" statuses are kept; any other failure asks the kernel again.
// No guest or SDK dependency: tested on its own (missing_file_cache_test.cpp).

#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace torchlight::hooks {

class MissingFileCache {
 public:
  // The key of a guest path the cache may remember: upper case, '\' separators, on the GAME:
  // device, no wildcard. None for any other path.
  static std::optional<std::string> Key(std::string_view guest_path);
  // Whether a status means the file or its folder does not exist.
  static bool IsNotFound(uint32_t status);

  // The status remembered for `key`, if any (any thread).
  std::optional<uint32_t> Find(const std::string& key) const;
  // Remembers `status` for `key` when IsNotFound(status) (any thread).
  void Remember(const std::string& key, uint32_t status);
  size_t size() const;

 private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, uint32_t> missing_;
};

}  // namespace torchlight::hooks
