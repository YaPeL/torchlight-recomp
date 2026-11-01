// Tests for the storage decision and the portable folder's layout. Everything below a temporary
// folder: no test touches a real user folder.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "platform/storage_mode.h"
#include "platform/user_folders.h"

namespace {

namespace fs = std::filesystem;
using namespace torchlight::platform;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

StorageFacts Portable() {
  StorageFacts facts;
  facts.portable_by_default = true;
  facts.portable_folder = "app/TorchlightRecomp-data";
  return facts;
}

void TestDecisionOrder() {
  StorageFacts f = Portable();
  Check(DecideStorage(f) == StorageMode::kAsk, "nothing decided: ask");

  f.user_folders_chosen = true;
  Check(DecideStorage(f) == StorageMode::kUserFolders, "the user's folders chosen before");

  f.portable_exists = f.portable_writable = true;
  Check(DecideStorage(f) == StorageMode::kPortable, "the portable folder beats an old choice");

  f.portable_writable = false;
  Check(DecideStorage(f) == StorageMode::kPortableUnwritable,
        "an unwritable portable folder never falls back in silence");

  f.roots_given = true;
  Check(DecideStorage(f) == StorageMode::kUserFolders, "a run with its own roots is left alone");

  f = Portable();
  f.portable_exists = f.portable_writable = true;
  f.test_folders = true;
  Check(DecideStorage(f) == StorageMode::kUserFolders, "the tests' folders win");

  f = Portable();
  f.portable_exists = f.portable_writable = true;
  f.portable_by_default = false;
  Check(DecideStorage(f) == StorageMode::kUserFolders, "a build without the default never moves");

  f = Portable();
  f.portable_folder.clear();
  Check(DecideStorage(f) == StorageMode::kUserFolders, "no place for a portable folder (Android)");
}

void TestApplicationFolder() {
  const fs::path base = fs::temp_directory_path() / "tl_app";
  Check(ApplicationFolder(base / "bin" / "", "") == base / "bin", "the executable's folder");
  Check(ApplicationFolder(base / "bin", "") == base / "bin", "with no trailing separator too");
  Check(ApplicationFolder(base / "bin" / "", base / "games" / "Torchlight.AppImage") ==
            base / "games",
        "beside the AppImage, not its temporary mount");
  Check(ApplicationFolder(base / "Torchlight.app" / "Contents" / "MacOS" / "", "") == base,
        "beside the .app, never inside it");
  Check(ApplicationFolder(base / "Other" / "Contents" / "MacOS" / "", "") ==
            base / "Other" / "Contents" / "MacOS",
        "a folder of those names that is no bundle is a plain folder");
  Check(ApplicationFolder("", "").empty(), "unknown stays unknown (Android)");
}

void TestCanWriteFolder() {
  const fs::path dir = fs::temp_directory_path() / "tl_storage_write";
  fs::remove_all(dir);
  fs::create_directories(dir);
  std::string error;
  Check(CanWriteFolder(dir, error), "a writable folder");
  Check(fs::is_empty(dir), "the test file is removed");
  Check(!CanWriteFolder(dir / "missing", error) && !error.empty(), "a missing folder");

  // Read-only by its permission bits. Root ignores them and Windows' read-only attribute does not
  // apply to folders (platform_win_test denies it through an ACL instead): checked only where a
  // plain write really fails.
  fs::permissions(dir, fs::perms::owner_read | fs::perms::owner_exec);
  const bool really_denied = !std::ofstream(dir / "really").good();
  std::error_code ec;
  fs::remove(dir / "really", ec);
  if (really_denied) Check(!CanWriteFolder(dir, error), "a read-only folder");
  fs::permissions(dir, fs::perms::owner_all);
  fs::remove_all(dir);
}

void TestPortableLayout() {
  const fs::path before = UserFolder(UserFolderKind::kConfig);
  const fs::path root = fs::temp_directory_path() / "tl_app" / kPortableFolderName;
  SetPortableRoot(root);
  Check(UserFolder(UserFolderKind::kConfig) == root / "config" &&
            UserFolder(UserFolderKind::kState) == root / "logs" &&
            UserFolder(UserFolderKind::kCache) == root / "cache" &&
            UserFolder(UserFolderKind::kData) == root / "data" &&
            UserFolder(UserFolderKind::kGameData) == root / "game",
        "every kind below the portable folder");
  Check(LogsSubfolder().empty(), "the logs are logs/ itself, not logs/logs");
  Check(PlainUserFolder(UserFolderKind::kConfig) == before,
        "the user's own folder stays reachable, to copy old data from");
  for (const LegacyFolder& folder : LegacyUserFolders()) {
    Check(folder.new_path.string().find(kPortableFolderName) == std::string::npos,
          "the old names are looked for among the user's folders only");
  }
  SetPortableRoot({});
  Check(UserFolder(UserFolderKind::kConfig) == before, "back to the user's folders");
  Check(LogsSubfolder() == "logs", "logs/ below the user's state folder");
}

}  // namespace

int main() {
  TestDecisionOrder();
  TestApplicationFolder();
  TestCanWriteFolder();
  TestPortableLayout();
  std::printf("storage_mode_test: ok\n");
  return 0;
}
