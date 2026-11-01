// With a portable root, every folder platform.h gives is below it: nothing of the player's escapes
// to the user's folders. (The layout of the kinds is storage_mode_test's; this is the platform's
// own functions, one implementation per platform.)

#include <cstdio>
#include <cstdlib>
#include <string>

#include "platform/platform.h"
#include "platform/storage_mode.h"
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

void SetEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

// A run's own roots: every folder below them. The user's folders are only ever read here as names
// (UserFolder creates nothing); only the functions that create folders are called below a root.
void TestRunRoots() {
  const char* variable = kTestUserFoldersVariable;
  const char* previous = std::getenv(variable);
  const std::string saved = previous ? previous : "";
  const fs::path root = fs::temp_directory_path() / "tl_run_roots";
  const fs::path cache_root = fs::temp_directory_path() / "tl_run_roots_cache";

  // Below the tests' folders they change nothing.
  SetEnvironment(variable, (root / "tests").string());
  const fs::path tests_config = UserFolder(UserFolderKind::kConfig);
  SetRunRoots(root, cache_root);
  if (UserFolder(UserFolderKind::kConfig) != tests_config) {
    std::fprintf(stderr, "FAIL: the tests' folders win over a run's roots\n");
    std::exit(1);
  }

  SetEnvironment(variable, "");
  const fs::path real_config = PlainUserFolder(UserFolderKind::kConfig);
  SetRunRoots(root, {});
  Check(ConfigDir(), root / "config", "--user_data_root: settings");
  Check(DataDir(), root, "--user_data_root: the saves are the root itself");
  Check(GameDataDir(), root / "game", "--user_data_root: game files and import");
  Check(LogDir(), root / "logs", "--user_data_root: logs");
  Check(ShaderCacheDir(), root / "cache" / "ogre", "--user_data_root: shader cache");
  Check(CacheDir("layouts"), root / "cache" / "layouts", "--user_data_root: other caches");

  SetRunRoots(root, cache_root);
  Check(ShaderCacheDir(), cache_root / "ogre", "--cache_root wins for the cache");
  Check(ConfigDir(), root / "config", "--cache_root leaves the settings below --user_data_root");
  SetPortableRoot({});

  SetRunRoots({}, cache_root);
  Check(ShaderCacheDir(), cache_root / "ogre", "--cache_root alone: the cache");
  if (UserFolder(UserFolderKind::kConfig) != real_config) {
    std::fprintf(stderr, "FAIL: --cache_root alone moves only the cache\n");
    std::exit(1);
  }
  SetPortableRoot({});
  if (PlainUserFolder(UserFolderKind::kConfig) != real_config) {
    std::fprintf(stderr, "FAIL: the user's own folder does not change\n");
    std::exit(1);
  }

  // The start's own function: with roots, the folders follow them, nothing is migrated.
  std::vector<StartupMessage> log;
  if (SetUpFolders("", "", root, "", "", log) != StorageMode::kUserFolders || log.size() != 1 ||
      log[0].text.find("nothing migrated") == std::string::npos) {
    std::fprintf(stderr, "FAIL: a start with roots keeps to them and migrates nothing\n");
    std::exit(1);
  }
  Check(ConfigDir(), root / "config", "the start: settings below the root");
  Check(LogDir(), root / "logs", "the start: logs below the root");
  SetPortableRoot({});
  log.clear();
  SetUpFolders("", "", "", "", cache_root, log);
  Check(ShaderCacheDir(), cache_root / "ogre", "the start: --cache_root alone");
  SetPortableRoot({});

  SetEnvironment(variable, saved);
  fs::remove_all(root);
  fs::remove_all(cache_root);
}

}  // namespace

int main() {
  TestRunRoots();
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
