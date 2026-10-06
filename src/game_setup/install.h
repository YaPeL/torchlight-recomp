// Installing the game's files from the user's package or an extracted folder (REL.4).
//
// Everything goes to <game_dir>.partial first; once it holds every file of the table with its size
// and SHA-256 it is renamed to <game_dir>, so there is never half an install. An existing
// <game_dir> is never replaced or deleted: it is renamed aside (<game_dir>.old-<date>) first.

#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>

#include "game_setup/game_files.h"

namespace torchlight::game_setup {

// The facts of a package's header; nothing when the file cannot be read as one.
std::optional<PackageFacts> ReadPackageFacts(const std::filesystem::path& package);

enum class Source { kPackage, kFolder };
// What the progress is at: copying or extracting, then checking.
enum class Phase { kCopying, kChecking };
using PhaseProgress = std::function<bool(Phase phase, uint64_t done, uint64_t total)>;

struct InstallResult {
  Message error;         // empty on success; Cancelled() when cancelled
  std::string moved_to;  // where an existing install was moved, if any
};
// The install's English texts, for the translation check (setup_text_test).
inline constexpr std::string_view kTextCannotReadFolder = "Cannot read the folder {path}: {error}";
inline constexpr std::string_view kTextCannotCopy = "Cannot copy {path}.";
inline constexpr std::string_view kTextCannotReadPackage = "Cannot read the package {path}.";
inline constexpr std::string_view kTextCannotReadEntry = "Cannot read {file} from the package.";
inline constexpr std::string_view kTextCannotWrite = "Cannot write {path}.";
inline constexpr std::string_view kTextCannotCreate = "Cannot create {path}: {error}";
inline constexpr std::string_view kTextCannotMoveAside =
    "Cannot move the previous {path} aside: {error}";
inline constexpr std::string_view kTextCannotFinish =
    "Cannot finish the install in {path}: {error}";

InstallResult Install(Source source, const std::filesystem::path& from,
                      const std::filesystem::path& game_dir, std::span<const GameFile> files,
                      const PhaseProgress& progress);

}  // namespace torchlight::game_setup
