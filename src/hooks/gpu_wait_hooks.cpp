// The guest's wait for the GPU's progress, sleeping instead of spinning.
//
// Xbox D3D waits for the GPU (in the device swap, among others) by calling one wait step in a loop
// until the GPU's progress word passes a target (guest_abi functions::kGpuProgressWaitStep). The
// step's own pause is a few db16cyc, no-ops once recompiled, so the guest's main thread spun on a
// core for as long as the GPU took (the vblank, with vsync). The callers run the step only while
// the target is not reached yet (0x821A5C10 checks its condition first, @0x821A5C44 and
// @0x821A5CB0), so the override sleeps briefly on every step, then runs the original step, which
// checks as it always did: every decision (keep waiting, stop, the 5000-unit hang watchdog, which
// goes by the clock) stays the guest's, and a wait ends at most kWaitStepSleep after its target.
// (Sleeping only while the progress word had not moved, the first version, never slept on Linux:
// there the word changes between consecutive steps while the target is still ahead.)
//
// Every step also goes to the wait watch (gpu_wait_watch.h): a wait longer than 10 s is logged
// ("guest GPU wait: ... without reaching its target"), with the GPU's progress word, and the wait
// goes on.

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/logging.h>
#include <rex/thread.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/xbox_d3d.h"
#include "hooks/gpu_wait_watch.h"
#include "platform/platform.h"

namespace {

namespace fn = torchlight::guest_abi::functions;
namespace d3d = torchlight::guest_abi::xbox_d3d;
using torchlight::hooks::GpuWaitWatch;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

// Short against a frame (8.3 ms at 120 Hz) and long enough that the waiting thread stays asleep
// (rex::thread::Sleep waits on a high resolution timer on Windows, patch 17; nanosleep on POSIX).
constexpr std::chrono::microseconds kWaitStepSleep{200};

// How long the guest waits for the GPU here, logged every kWaitLogPeriod while it waits at all
// ("guest GPU waits:"), for the measurements of where the guest's thread goes
// (docs/performance-profile.md).
constexpr std::chrono::seconds kWaitLogPeriod{10};
std::atomic<uint64_t> g_wait_steps{0}, g_wait_ns{0};
std::atomic<int64_t> g_wait_log_ns{0};

void CountWait(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
  g_wait_steps.fetch_add(1, std::memory_order_relaxed);
  g_wait_ns.fetch_add(uint64_t(std::chrono::nanoseconds(end - start).count()),
                      std::memory_order_relaxed);
  const int64_t now = std::chrono::nanoseconds(end.time_since_epoch()).count();
  int64_t last = g_wait_log_ns.load(std::memory_order_relaxed);
  if (last == 0) {
    g_wait_log_ns.compare_exchange_strong(last, now);
    return;
  }
  if (now - last < std::chrono::nanoseconds(kWaitLogPeriod).count() ||
      !g_wait_log_ns.compare_exchange_strong(last, now)) {
    return;
  }
  const double seconds = double(now - last) / 1e9;
  const uint64_t steps = g_wait_steps.exchange(0), ns = g_wait_ns.exchange(0);
  REXLOG_INFO("guest GPU waits: {} steps, {:.1f} ms waiting per second over the last {:.1f} s",
              steps, double(ns) / 1e6 / seconds, seconds);
}

// The GPU's progress word the step compares (functions::kGpuProgressWaitStep): the device's
// pointer to it, read the way the step reads it. 0 when there is no device or pointer.
uint32_t GpuProgress(const uint8_t* base, uint32_t state) {
  using torchlight::guest_abi::ReadU32;
  const uint32_t device = ReadU32(base, state, d3d::gpu_wait::kDevice);
  if (!device) return 0;
  const uint32_t word = ReadU32(base, device, d3d::device::kGpuProgressAddress);
  return word ? ReadU32(base, word) : 0;
}

void Watch(const uint8_t* base, uint32_t state, std::chrono::steady_clock::time_point start,
           std::chrono::steady_clock::time_point end) {
  thread_local GpuWaitWatch watch;  // a wait is one guest thread's
  const auto report = watch.Step(start, end, GpuProgress(base, state),
                                 torchlight::platform::SurfaceAwayCount());
  if (!report) return;
  REXLOG_WARN(
      "guest GPU wait: {:.1f} s without reaching its target{} | {} steps; progress word "
      "0x{:08X} at its start, 0x{:08X} now, changed {} times",
      std::chrono::duration<double>(report->waited).count(), report->repeat ? " (still)" : "",
      report->steps, report->progress_at_start, report->progress_now, report->progress_changes);
}

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kGpuProgressWaitStep, 82774170);
REX_EXTERN(__imp__sub_82774170);
REX_FUNC(sub_82774170) {
  const auto start = std::chrono::steady_clock::now();
  const uint32_t state = ctx.r3.u32;  // r3 is the step's result once it returns
  rex::thread::Sleep(kWaitStepSleep);
  __imp__sub_82774170(ctx, base);
  const auto end = std::chrono::steady_clock::now();
  CountWait(start, end);
  Watch(base, state, start, end);
}

}  // extern "C"
