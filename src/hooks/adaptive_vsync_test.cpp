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

  // No host cap: the game's own choice, whatever the frame rate.
  Check(AdaptiveVsyncInterval(immediate, 25.0f, 0) == immediate, "no cap: the game's immediate");
  Check(AdaptiveVsyncInterval(wait, 45.0f, 0) == wait, "no cap: the game's default");

  // A cap of 30: holding it keeps the vsync (the game alone would drop it under 31).
  Check(AdaptiveVsyncInterval(immediate, 29.9f, 30) == wait, "30 held at 29.9: waits");
  Check(AdaptiveVsyncInterval(immediate, 29.0f, 30) == wait, "30 at 29.0: still waits");
  Check(AdaptiveVsyncInterval(wait, 28.5f, 30) == immediate, "under 29: cannot hold 30");

  // Caps over 32: the game's own threshold of 31 stands.
  Check(AdaptiveVsyncInterval(wait, 30.5f, 40) == immediate, "40 at 30.5: under 31");
  Check(AdaptiveVsyncInterval(immediate, 35.0f, 40) == wait, "40 at 35: over 31");
  Check(AdaptiveVsyncInterval(wait, 30.9f, 60) == immediate, "60 at 30.9: under 31");
  Check(AdaptiveVsyncInterval(immediate, 59.9f, 60) == wait, "60 held: waits");

  if (g_failures) return 1;
  std::puts("adaptive_vsync_test: ok");
  return 0;
}
