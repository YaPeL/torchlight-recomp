// The guest's file existence checks, file reads and XMemAlloc calls, timed for the long frame
// report (live/guest_events.h). Every mode; the original always runs, the override only reads its
// arguments and result.

#include <chrono>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "live/guest_events.h"

namespace {

namespace fn = torchlight::guest_abi::functions;
using torchlight::live::GuestEvents;
using Clock = std::chrono::steady_clock;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

double Ms(Clock::time_point since) {
  return std::chrono::duration<double, std::milli>(Clock::now() - since).count();
}

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kFileAttributes, 8287E0C8);
REX_EXTERN(__imp__sub_8287E0C8);
REX_FUNC(sub_8287E0C8) {
  const uint32_t path = ctx.r3.u32;
  const auto start = Clock::now();
  __imp__sub_8287E0C8(ctx, base);
  const double ms = Ms(start);
  GuestEvents::Get().FileCheck(torchlight::guest_abi::ogre::ReadCString(base, path, 260), ctx.r3.u32 != 0xFFFFFFFFu, ms);
}

FUNCTION_ADDRESS_CHECK(kReadFile, 8287F408);
REX_EXTERN(__imp__sub_8287F408);
REX_FUNC(sub_8287F408) {
  const uint32_t bytes = ctx.r5.u32;
  const auto start = Clock::now();
  __imp__sub_8287F408(ctx, base);
  GuestEvents::Get().Read(bytes, Ms(start));
}

FUNCTION_ADDRESS_CHECK(kXMemAlloc, 8287D640);
REX_EXTERN(__imp__sub_8287D640);
REX_FUNC(sub_8287D640) {
  const uint32_t bytes = ctx.r3.u32;
  const auto start = Clock::now();
  __imp__sub_8287D640(ctx, base);
  GuestEvents::Get().Allocation(bytes, Ms(start));
}

}  // extern "C"
