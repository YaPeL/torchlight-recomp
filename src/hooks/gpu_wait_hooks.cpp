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

#include <chrono>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/thread.h>

#include "guest_abi/guest_functions.h"
#include "live/install.h"

namespace {

namespace fn = torchlight::guest_abi::functions;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

// Short against a frame (8.3 ms at 120 Hz) and long enough that the waiting thread stays asleep
// (rex::thread::Sleep waits on a high resolution timer on Windows, patch 17; nanosleep on POSIX).
constexpr std::chrono::microseconds kWaitStepSleep{200};

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kGpuProgressWaitStep, 82774170);
REX_EXTERN(__imp__sub_82774170);
REX_FUNC(sub_82774170) {
  // TEMPORARY (perf/native-vs-xenos): Xenos is measured without this project's optimizations.
  static const bool sleep = torchlight::live::OnlyMode();
  if (sleep) rex::thread::Sleep(kWaitStepSleep);
  __imp__sub_82774170(ctx, base);
}

}  // extern "C"
