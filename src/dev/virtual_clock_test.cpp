// The deterministic runs' clock (virtual_clock.h).

#include <cstdio>
#include <cstdlib>

#include "dev/virtual_clock.h"

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", what);
  }
}
}  // namespace

int main() {
  using torchlight::dev::VirtualClock;
  VirtualClock clock(1000, 100);
  VirtualClock::Reader a, b;
  Check(clock.Read(a) == 1000, "first read: the start");
  Check(clock.Read(a) == 1001 && clock.Read(a) == 1002, "reads within a frame move one tick each");
  Check(clock.Read(b) == 1000, "another thread starts from the frame's time, whatever the first read");
  clock.Advance();
  Check(clock.frame() == 1 && clock.FrameTicks() == 1100, "a frame is one step");
  Check(clock.Read(a) == 1100 && clock.Read(b) == 1100, "a new frame: every thread at the frame's time");
  // More reads in a frame than the step: still monotonic, and the next frame never goes back.
  VirtualClock small(0, 2);
  VirtualClock::Reader r;
  for (int i = 0; i < 5; ++i) small.Read(r);
  small.Advance();
  Check(small.Read(r) == 5, "past the next frame's time: one more than the last read");
  // Two runs with the same reads see the same values.
  VirtualClock one(0, 10), two(0, 10);
  VirtualClock::Reader ra, rb;
  bool same = true;
  for (int f = 0; f < 4; ++f) {
    for (int i = 0; i < f + 1; ++i) same = same && one.Read(ra) == two.Read(rb);
    one.Advance();
    two.Advance();
  }
  Check(same, "the same reads, the same values");
  if (failures) return EXIT_FAILURE;
  std::puts("virtual_clock_test: ok");
  return EXIT_SUCCESS;
}
