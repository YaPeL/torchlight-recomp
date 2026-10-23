// MissingFileCache (missing_file_cache.h): which paths it may remember and what it gives back.
#include <cstdio>
#include <optional>
#include <string>

#include "hooks/missing_file_cache.h"

using torchlight::hooks::MissingFileCache;

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}
}  // namespace

int main() {
  // Normalization: case does not matter; '/' and '\\' stay different (the native search splits
  // only at '\\', so the two spellings are different searches).
  Check(MissingFileCache::Key("game:\\Media\\Sounds\\a.wav.cmp") ==
            std::optional<std::string>("GAME:\\MEDIA\\SOUNDS\\A.WAV.CMP"),
        "lower case normalized");
  Check(MissingFileCache::Key("GAME:\\MEDIA\\SOUNDS\\A.WAV.CMP") ==
            MissingFileCache::Key("Game:\\media\\sounds\\a.WAV.cmp"),
        "the same file in another case gives one key");
  Check(MissingFileCache::Key("game:\\media\\sounds/a.wav") !=
            MissingFileCache::Key("game:\\media\\sounds\\a.wav"),
        "'/' and '\\' give different keys");
  // Only the game's device.
  Check(!MissingFileCache::Key("SAVE:\\backup.tmp"), "the user's data");
  Check(!MissingFileCache::Key("tlmod0:\\media\\a.dds"), "a mod's device");
  Check(!MissingFileCache::Key("tlrepl:\\media\\a.dds"), "the replacements");
  Check(!MissingFileCache::Key("tlhost:\\ui\\a.layout"), "our files");
  Check(!MissingFileCache::Key("gamex:\\a"), "a device that only starts like it");
  Check(!MissingFileCache::Key("GAME:"), "the device alone");
  Check(!MissingFileCache::Key("MEDIA\\A.DDS"), "no device");
  // No wildcard search.
  Check(!MissingFileCache::Key("game:\\media\\*.*"), "a listing");
  Check(!MissingFileCache::Key("game:\\media\\a?.dds"), "a pattern");

  // Only "does not exist" statuses are kept, and given back unchanged.
  MissingFileCache cache;
  const std::string key = *MissingFileCache::Key("game:\\media\\a.wav.cmp");
  Check(!cache.Find(key), "empty at first");
  cache.Remember(key, 0xC0000022);  // STATUS_ACCESS_DENIED: ask again next time
  Check(!cache.Find(key), "another failure is not remembered");
  cache.Remember(key, 0xC0000034);
  Check(cache.Find(key) == std::optional<uint32_t>(0xC0000034), "the kernel's status given back");
  for (uint32_t status : {0xC000000Fu, 0xC000003Au, 0x80000006u}) {
    MissingFileCache c;
    c.Remember(key, status);
    Check(c.Find(key) == std::optional<uint32_t>(status), "each not-found status");
  }
  cache.Remember(key, 0xC000003A);
  Check(cache.Find(key) == std::optional<uint32_t>(0xC0000034), "the first answer stays");
  Check(cache.size() == 1, "one entry per path");

  if (failures == 0) std::printf("missing_file_cache_test: ok\n");
  return failures == 0 ? 0 : 1;
}
