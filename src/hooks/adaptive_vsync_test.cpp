// AdaptiveVsyncInterval: the game's adaptive vsync under the host's fps cap.

#include <cstdio>

#include "guest_abi/adaptive_vsync.h"
#include "hooks/adaptive_vsync.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
}  // namespace

int main() {
  using torchlight::hooks::AdaptiveVsyncInterval;
  namespace abi = torchlight::guest_abi::adaptive_vsync;
  const uint32_t immediate = abi::kIntervalImmediate, wait = abi::kIntervalDefault;

  // No host cap: the game's own choice.
  Check(AdaptiveVsyncInterval(immediate, 0) == immediate, "no cap: the game's immediate");
  Check(AdaptiveVsyncInterval(wait, 0) == wait, "no cap: the game's default");
  // A cap of 31 or less: always waits, whatever the game chose from its lastFPS (about 30 when it
  // holds the cap, lower after a hitch, 0 before OGRE's first second).
  Check(AdaptiveVsyncInterval(immediate, 30) == wait, "30: waits");
  Check(AdaptiveVsyncInterval(wait, 30) == wait, "30: still waits");
  Check(AdaptiveVsyncInterval(immediate, 31) == wait, "31: waits");
  // Over 31 (40, 60, 120): the game's threshold of 31 stands.
  Check(AdaptiveVsyncInterval(immediate, 32) == immediate, "32: the game's immediate");
  Check(AdaptiveVsyncInterval(immediate, 40) == immediate, "40: the game's immediate");
  Check(AdaptiveVsyncInterval(wait, 60) == wait, "60: the game's default");

  if (g_failures) return 1;
  std::puts("adaptive_vsync_test: ok");
  return 0;
}
