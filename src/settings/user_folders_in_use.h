// The folders the player's own files and the files generated for them are kept in this run: the
// platform's (platform.h DataDir, CacheDir) unless the runtime was given its own roots
// (--user_data_root, --cache_root), which then hold everything, so a run with its own roots never
// reads or writes the player's mods, caches or achievement progress. Without them nothing moves:
// the runtime's default user data root is the platform's data folder already
// (live/install.cpp ConfigurePaths). No platform or runtime types.

#pragma once

#include <filesystem>

namespace torchlight::settings {

struct UserRoots {
  std::filesystem::path platform_data;   // platform::DataDir() (empty: none)
  std::filesystem::path platform_cache;  // the platform's cache folder (platform::CacheDir's parent)
  std::filesystem::path user_data_root;  // the runtime's
  std::filesystem::path cache_root;      // the runtime's
  bool user_data_root_given = false;     // --user_data_root (command line or config)
  bool cache_root_given = false;         // --cache_root
};

struct FoldersInUse {
  std::filesystem::path data;          // mods/, mod_set.txt, replacements/, achievements/
  std::filesystem::path cache;         // unitdata/ (the runtime's cache: <data>/cache by default)
  std::filesystem::path generated;     // atlases/, prompts/ (the platform's cache by default)
  std::filesystem::path achievements;  // <data>/achievements/
};
// Empty paths where there is no folder (no platform data folder and no root given).
FoldersInUse ResolveFoldersInUse(const UserRoots& roots);

}  // namespace torchlight::settings
