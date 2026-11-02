// The PGO instrumented build's counters (TORCHLIGHT_PGO_GENERATE, docs/perf/pgo.md), written every
// minute to --pgo_profile_dir as torchlight-<start time>.profraw (one file per run, on every
// platform): the game ends by a hard exit, which
// skips the runtime's own write at exit, and a run can be stopped at any point. Each write holds
// the counts since the start, so the last one of a run is the whole run. The runtime appends to
// its file when it does not merge, so every write goes to a truncated temporary file that then
// replaces the profile: a run killed in the middle of a write keeps the previous one. Only in the
// instrumented build.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(pgo_profile_dir, "", "Torchlight",
                      "PGO instrumented builds: the folder the profile counters are written to "
                      "every minute (empty: the working folder)");

extern "C" {
int __llvm_profile_write_file(void);
void __llvm_profile_set_filename(const char* name);
}

namespace {

struct Writer {
  Writer() {
    std::thread([] {
      using namespace std::chrono_literals;
      std::this_thread::sleep_for(60s);  // the command line is parsed by then
      const std::filesystem::path dir = REXCVAR_GET(pgo_profile_dir);
      std::error_code error;
      if (!dir.empty()) std::filesystem::create_directories(dir, error);
      const auto start = std::chrono::system_clock::now().time_since_epoch();
      const std::filesystem::path name =
          dir / ("torchlight-" +
                 std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(start).count()) +
                 ".profraw");
      static const std::string temporary = name.string() + ".tmp";
      __llvm_profile_set_filename(temporary.c_str());
      REXLOG_INFO("pgo: profile counters written every minute to {}", name.string());
      for (;;) {
        std::ofstream(temporary, std::ios::trunc);
        if (__llvm_profile_write_file() != 0) {
          REXLOG_WARN("pgo: the profile write failed");
        } else {
          std::filesystem::rename(temporary, name, error);
          if (error) REXLOG_WARN("pgo: the profile could not be renamed: {}", error.message());
        }
        std::this_thread::sleep_for(60s);
      }
    }).detach();
  }
} g_writer;

}  // namespace
