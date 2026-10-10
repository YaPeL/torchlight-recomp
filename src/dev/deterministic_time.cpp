#include "dev/deterministic_time.h"

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(dev_deterministic_time, false, "Torchlight",
                    "Development builds only, with --dev_input_script: a fixed date for time() and "
                    "a game clock that moves a fixed step per guest frame, so runs of the same "
                    "script can be compared");

#ifndef TORCHLIGHT_DEV_INPUT

namespace torchlight::dev {
void InstallDeterministicTime() {
  if (REXCVAR_GET(dev_deterministic_time)) {
    REXLOG_WARN("dev: --dev_deterministic_time ignored: this build has no input scripts "
                "(CMake option TORCHLIGHT_DEV_INPUT)");
  }
}
bool DeterministicTimeOn() { return false; }
void AdvanceDeterministicTime() {}
double DeterministicSeconds() { return 0; }
}  // namespace torchlight::dev

#else

#include <atomic>
#include <cstdint>

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "dev/virtual_clock.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/xapi_time.h"

namespace torchlight::dev {

namespace {

namespace time_abi = torchlight::guest_abi::xapi::time;

static_assert(time_abi::kQueryPerformanceCounter.address == 0x821F8FE0 &&
              time_abi::kQueryPerformanceFrequency.address == 0x821F9088 &&
              time_abi::kGetTickCount.address == 0x821F8EE0 && time_abi::kTime.address == 0x8285CB08);

// The virtual counter's rate and step: 50 MHz (the Xbox timebase's order), 60 guest frames per
// virtual second. The start is one hour, so nothing reads a counter near 0.
constexpr uint64_t kFrequency = 50'000'000;
constexpr uint64_t kTicksPerFrame = kFrequency / 60;
// time()'s fixed date: 2010-01-01 00:00:00 UTC.
constexpr uint64_t kFixedTime = 1'262'304'000;

std::atomic<bool> g_on{false};
VirtualClock g_clock(kFrequency * 3600, kTicksPerFrame);
thread_local VirtualClock::Reader t_reader;

}  // namespace

void InstallDeterministicTime() {
  if (!REXCVAR_GET(dev_deterministic_time)) return;
  g_on = true;
  REXLOG_INFO("dev: deterministic time: time() fixed at {}, the game's counter {} ticks per guest "
              "frame at {} Hz",
              kFixedTime, kTicksPerFrame, kFrequency);
}

bool DeterministicTimeOn() { return g_on; }

void AdvanceDeterministicTime() {
  if (g_on) g_clock.Advance();
}

double DeterministicSeconds() { return double(g_clock.frame() * kTicksPerFrame) / double(kFrequency); }

}  // namespace torchlight::dev

namespace {

void StoreU64(uint8_t* base, uint32_t address, uint64_t value) {
  torchlight::guest_abi::WriteU32(base, address, uint32_t(value >> 32));
  torchlight::guest_abi::WriteU32(base, address + 4, uint32_t(value));
}

}  // namespace

REX_EXTERN(__imp__sub_821F8FE0);
REX_EXTERN(__imp__sub_821F9088);
REX_EXTERN(__imp__sub_821F8EE0);
REX_EXTERN(__imp__sub_8285CB08);

extern "C" {

// QueryPerformanceCounter.
REX_FUNC(sub_821F8FE0) {
  using namespace torchlight::dev;
  if (!g_on) {
    __imp__sub_821F8FE0(ctx, base);
    return;
  }
  StoreU64(base, ctx.r3.u32, g_clock.Read(t_reader));
  ctx.r3.u64 = 1;
}

// QueryPerformanceFrequency.
REX_FUNC(sub_821F9088) {
  using namespace torchlight::dev;
  if (!g_on) {
    __imp__sub_821F9088(ctx, base);
    return;
  }
  StoreU64(base, ctx.r3.u32, kFrequency);
  ctx.r3.u64 = 1;
}

// GetTickCount: the same clock, in milliseconds.
REX_FUNC(sub_821F8EE0) {
  using namespace torchlight::dev;
  if (!g_on) {
    __imp__sub_821F8EE0(ctx, base);
    return;
  }
  ctx.r3.u64 = uint32_t(g_clock.Read(t_reader) / (kFrequency / 1000));
}

// The C runtime's time().
REX_FUNC(sub_8285CB08) {
  using namespace torchlight::dev;
  if (!g_on) {
    __imp__sub_8285CB08(ctx, base);
    return;
  }
  if (ctx.r3.u32) StoreU64(base, ctx.r3.u32, kFixedTime);
  ctx.r3.u64 = kFixedTime;
}

}  // extern "C"

#endif  // TORCHLIGHT_DEV_INPUT
