// The log folder's size limits (docs/crash-handling.md, section 5): a run's log rotates at 5 MB and
// keeps at most 50 MB, and the folder stays under 200 MB by removing the oldest runs at startup,
// always keeping the newest crash reports.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace torchlight::live {

struct LogBudget {
  uint64_t folder_bytes = 200ull << 20;
  uint64_t run_bytes = 50ull << 20;  // the rotating sink's files together
  uint64_t file_bytes = 5ull << 20;  // one file of the rotating sink
  size_t crash_reports_kept = 10;
};

// The rotated files a run keeps besides its current one, so that all of them fit run_bytes.
int RotatedFilesPerRun(const LogBudget& budget);

// Removes the oldest runs' files from `dir` until the folder fits folder_bytes with room for one
// more run (run_bytes). A run NNN is <app>_NNN.log, its rotations <app>_NNN.K.log, the OGRE log
// <app>_NNN_ogre.log and the crash report <app>_NNN_crash.txt (and .dmp); other files are neither
// counted nor touched. The crash reports of the newest crash_reports_kept runs that have one stay
// even when the rest of their run goes. Returns how many files were removed.
size_t PruneLogFolder(const std::filesystem::path& dir, const std::string& app,
                      const LogBudget& budget);

}  // namespace torchlight::live
