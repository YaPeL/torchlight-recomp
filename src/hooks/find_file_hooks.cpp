// The guest's FindFirstFileA with Windows path semantics on the mods' device. The game, written for
// Windows, builds a mod's subfolder search with '/' (its folder, kept with '/', plus "/*.*"); the
// Xbox library's search splits the path only at '\' and fails without one, so no subfolder of a
// mod was ever listed. A search on a mod's device (tlmod<N>:) is given its path with '\' separators (guest_path.h),
// copied below the stack pointer for the call; any other search goes through as it is (guest_abi
// xapi_files.h says why it is not global). The guest's other file functions need nothing: the
// kernel takes both separators.
//
// Every search is also timed for the long frame report (live/guest_events.h), as the game's file
// existence check (guest_abi guest_functions.h kFileAttributes), with the path the game gave.

#include <chrono>
#include <cstdint>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/xapi_files.h"
#include "guest_abi/guest_functions.h"
#include "hooks/guest_path.h"
#include "live/guest_events.h"

namespace {

namespace find = torchlight::guest_abi::xapi::find;

static_assert(find::kFindFirstFile.address == 0x8287E0C8 &&
              torchlight::guest_abi::functions::kFileAttributes.address == find::kFindFirstFile.address);

constexpr uint32_t kMaxPath = 1024;  // read up to here; the guest's own limit is MAX_PATH (260)

std::string ReadCString(const uint8_t* base, uint32_t at, uint32_t max_length) {
  std::string out;
  for (uint32_t i = 0; i < max_length && base[at + i]; ++i) out += char(base[at + i]);
  return out;
}


}  // namespace

REX_EXTERN(__imp__sub_8287E0C8);

extern "C" {

REX_FUNC(sub_8287E0C8) {
  const std::string path = ReadCString(base, ctx.r3.u32, kMaxPath);
  const auto windows = torchlight::hooks::ModsSearchPath(path);
  const auto start = std::chrono::steady_clock::now();
  if (windows) {
    // The copy sits below the caller's stack pointer; the call's frames go below the copy.
    const uint64_t saved_r1 = ctx.r1.u64;
    const uint32_t size = (uint32_t(windows->size()) + 1 + 15) & ~15u;
    const uint32_t at = ((ctx.r1.u32 - size) & ~15u) - 16;
    for (size_t i = 0; i < windows->size(); ++i) base[at + i] = uint8_t((*windows)[i]);
    base[at + windows->size()] = 0;
    ctx.r1.u64 = at - 16;
    ctx.r3.u64 = at;
    __imp__sub_8287E0C8(ctx, base);
    ctx.r1.u64 = saved_r1;
  } else {
    __imp__sub_8287E0C8(ctx, base);
  }
  torchlight::live::GuestEvents::Get().FileCheck(
      path, ctx.r3.u32 != 0xFFFFFFFFu,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}


}  // extern "C"
