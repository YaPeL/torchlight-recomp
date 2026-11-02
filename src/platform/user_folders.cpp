#include "platform/user_folders.h"

#include <cctype>
#include <cstdlib>
#include <system_error>

namespace torchlight::platform {

// Renames `from` to `to` failing if `to` exists (platform file).
bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error);

namespace {

// The folders this run keeps in place of the user's, by kind; empty: the user's.
std::filesystem::path& FolderOverride(UserFolderKind kind) {
  static std::filesystem::path folders[5];
  return folders[static_cast<int>(kind)];
}

bool IsXuid(const std::string& name) {
  if (name.size() != 16) return false;
  for (unsigned char c : name) {
    if (!std::isxdigit(c)) return false;
  }
  return true;
}

bool HasMarker(const LegacyFolder& folder) {
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(folder.old_path, ec)) {
    const std::string name = entry.path().filename().string();
    for (const std::string& marker : folder.markers) {
      if (marker == "<16 hex digits>" ? IsXuid(name) : name == marker) return true;
    }
  }
  return false;
}

}  // namespace

MigrationResult MigrateFolder(const LegacyFolder& folder) {
  std::error_code ec;
  const std::string from = folder.old_path.string(), to = folder.new_path.string();
  if (!std::filesystem::is_directory(folder.old_path, ec)) return {};
  if (std::filesystem::exists(folder.new_path, ec) || ec) {
    return {Migration::kBothExist,
            "user folders: both " + from + " and " + to + " exist; nothing moved (the old one is " +
                "no longer used)"};
  }
  if (!HasMarker(folder)) {
    return {Migration::kNotOurs,
            "user folders: " + from + " has nothing of this project's; left alone"};
  }
  std::string error;
  if (!RenameNoReplace(folder.old_path, folder.new_path, error)) {
    return {Migration::kFailed,
            "user folders: cannot move " + from + " to " + to + " (" + error + "); nothing moved"};
  }
  return {Migration::kMoved, "user folders: moved " + from + " to " + to};
}

std::vector<LegacyFolder> LegacyUserFolders() {
  struct Kind {
    UserFolderKind kind;
    std::vector<std::string> markers;
  };
  const Kind kinds[] = {
      {UserFolderKind::kConfig, {"settings.toml", "torchlight.toml"}},
      {UserFolderKind::kState, {"logs"}},
      {UserFolderKind::kCache, {"ogre", "layouts"}},
      {UserFolderKind::kData, {"<16 hex digits>", "cache", "save-backups"}},
  };
  std::vector<LegacyFolder> folders;
  for (const Kind& k : kinds) {
    const std::filesystem::path base = UserFolderBase(k.kind);
    if (base.empty()) continue;
    folders.push_back({base / kLegacyUserFolderName, base / kUserFolderName, k.markers});
  }
  return folders;
}

std::vector<StartupMessage> MigrateUserFolders(bool run_has_own_roots) {
  std::vector<StartupMessage> messages;
  if (run_has_own_roots) {
    messages.push_back({false, "user folders: this run has its own roots; nothing migrated"});
    return messages;
  }
  const std::filesystem::path cache = PlainUserFolder(UserFolderKind::kCache);
  for (const LegacyFolder& folder : LegacyUserFolders()) {
    MigrationResult result = MigrateFolder(folder);
    if (result.message.empty()) continue;
    const bool warning = result.outcome == Migration::kFailed ||
                         (result.outcome == Migration::kBothExist && folder.new_path != cache);
    messages.push_back({warning, std::move(result.message)});
  }
  return messages;
}

std::filesystem::path UserFolderBase(UserFolderKind kind) {
  if (const char* root = std::getenv(kTestUserFoldersVariable); root && *root) {
    switch (kind) {
      case UserFolderKind::kConfig: return std::filesystem::path(root) / "config";
      case UserFolderKind::kState: return std::filesystem::path(root) / "state";
      case UserFolderKind::kCache: return std::filesystem::path(root) / "cache";
      case UserFolderKind::kData: return std::filesystem::path(root) / "data";
      case UserFolderKind::kGameData: return std::filesystem::path(root) / "game_data";
    }
  }
  return PlatformUserFolderBase(kind);
}

void SetPortableRoot(const std::filesystem::path& root) {
  const bool set = !root.empty();
  FolderOverride(UserFolderKind::kConfig) = set ? root / "config" : std::filesystem::path();
  FolderOverride(UserFolderKind::kState) = set ? root / "logs" : std::filesystem::path();
  FolderOverride(UserFolderKind::kCache) = set ? root / "cache" : std::filesystem::path();
  FolderOverride(UserFolderKind::kData) = set ? root / "data" : std::filesystem::path();
  FolderOverride(UserFolderKind::kGameData) = set ? root / "game" : std::filesystem::path();
}

void SetRunRoots(const std::filesystem::path& user_data_root,
                 const std::filesystem::path& cache_root) {
  if (const char* test = std::getenv(kTestUserFoldersVariable); test && *test) return;
  if (!user_data_root.empty()) {
    SetPortableRoot(user_data_root);
    FolderOverride(UserFolderKind::kData) = user_data_root;
  }
  if (!cache_root.empty()) FolderOverride(UserFolderKind::kCache) = cache_root;
}

std::filesystem::path LogsSubfolder() {
  return FolderOverride(UserFolderKind::kState).empty() ? std::filesystem::path("logs")
                                                        : std::filesystem::path();
}

std::filesystem::path UserFolder(UserFolderKind kind) {
  if (const std::filesystem::path& folder = FolderOverride(kind); !folder.empty()) return folder;
  return PlainUserFolder(kind);
}

std::filesystem::path PlainUserFolder(UserFolderKind kind) {
  const std::filesystem::path base = UserFolderBase(kind);
  if (base.empty()) return base;
  if (kind == UserFolderKind::kGameData) return base / kUserFolderName / "game";
  return base / kUserFolderName;
}

}  // namespace torchlight::platform
