// PruneLogFolder and RotatedFilesPerRun: the log folder's limits, on a temporary folder.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "live/log_budget.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

namespace fs = std::filesystem;

void Write(const fs::path& file, size_t bytes) { std::ofstream(file) << std::string(bytes, 'x'); }

struct Folder {
  fs::path dir;
  explicit Folder(const char* name) : dir(fs::temp_directory_path() / name) {
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  ~Folder() { fs::remove_all(dir); }
  bool Has(const char* file) const { return fs::exists(dir / file); }
};
}  // namespace

int main() {
  using torchlight::live::LogBudget;
  using torchlight::live::PruneLogFolder;
  using torchlight::live::RotatedFilesPerRun;

  Check(RotatedFilesPerRun(LogBudget{}) == 9, "default: 9 rotated files of 5 MB, 50 MB a run");

  // Folder 300 bytes, room for a 100-byte run: the runs must fit 200.
  LogBudget budget{.folder_bytes = 300, .run_bytes = 100, .file_bytes = 50, .crash_reports_kept = 1};
  {
    Folder f("torchlight_log_budget_fits");
    Write(f.dir / "tl_001.log", 100);
    Write(f.dir / "tl_002.log", 100);
    Check(PruneLogFolder(f.dir, "tl", budget) == 0, "fits: nothing removed");
    Check(f.Has("tl_001.log") && f.Has("tl_002.log"), "fits: both runs stay");
  }
  {
    Folder f("torchlight_log_budget_oldest");
    Write(f.dir / "tl_001.log", 60);
    Write(f.dir / "tl_001.1.log", 60);
    Write(f.dir / "tl_001_ogre.log", 10);
    Write(f.dir / "tl_002.log", 100);
    Write(f.dir / "tl_010.log", 90);
    Write(f.dir / "notes.txt", 1000);
    Write(f.dir / "tl_abc.log", 1000);
    Check(PruneLogFolder(f.dir, "tl", budget) == 3, "over: the oldest run's three files go");
    Check(!f.Has("tl_001.log") && !f.Has("tl_001.1.log") && !f.Has("tl_001_ogre.log"),
          "over: run 1 removed with its rotation and OGRE log");
    Check(f.Has("tl_002.log") && f.Has("tl_010.log"), "over: runs 2 and 10 stay (numeric order)");
    Check(f.Has("notes.txt") && f.Has("tl_abc.log"), "other files are not touched or counted");
  }
  {
    Folder f("torchlight_log_budget_reports");
    Write(f.dir / "tl_001.log", 150);
    Write(f.dir / "tl_001_crash.txt", 5);
    Write(f.dir / "tl_002.log", 150);
    Write(f.dir / "tl_002_crash.txt", 5);
    Write(f.dir / "tl_002_crash.dmp", 5);
    Write(f.dir / "tl_003.log", 150);
    PruneLogFolder(f.dir, "tl", budget);
    Check(!f.Has("tl_001.log") && !f.Has("tl_002.log") && f.Has("tl_003.log"),
          "reports: the two oldest logs go");
    Check(!f.Has("tl_001_crash.txt"), "reports: an older report beyond the kept count goes");
    Check(f.Has("tl_002_crash.txt") && f.Has("tl_002_crash.dmp"),
          "reports: the newest run's report stays without its log");
  }
  Check(PruneLogFolder(fs::temp_directory_path() / "torchlight_log_budget_missing", "tl", budget) == 0,
        "a missing folder: nothing to do");

  if (g_failures == 0) std::printf("log_budget_test: all passed\n");
  return g_failures == 0 ? 0 : 1;
}
