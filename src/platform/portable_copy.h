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
  // When not empty, only these files directly in `from`: a folder this source shares with another
  // (macOS keeps the settings beside the saves).
  std::vector<std::filesystem::path> only;
  // Written to the disk itself before the folder is finished; the cache can be made again.
  bool durable = true;
};

// What copying the user's own folders takes: config, saves, cache and the game's files. Logs stay.
std::vector<CopySource> UserFolderSources();
// The same from given folders, any of them empty: none. They may be nested or the same (macOS: the
// settings beside the saves; Windows: the logs and the game below the cache's folder): each file
// goes once, to its place. `logs` stays out.
std::vector<CopySource> UserFolderSources(const std::filesystem::path& config,
                                          const std::filesystem::path& data,
                                          const std::filesystem::path& cache,
                                          const std::filesystem::path& game,
                                          const std::filesystem::path& logs);
// What copying another portable folder takes: the same, from its layout.
std::vector<CopySource> PortableFolderSources(const std::filesystem::path& portable);
// Whether any of these sources holds a file.
bool SourcesHoldFiles(const std::vector<CopySource>& sources);
// Whether `folder` is a portable folder of this game (its settings, or its game's executable and a
// data/): an application's own folder with a data/ is not, nor the user's folder with a game/.
bool LooksLikePortableFolder(const std::filesystem::path& folder);

// Bytes copied so far, of `total`; returns false to cancel.
using CopyProgress = std::function<bool(uint64_t done, uint64_t total)>;
enum class CopyResult { kDone, kCancelled, kFailed };
// `portable` must not exist yet, or be empty (else kFailed). On a cancel or a failure the .partial
// folder is removed and `portable` is not there. Anything that cannot be copied as it is fails the
// whole copy, rather than a portable folder that lost part of the saves: an unreadable folder, a
// symbolic link, a device. `error` says why.
CopyResult CopyIntoPortable(const std::vector<CopySource>& sources,
                            const std::filesystem::path& portable, const CopyProgress& progress,
                            std::string& error);

}  // namespace torchlight::platform
