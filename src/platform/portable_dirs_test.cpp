// With a portable root, every folder platform.h gives is below it: nothing of the player's escapes
// to the user's folders. (The layout of the kinds is storage_mode_test's; this is the platform's
// own functions, one implementation per platform.)

#include <cstdio>
#include <cstdlib>
#include <string>

#include "platform/platform.h"
#include "platform/user_folders.h"

namespace {

namespace fs = std::filesystem;
using namespace torchlight::platform;

void Check(const std::string& got, const fs::path& want, const char* what) {
  // Lexical, so the trailing separator the functions add does not count.
  if ((fs::path(got) / "").lexically_normal() != (want / "").lexically_normal()) {
    std::fprintf(stderr, "FAIL: %s: %s, not %s\n", what, got.c_str(), want.string().c_str());
    std::exit(1);
  }
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "tl_portable_dirs" / "TorchlightRecomp-data";
  fs::remove_all(root.parent_path());
  SetPortableRoot(root);
  Check(ConfigDir(), root / "config", "settings");
  Check(DataDir(), root / "data", "saves");
  Check(GameDataDir(), root / "game", "game files");
  Check(LogDir(), root / "logs", "logs");
  Check(ShaderCacheDir(), root / "cache" / "ogre", "shader cache");
  Check(CacheDir("layouts"), root / "cache" / "layouts", "other caches");
  SetPortableRoot({});
  fs::remove_all(root.parent_path());
  std::printf("portable_dirs_test: ok\n");
  return 0;
}
