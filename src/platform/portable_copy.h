// Copying the player's files into a new portable folder (storage_mode.h). Everything goes to
// <portable>.partial first and is renamed once whole, so there is never half a portable folder. The
// sources are only read: nothing of them is moved or deleted. No SDL or runtime, tested on its own.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace torchlight::platform {

struct CopySource {
  std::filesystem::path from;  // a folder; one that does not exist is skipped
  std::filesystem::path to;    // below the portable folder
  // Below `from`, left out (another source's, or not for the portable folder).
  std::vector<std::filesystem::path> leave_out;
};

// What copying the user's own folders takes: config, saves, cache and the game's files. Logs stay.
std::vector<CopySource> UserFolderSources();
// The same from given folders, any of them empty: none.
std::vector<CopySource> UserFolderSources(const std::filesystem::path& config,
                                          const std::filesystem::path& data,
                                          const std::filesystem::path& cache,
                                          const std::filesystem::path& game);
// What copying another portable folder takes: the same, from its layout.
std::vector<CopySource> PortableFolderSources(const std::filesystem::path& portable);
// Whether any of these sources holds a file.
bool SourcesHoldFiles(const std::vector<CopySource>& sources);
// Whether `folder` has a portable folder's layout (one of config/, data/, game/).
bool LooksLikePortableFolder(const std::filesystem::path& folder);

// Bytes copied so far, of `total`; returns false to cancel.
using CopyProgress = std::function<bool(uint64_t done, uint64_t total)>;
enum class CopyResult { kDone, kCancelled, kFailed };
// `portable` must not exist yet (kFailed). On a cancel or a failure the .partial folder is removed
// and `portable` was never there. Symbolic links are not followed or copied. `error` says why.
CopyResult CopyIntoPortable(const std::vector<CopySource>& sources,
                            const std::filesystem::path& portable, const CopyProgress& progress,
                            std::string& error);

}  // namespace torchlight::platform
