// User folders on Windows: known folders, and a rename that never replaces.
//
// Configuration roams (FOLDERID_RoamingAppData); cache, state (logs) and the installed game files
// (about 200 MB, game\) stay on the machine (FOLDERID_LocalAppData); the runtime's user data (saves,
// save-backups, profiles) goes to Saved Games (FOLDERID_SavedGames), not to Documents, which
// OneDrive often syncs and the game's renames on every save would race with.

#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

#include "platform/user_folders.h"

namespace torchlight::platform {

std::filesystem::path PlatformUserFolderBase(UserFolderKind kind) {
  // kCache, kState and kGameData: the machine's own folder.
  REFKNOWNFOLDERID folder = kind == UserFolderKind::kConfig  ? FOLDERID_RoamingAppData
                            : kind == UserFolderKind::kData ? FOLDERID_SavedGames
                                                            : FOLDERID_LocalAppData;
  PWSTR path = nullptr;
  std::filesystem::path base;
  if (SUCCEEDED(SHGetKnownFolderPath(folder, KF_FLAG_DEFAULT, nullptr, &path))) base = path;
  CoTaskMemFree(path);
  return base;
}

bool RenameNoReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                     std::string& error) {
  // Without MOVEFILE_REPLACE_EXISTING the move fails when `to` exists; without
  // MOVEFILE_COPY_ALLOWED it fails across volumes instead of copying.
  if (MoveFileExW(from.c_str(), to.c_str(), 0)) return true;
  const DWORD code = GetLastError();
  char* text = nullptr;
  FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                     FORMAT_MESSAGE_IGNORE_INSERTS,
                 nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
  error = text ? text : "error " + std::to_string(code);
  LocalFree(text);
  while (!error.empty() && (error.back() == '\n' || error.back() == '\r' || error.back() == '.')) {
    error.pop_back();
  }
  return false;
}

}  // namespace torchlight::platform
