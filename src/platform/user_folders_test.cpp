// Tests for MigrateFolder: the old folder moves only when the new one does not exist and the old
// one is ours; nothing is ever replaced or deleted.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "platform/user_folders.h"

namespace {

namespace fs = std::filesystem;
using torchlight::platform::LegacyFolder;
using torchlight::platform::Migration;
using torchlight::platform::MigrateFolder;
using torchlight::platform::UserFolder;
using torchlight::platform::UserFolderKind;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

void Write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream(file) << text;
}

std::string Read(const fs::path& file) {
  std::ifstream in(file);
  std::string text;
  std::getline(in, text);
  return text;
}

struct Scratch {
  fs::path root = fs::temp_directory_path() / "tl_user_folders_test";
  Scratch() {
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~Scratch() { fs::remove_all(root); }
  LegacyFolder Folder(std::vector<std::string> markers = {"settings.toml"}) const {
    return {root / "torchlight", root / "TorchlightRecomp", std::move(markers)};
  }
};

void TestOldOnlyMoves() {
  Scratch s;
  const LegacyFolder folder = s.Folder();
  Write(folder.old_path / "settings.toml", "aspect = \"auto\"");
  Write(folder.old_path / "nested" / "file", "kept");
  const auto result = MigrateFolder(folder);
  Check(result.outcome == Migration::kMoved, "old only: moved");
  Check(!fs::exists(folder.old_path), "old only: the old name is gone");
  Check(Read(folder.new_path / "settings.toml") == "aspect = \"auto\"" &&
            Read(folder.new_path / "nested" / "file") == "kept",
        "old only: everything kept under the new name");
  Check(result.message.find(folder.old_path.string()) != std::string::npos &&
            result.message.find(folder.new_path.string()) != std::string::npos,
        "old only: the log says what moved where");
}

void TestBothExistTouchesNothing() {
  Scratch s;
  const LegacyFolder folder = s.Folder();
  Write(folder.old_path / "settings.toml", "old");
  Write(folder.new_path / "settings.toml", "new");
  const auto result = MigrateFolder(folder);
  Check(result.outcome == Migration::kBothExist && !result.message.empty(), "both: warned");
  Check(Read(folder.old_path / "settings.toml") == "old", "both: old untouched");
  Check(Read(folder.new_path / "settings.toml") == "new", "both: new untouched");
  // An empty new folder is not replaced either.
  fs::remove_all(folder.new_path);
  fs::create_directories(folder.new_path);
  Check(MigrateFolder(folder).outcome == Migration::kBothExist &&
            fs::exists(folder.old_path / "settings.toml") && fs::is_empty(folder.new_path),
        "both (new empty): nothing moved");
}

void TestNoOldDoesNothing() {
  Scratch s;
  const LegacyFolder folder = s.Folder();
  auto result = MigrateFolder(folder);
  Check(result.outcome == Migration::kNothing && result.message.empty(), "neither: nothing");
  Check(!fs::exists(folder.new_path), "neither: no folder created");
  Write(folder.new_path / "settings.toml", "new");
  result = MigrateFolder(folder);
  Check(result.outcome == Migration::kNothing && Read(folder.new_path / "settings.toml") == "new",
        "new only: nothing");
}

void TestNotOursIsLeftAlone() {
  Scratch s;
  const LegacyFolder folder = s.Folder();
  Write(folder.old_path / "someone_elses.ini", "x");
  const auto result = MigrateFolder(folder);
  Check(result.outcome == Migration::kNotOurs, "not ours: left alone");
  Check(fs::exists(folder.old_path / "someone_elses.ini") && !fs::exists(folder.new_path),
        "not ours: untouched, nothing created");
  // A profile folder (an XUID) marks the runtime's data folder.
  const LegacyFolder data = s.Folder({"<16 hex digits>", "cache"});
  fs::create_directories(data.old_path / "B13EBABEBABEBABE" / "58410A7E");
  Check(MigrateFolder(data).outcome == Migration::kMoved &&
            fs::is_directory(data.new_path / "B13EBABEBABEBABE" / "58410A7E"),
        "profile folder: ours, moved");
}

// Sets an environment variable for this process (the test's own; nothing in the project sets it
// this way).
void SetEnvironment(const char* name, const std::string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

void TestTestFoldersVariable() {
  const char* name = torchlight::platform::kTestUserFoldersVariable;
  const char* previous = std::getenv(name);
  const std::string saved = previous ? previous : "";
  const fs::path root = fs::temp_directory_path() / "tl_user_folders_variable";
  SetEnvironment(name, root.string());
  Check(UserFolder(UserFolderKind::kConfig) == root / "config" / "TorchlightRecomp" &&
            UserFolder(UserFolderKind::kState) == root / "state" / "TorchlightRecomp" &&
            UserFolder(UserFolderKind::kCache) == root / "cache" / "TorchlightRecomp" &&
            UserFolder(UserFolderKind::kData) == root / "data" / "TorchlightRecomp" &&
            UserFolder(UserFolderKind::kGameData) ==
                root / "game_data" / "TorchlightRecomp" / "game",
        "the test variable holds every user folder");
  SetEnvironment(name, saved);
}

// A user folder below a name with accents and ñ (a Windows account such as Martín's): resolved,
// created, written and migrated through UTF-8 strings, as the platform code passes them on.
void TestNonAsciiFolders() {
  const char* name = torchlight::platform::kTestUserFoldersVariable;
  const char* previous = std::getenv(name);
  const std::string saved = previous ? previous : "";
  const fs::path root =
      fs::temp_directory_path() / fs::path(u8"tl_user_folders_Martín_ñandú");
  fs::remove_all(root);
  const std::u8string root_u8 = root.u8string();
  SetEnvironment(name, std::string(root_u8.begin(), root_u8.end()));
  const fs::path config = UserFolder(UserFolderKind::kConfig);
  Check(config == root / "config" / "TorchlightRecomp", "a non-ASCII base resolves");
  Write(config / "settings.toml", "language = \"es\"");
  Check(fs::exists(root / "config" / "TorchlightRecomp" / "settings.toml") &&
            Read(config / "settings.toml") == "language = \"es\"",
        "written and read back below it");
  const LegacyFolder folder = {root / "torchlight", root / u8"TorchlightRecomp ñ",
                               {"settings.toml"}};
  Write(folder.old_path / "settings.toml", "kept");
  Check(MigrateFolder(folder).outcome == Migration::kMoved &&
            Read(folder.new_path / "settings.toml") == "kept",
        "migrated below a non-ASCII base");
  SetEnvironment(name, saved);
  fs::remove_all(root);
}

}  // namespace

int main() {
  TestTestFoldersVariable();
  TestNonAsciiFolders();
  TestOldOnlyMoves();
  TestBothExistTouchesNothing();
  TestNoOldDoesNothing();
  TestNotOursIsLeftAlone();
  std::printf("user_folders_test: ok\n");
  return 0;
}
