// Files that must survive a power cut (the achievements' state, the settings, the imported saves):
// written whole under a temporary name, flushed to the disk, then renamed over the old one, so the
// file is either the old one or the new one. No SDL or runtime: the libraries that build on their
// own (achievements) use it too (durable_file.cmake).

#pragma once

#include <filesystem>
#include <string>

namespace torchlight::platform {

// The file's data on the disk itself, not only handed to the drive: Linux fsync; macOS
// fcntl(F_FULLFSYNC), where fsync does not reach the disk (fsync on a volume without it); Windows
// FlushFileBuffers. False with the reason when it cannot. Linux and macOS do not follow a symbolic
// link.
bool FlushFileToDisk(const std::filesystem::path& file, std::string& error);

// Renames `from` over `to` (replacing it) and makes the rename durable: Linux and macOS flush the
// folder after the rename (fsync, F_FULLFSYNC); Windows renames with MOVEFILE_WRITE_THROUGH. False
// with the reason when the rename or its flush fails (`from` is left where it is when the rename
// fails).
bool CommitReplace(const std::filesystem::path& from, const std::filesystem::path& to,
                   std::string& error);

}  // namespace torchlight::platform
