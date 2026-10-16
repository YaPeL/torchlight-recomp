// FramePacer: presents one period apart when the frames come ready with a varying delay.

#include <chrono>
#include <cstdint>
#include <cstdio>

#include "live/frame_pacer.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

// A fixed pseudo-random sequence (the test does not depend on the platform's generators).
uint32_t g_seed = 12345;
uint32_t Next() {
  g_seed = g_seed * 1664525u + 1013904223u;
  return g_seed >> 8;
}
}  // namespace

int main() {
  using torchlight::live::FramePacer;
  using namespace std::chrono_literals;
  using Clock = FramePacer::Clock;
  const auto start = Clock::time_point{} + 1h;

  {
    FramePacer pacer;
    Check(pacer.Slot(start) == start, "no period: presented when ready");
    Check(pacer.Slot(start + 3ms) == start + 3ms, "no period: still when ready");
  }

  // 60 fps: the guest's vblank every period, each frame ready 1 to 5 ms after it.
  const auto period = std::chrono::duration_cast<Clock::duration>(1s) / 60;
  FramePacer pacer;
  pacer.SetPeriod(period);
  Clock::time_point last{};
  int even = 0, frames = 0, early = 0;
  Clock::duration waited{};
  for (int i = 0; i < 6000; ++i) {
    const auto ready = start + i * period + 1ms + std::chrono::microseconds(Next() % 4000);
    const auto slot = pacer.Slot(ready);
    if (slot < ready) ++early;
    if (i >= 600) {  // after 10 s
      ++frames;
      waited += slot - ready;
      const auto gap = slot - last;
      if (gap > period - 50us && gap < period + 50us) ++even;
    }
    last = slot;
  }
  Check(early == 0, "never presented before the frame is ready");
  std::printf("even %d of %d, mean wait %.2f ms\n", even, frames,
              std::chrono::duration<double, std::milli>(waited).count() / frames);
  Check(even >= frames * 9 / 10, "nine presents in ten one period after the previous one");
  Check(waited / frames < 3ms, "the added latency stays under the frames' spread");

  // One frame 12 ms late (a hitch): the slots move later with it, and within two seconds the wait
  // is back under the frames' spread.
  {
    const auto base = last + period;
    Clock::duration after_hitch{};
    int counted = 0;
    for (int i = 0; i < 240; ++i) {
      auto ready = base + i * period + 1ms + std::chrono::microseconds(Next() % 4000);
      if (i == 0) ready += 12ms;
      const auto slot = pacer.Slot(ready);
      if (i >= 120) {
        after_hitch += slot - ready;
        ++counted;
      }
      last = slot;
    }
    std::printf("mean wait 2-4 s after a hitch %.2f ms\n",
                std::chrono::duration<double, std::milli>(after_hitch).count() / counted);
    Check(after_hitch / counted < 3ms, "the wait comes back after a hitch");
  }

  // A stall (a level load): the next frame is presented when ready and the slots follow it.
  const auto after = last + 2s;
  Check(pacer.Slot(after) == after, "after a stall: presented when ready");
  Check(pacer.Slot(after + 2ms) == after + period, "then one period later");

  // A new cap restarts the slots.
  pacer.SetPeriod(period / 2);
  const auto t = after + 10s;
  Check(pacer.Slot(t) == t, "new period: presented when ready");
  Check(pacer.Slot(t + 1ms) == t + period / 2, "then half the old period later");

  if (g_failures) return 1;
  std::printf("frame_pacer_test: ok\n");
  return 0;
}
