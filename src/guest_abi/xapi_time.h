// The guest's time sources from the Xbox library it links (XAPI and the C runtime, statically
// linked): what the deterministic development runs replace (dev/deterministic_time.h).

#pragma once

#include "guest_abi/guest_functions.h"

namespace torchlight::guest_abi::xapi::time {

using functions::GuestFunction;

// [confirmed] QueryPerformanceCounter(r3 = LARGE_INTEGER*) -> 1: the timebase (mftb, read again
// when its low word is 0, @0x821F8FE0..@0x821F8FEC) stored as a u64 at r3 (@0x821F8FF0). Ten
// callers, among them OGRE's Timer (Timer::reset @0x82423690, Timer::getMilliseconds @0x824236DC).
inline constexpr GuestFunction kQueryPerformanceCounter{0x821F8FE0, Confidence::kConfirmed};
// [confirmed] QueryPerformanceFrequency(r3 = LARGE_INTEGER*) -> 1: the kernel's frequency (import
// thunk 0x830263CC, KeQueryPerformanceFrequency) stored as a u64 at r3 (@0x821F90A0).
inline constexpr GuestFunction kQueryPerformanceFrequency{0x821F9088, Confidence::kConfirmed};
// [confirmed] GetTickCount() -> milliseconds: the kernel's tick count, read from the kernel time
// stamp bundle (*(0x820006A4) + 16, @0x821F8EE4..@0x821F8EE8). 62 callers; OGRE's Timer
// compares it with the counter to correct leaps (Timer::getMilliseconds @0x82423710).
inline constexpr GuestFunction kGetTickCount{0x821F8EE0, Confidence::kConfirmed};
// [confirmed] The C runtime's time(r3 = time_t* or 0) -> seconds since 1970 as a 64-bit time_t:
// the system time (sub_82883280, KeQuerySystemTime) less the 1601-1970 offset, divided by 10^7
// (@0x8285CB24..@0x8285CB58), stored at r3 when it is not 0 (@0x8285CB70). Its callers seed the
// C runtime's rand() with it (srand(time(0)): sub_82204590, sub_82214818, sub_8224F620; srand
// is sub_8285CAD8, the seed per thread at +20 of the thread's data), and sub_82657A00.
inline constexpr GuestFunction kTime{0x8285CB08, Confidence::kConfirmed};

}  // namespace torchlight::guest_abi::xapi::time
