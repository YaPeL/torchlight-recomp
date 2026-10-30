// The PGO instrumented build's counters (TORCHLIGHT_PGO_GENERATE, docs/perf/pgo.md), written every
// minute to --pgo_profile_dir as torchlight-<pid>.profraw: the game ends by a hard exit, which
// skips the runtime's own write at exit, and a run can be stopped at any point. Each write holds
// the counts since the start, so the last one of a run is the whole run. Only in the instrumented
// build.

#include <chrono>
#include <string>
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
      const std::string dir = REXCVAR_GET(pgo_profile_dir);
      static const std::string name =
          (dir.empty() ? std::string() : dir + "/") + "torchlight-%p.profraw";
      __llvm_profile_set_filename(name.c_str());
      REXLOG_INFO("pgo: profile counters written every minute to {}", name);
      for (;;) {
        if (__llvm_profile_write_file() != 0) REXLOG_WARN("pgo: the profile write failed");
        std::this_thread::sleep_for(60s);
      }
    }).detach();
  }
} g_writer;

}  // namespace
