// The copy into a portable folder, on invented folders below the system's temporary folder.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "platform/portable_copy.h"
#include "platform/storage_mode.h"
#include "platform/user_folders.h"

namespace fs = std::filesystem;
using namespace torchlight::platform;

namespace {

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void Write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file, std::ios::binary) << text;
}

std::string Read(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

fs::path Partial(const fs::path& portable) {
  fs::path partial = portable;
  partial += ".partial";
  return partial;
}

void TestCopy(const fs::path& root) {
  const fs::path from = root / "from", portable = root / "app" / kPortableFolderName;
  Write(from / "config" / "settings.toml", "fps");
  Write(from / "data" / "saves" / "a.sav", std::string(3 << 20, 's'));
  Write(from / "game" / "default.xex", "xex");
  fs::create_directories(from / "cache" / "empty");
  fs::create_directories(root / "app");
  Write(root / "outside" / "secret", "not copied");

  const auto sources = PortableFolderSources(from);
  Check(SourcesHoldFiles(sources), "the source holds files");
  Check(LooksLikePortableFolder(from), "a portable folder's layout");
  Check(!LooksLikePortableFolder(root / "outside"), "not a portable folder's layout");
  Write(root / "app-folder" / "data" / "ui" / "strings.txt", "x");
  Check(!LooksLikePortableFolder(root / "app-folder"), "an application's own data/ is not one");

  // Cancelled, then failed: nothing is left, and the source is as it was.
  std::string error;
  const auto cancel = [](uint64_t, uint64_t) { return false; };
  Check(CopyIntoPortable(sources, portable, cancel, error) == CopyResult::kCancelled, "cancel");
  Check(!fs::exists(portable) && !fs::exists(Partial(portable)), "a cancel leaves nothing");

  // An interrupted copy's leftover does not stop or leak into the next.
  Write(Partial(portable) / "stale", "old");
  uint64_t seen = 0, of = 0;
  const auto watch = [&](uint64_t done, uint64_t total) {
    seen = done;
    of = total;
    return true;
  };
  Check(CopyIntoPortable(sources, portable, watch, error) == CopyResult::kDone, "the copy");
  Check(seen == of && of == (3u << 20) + 6, "the progress reaches the total");
  Check(Read(portable / "config" / "settings.toml") == "fps", "settings copied");
  Check(fs::file_size(portable / "data" / "saves" / "a.sav") == 3u << 20, "saves copied whole");
  Check(Read(portable / "game" / "default.xex") == "xex", "the game copied");
  Check(fs::is_directory(portable / "cache" / "empty"), "empty folders copied");
  Check(!fs::exists(portable / "stale"), "a stale partial folder is not mixed in");
  Check(!fs::exists(Partial(portable)), "no partial folder after the rename");
  Check(Read(from / "data" / "saves" / "a.sav").size() == 3u << 20, "the original stays");

  // Never over an existing folder.
  Check(CopyIntoPortable(sources, portable, {}, error) == CopyResult::kFailed, "it exists");
  Check(Read(portable / "config" / "settings.toml") == "fps", "the existing folder stays");
  Check(error.find("exists") != std::string::npos, "says it exists");
}

// What would leave saves behind fails the copy instead.
void TestNothingLost(const fs::path& root) {
  const fs::path from = root / "linked", portable = root / "linked-portable";
  Write(from / "data" / "ok.sav", "x");
  std::error_code link_ec;
  fs::create_symlink(root / "outside", from / "data" / "saves", link_ec);
  std::string error;
  if (!link_ec) {
    Check(CopyIntoPortable(PortableFolderSources(from), portable, {}, error) == CopyResult::kFailed,
          "a link inside fails the copy");
    Check(!fs::exists(portable) && !fs::exists(Partial(portable)), "and leaves nothing");
  }
  std::error_code top_ec;
  fs::create_directories(root / "dangling");
  fs::create_symlink(root / "not-there", root / "dangling" / "data", top_ec);
  if (!top_ec) {
    Check(CopyIntoPortable(PortableFolderSources(root / "dangling"), portable, {}, error) ==
              CopyResult::kFailed,
          "a link to what cannot be reached fails the copy");
  }
  // An empty target, what an attempt left, is no obstacle.
  fs::create_directories(portable);
  fs::remove(from / "data" / "saves", link_ec);
  Check(CopyIntoPortable(PortableFolderSources(from), portable, {}, error) == CopyResult::kDone,
        "over an empty folder");
  Check(fs::exists(portable / "data" / "ok.sav"), "and copied");
}

void CheckOnce(const fs::path& portable, const char* what) {
  Check(Read(portable / "config" / "settings.toml") == "s", what);
  Check(Read(portable / "data" / "a.sav") == "v", what);
  Check(Read(portable / "game" / "default.xex") == "g", what);
  Check(!fs::exists(portable / "logs"), what);
  // Nowhere else: each file once.
  int settings = 0, saves = 0, games = 0, logs = 0;
  for (const auto& entry : fs::recursive_directory_iterator(portable)) {
    const std::string name = entry.path().filename().string();
    settings += name == "settings.toml";
    saves += name == "a.sav";
    games += name == "default.xex";
    logs += name == "torchlight_001.log";
  }
  Check(settings == 1 && saves == 1 && games == 1 && logs == 0, what);
}

// The platforms' layouts, wherever these tests run.
void TestLayouts(const fs::path& root) {
  std::string error;
  {  // macOS: the settings beside the saves, the game below them, cache and logs elsewhere.
    const fs::path support = root / "mac" / "Support", logs = root / "mac" / "Logs";
    Write(support / "settings.toml", "s");
    Write(support / "torchlight.toml", "r");
    Write(support / "a.sav", "v");
    Write(support / "game" / "default.xex", "g");
    Write(logs / "torchlight_001.log", "l");
    const auto sources = UserFolderSources(support, support, root / "mac" / "Caches", support / "game", logs);
    const fs::path portable = root / "mac" / "portable";
    Check(CopyIntoPortable(sources, portable, {}, error) == CopyResult::kDone, error.c_str());
    CheckOnce(portable, "macOS's layout");
    Check(Read(portable / "config" / "torchlight.toml") == "r", "macOS's runtime settings");
  }
  {  // Windows: cache, logs and game below one folder.
    const fs::path local = root / "win" / "Local", roaming = root / "win" / "Roaming";
    Write(roaming / "settings.toml", "s");
    Write(root / "win" / "Saved" / "a.sav", "v");
    Write(local / "game" / "default.xex", "g");
    Write(local / "logs" / "torchlight_001.log", "l");
    Write(local / "ogre" / "shader", "c");
    const auto sources = UserFolderSources(roaming, root / "win" / "Saved", local, local / "game", local / "logs");
    const fs::path portable = root / "win" / "portable";
    Check(CopyIntoPortable(sources, portable, {}, error) == CopyResult::kDone, error.c_str());
    CheckOnce(portable, "Windows' layout");
    Check(Read(portable / "cache" / "ogre" / "shader") == "c", "Windows' cache");
  }
  {  // Only logs are no earlier install.
    const fs::path local = root / "logs-only" / "Local";
    Write(local / "logs" / "torchlight_001.log", "l");
    const auto sources = UserFolderSources(root / "logs-only" / "c", root / "logs-only" / "d", local, local / "game", local / "logs");
    Check(!SourcesHoldFiles(sources), "only logs");
  }
}

void TestUnreadable(const fs::path& root) {
  // Only where permissions stop it (not as root or on Windows).
  const fs::path from = root / "locked-from", portable = root / "locked-portable";
  Write(from / "data" / "ok.sav", "x");
  const fs::path locked = from / "data" / "locked";
  fs::create_directories(locked);
  fs::permissions(locked, fs::perms::none);
  std::error_code probe;
  fs::directory_iterator check(locked, probe);
  std::string error;
  const CopyResult result = CopyIntoPortable(PortableFolderSources(from), portable, {}, error);
  fs::permissions(locked, fs::perms::owner_all);
  if (probe) {
    Check(result == CopyResult::kFailed, "an unreadable folder fails the copy");
    Check(!fs::exists(portable) && !fs::exists(Partial(portable)), "and leaves nothing");
  }
}

void TestUserFolders(const fs::path& root) {
  const fs::path config = root / "user" / "config", data = root / "user" / "data";
  const fs::path game = data / "game";  // below the saves, as on Linux
  const auto sources = UserFolderSources(config, data, root / "user" / "cache", game, root / "user" / "logs");
  Check(!SourcesHoldFiles(sources), "nothing in them yet");
  Write(config / kUserFoldersChoiceFile, "");
  Check(!SourcesHoldFiles(sources), "the choice file alone is no data");
  Write(config / "settings.toml", "s");
  Write(data / "a.sav", "v");
  Write(game / "default.xex", "g");
  Check(SourcesHoldFiles(sources), "now they hold data");
  const fs::path portable = root / "user-app" / kPortableFolderName;
  fs::create_directories(portable.parent_path());
  std::string error;
  Check(CopyIntoPortable(sources, portable, {}, error) == CopyResult::kDone, error.c_str());
  Check(Read(portable / "config" / "settings.toml") == "s", "the settings");
  Check(Read(portable / "data" / "a.sav") == "v", "the saves");
  Check(Read(portable / "game" / "default.xex") == "g", "the game in its own place");
  Check(!fs::exists(portable / "data" / "game"), "and not with the saves");
  Check(!fs::exists(portable / "config" / kUserFoldersChoiceFile), "not the choice file");
  Check(fs::exists(config / "settings.toml") && fs::exists(data / "a.sav"), "the originals stay");
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "tl_portable_copy_test";
  std::error_code ec;
  fs::permissions(root / "locked-from" / "data" / "locked", fs::perms::owner_all, ec);
  fs::remove_all(root);
  fs::create_directories(root);
  TestCopy(root);
  TestNothingLost(root);
  TestLayouts(root);
  TestUnreadable(root);
  TestUserFolders(root);
  fs::remove_all(root);
  std::printf("portable_copy_test: ok\n");
  return 0;
}
