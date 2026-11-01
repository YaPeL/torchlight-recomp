#include "platform/storage_mode.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "platform/user_folders.h"

namespace torchlight::platform {

namespace fs = std::filesystem;

StorageMode DecideStorage(const StorageFacts& facts) {
  if (facts.test_folders || facts.roots_given) return StorageMode::kUserFolders;
  if (!facts.portable_by_default || facts.portable_folder.empty()) return StorageMode::kUserFolders;
  if (facts.portable_exists) {
    return facts.portable_writable ? StorageMode::kPortable : StorageMode::kPortableUnwritable;
  }
  return facts.user_folders_chosen ? StorageMode::kUserFolders : StorageMode::kAsk;
}

fs::path ApplicationFolder(const fs::path& executable_dir, const fs::path& appimage_path) {
  if (!appimage_path.empty()) return fs::absolute(appimage_path).parent_path();
  if (executable_dir.empty()) return {};
  // Without a trailing separator, so parent_path below walks real folders.
  const fs::path dir = executable_dir.has_filename() ? executable_dir : executable_dir.parent_path();
  const fs::path contents = dir.parent_path();
  const fs::path bundle = contents.parent_path();
  if (dir.filename() == "MacOS" && contents.filename() == "Contents" &&
      bundle.extension() == ".app") {
    return bundle.parent_path();
  }
  return dir;
}

bool CanWriteFolder(const fs::path& folder, std::string& error) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const fs::path probe = folder / (".tl-write-test-" + std::to_string(stamp));
  {
    std::ofstream out(probe, std::ios::binary);
    out << "test";
    out.flush();
    if (!out) {
      error = "cannot create a file in " + folder.string();
      return false;
    }
  }
  std::error_code ec;
  fs::remove(probe, ec);
  return true;
}

StorageFacts ReadStorageFacts(const fs::path& executable_dir, const fs::path& appimage_path,
                              bool roots_given) {
  StorageFacts facts;
  const char* test_folders = std::getenv(kTestUserFoldersVariable);
  facts.test_folders = test_folders && *test_folders;
  facts.roots_given = roots_given;
  facts.portable_by_default = TORCHLIGHT_PORTABLE_DEFAULT != 0;
  if (facts.test_folders || facts.roots_given || !facts.portable_by_default) return facts;
  const fs::path application = ApplicationFolder(executable_dir, appimage_path);
  if (application.empty()) return facts;
  facts.portable_folder = application / kPortableFolderName;
  std::error_code ec;
  facts.portable_exists = fs::is_directory(facts.portable_folder, ec);
  if (facts.portable_exists) {
    std::string ignored;
    facts.portable_writable = CanWriteFolder(facts.portable_folder, ignored);
  }
  const fs::path config = PlainUserFolder(UserFolderKind::kConfig);
  facts.user_folders_chosen = !config.empty() && fs::exists(config / kUserFoldersChoiceFile, ec);
  return facts;
}

}  // namespace torchlight::platform
