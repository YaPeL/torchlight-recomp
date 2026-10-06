#include "platform/user_folders.h"

#include <cctype>
#include <cstdlib>
#include <system_error>

namespace torchlight::platform {

// Renames `from` to `to` failing if `to` exists (platform file).
bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error);

namespace {

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

std::vector<StartupMessage> MigrateUserFolders() {
  std::vector<StartupMessage> messages;
  const std::filesystem::path cache = UserFolder(UserFolderKind::kCache);
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

std::filesystem::path UserFolder(UserFolderKind kind) {
  const std::filesystem::path base = UserFolderBase(kind);
  if (base.empty()) return base;
  if (kind == UserFolderKind::kGameData) return base / kUserFolderName / "game";
  return base / kUserFolderName;
}

}  // namespace torchlight::platform
