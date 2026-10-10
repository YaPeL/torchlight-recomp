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
//
// With TORCHLIGHT_MODS_DIAGNOSTICS the searches on the mods' devices go to the log, as asked and as answered
// (status, then each entry's name and whether it is a folder).

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

#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
#include <mutex>
#include <set>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(mods_find_log, false, "Torchlight",
                    "Diagnostics: log every file search on the mods' devices and its entries (a lot of "
                    "lines: thousands in seconds with a big mod)");
#endif

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

#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
namespace data = torchlight::guest_abi::xapi::find::find_data;
using torchlight::guest_abi::ReadU32;

static_assert(find::kFindFirstNative.address == 0x82883A68 && find::kFindNextFile.address == 0x8287E158 &&
              find::kFindNextNative.address == 0x82883BF0);

thread_local uint32_t g_last_status = 0;  // the last native search's NTSTATUS on this thread
std::mutex g_mutex;
std::set<uint32_t> g_handles;  // logged searches still open

bool OnModsDevice(const std::string& path) { return torchlight::hooks::OnModDevice(path); }

void LogEntry(const uint8_t* base, uint32_t find_data) {
  const uint32_t attributes = ReadU32(base, find_data + data::kAttributes.offset);
  REXLOG_INFO("mods: diagnostics: find entry: \"{}\" attributes 0x{:X}{}",
              ReadCString(base, find_data + data::kFileName.offset, data::kFileNameCapacity), attributes,
              attributes & data::kAttributeDirectory ? " (folder)" : "");
}
#endif

}  // namespace

REX_EXTERN(__imp__sub_8287E0C8);
#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
REX_EXTERN(__imp__sub_8287E158);
REX_EXTERN(__imp__sub_82883A68);
REX_EXTERN(__imp__sub_82883BF0);
#endif

extern "C" {

REX_FUNC(sub_8287E0C8) {
  const std::string path = ReadCString(base, ctx.r3.u32, kMaxPath);
  const auto windows = torchlight::hooks::ModsSearchPath(path);
#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
  const uint32_t find_data = ctx.r4.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  g_last_status = 0;
#endif
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
#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
  if (!REXCVAR_GET(mods_find_log) || (!windows && !OnModsDevice(path))) return;
  const uint32_t handle = ctx.r3.u32;
  REXLOG_INFO("mods: diagnostics: find first \"{}\"{} (from 0x{:08X}) -> handle 0x{:08X}, status 0x{:08X}", path,
              windows ? " as \"" + *windows + "\"" : std::string(), caller, handle, g_last_status);
  if (handle == 0xFFFFFFFFu) return;
  {
    std::lock_guard lock(g_mutex);
    g_handles.insert(handle);
  }
  LogEntry(base, find_data);
#endif
}

#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
REX_FUNC(sub_82883A68) {
  __imp__sub_82883A68(ctx, base);
  g_last_status = ctx.r3.u32;
}

REX_FUNC(sub_82883BF0) {
  __imp__sub_82883BF0(ctx, base);
  g_last_status = ctx.r3.u32;
}

REX_FUNC(sub_8287E158) {
  const uint32_t handle = ctx.r3.u32;
  const uint32_t find_data = ctx.r4.u32;
  bool tracked;
  {
    std::lock_guard lock(g_mutex);
    tracked = g_handles.contains(handle);
  }
  g_last_status = 0;
  __imp__sub_8287E158(ctx, base);
  if (!tracked) return;
  if (ctx.r3.u32) {
    LogEntry(base, find_data);
    return;
  }
  REXLOG_INFO("mods: diagnostics: find next on 0x{:08X} ends, status 0x{:08X}", handle, g_last_status);
  std::lock_guard lock(g_mutex);
  g_handles.erase(handle);
}
#endif

}  // extern "C"
