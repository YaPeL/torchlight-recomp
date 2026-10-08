// Mods diagnostics (TORCHLIGHT_MODS_DIAGNOSTICS only): every directory search the guest makes on
// tlmods: goes to the log, as the guest asks it (path and pattern) and as the runtime answers it
// (status, then each entry's name and whether it is a folder). The searches that follow show where
// the guest goes next. Wraps the guest's FindFirstFileA/FindNextFileA (guest_abi mods.h, find);
// the open and query flags are fixed in the guest's code and documented there.

#ifdef TORCHLIGHT_MODS_DIAGNOSTICS

#include <cctype>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/mods.h"

namespace {

namespace find = torchlight::guest_abi::mods::find;
namespace data = torchlight::guest_abi::mods::find::find_data;
using torchlight::guest_abi::ReadU32;

thread_local uint32_t g_last_status = 0;  // the last native search's NTSTATUS on this thread
std::mutex g_mutex;
std::set<uint32_t> g_handles;  // searches on tlmods: still open

std::string ReadCString(const uint8_t* base, uint32_t at, uint32_t max_length) {
  std::string out;
  for (uint32_t i = 0; i < max_length && base[at + i]; ++i) out += char(base[at + i]);
  return out;
}

bool OnModsDevice(const std::string& path) {
  static constexpr char kDevice[] = "tlmods:";
  if (path.size() < sizeof(kDevice) - 1) return false;
  for (size_t i = 0; i + 1 < sizeof(kDevice); ++i) {
    if (std::tolower(static_cast<unsigned char>(path[i])) != kDevice[i]) return false;
  }
  return true;
}

void LogEntry(const uint8_t* base, uint32_t find_data, const char* what) {
  const uint32_t attributes = ReadU32(base, find_data + data::kAttributes.offset);
  REXLOG_INFO("mods: diagnostics: find {}: \"{}\" attributes 0x{:X}{}", what,
              ReadCString(base, find_data + data::kFileName.offset, data::kFileNameCapacity), attributes,
              attributes & data::kAttributeDirectory ? " (folder)" : "");
}

}  // namespace

REX_EXTERN(__imp__sub_82883A68);
REX_EXTERN(__imp__sub_82883BF0);
REX_EXTERN(__imp__sub_8287E0C8);
REX_EXTERN(__imp__sub_8287E158);

extern "C" {

REX_FUNC(sub_82883A68) {
  __imp__sub_82883A68(ctx, base);
  g_last_status = ctx.r3.u32;
}

REX_FUNC(sub_82883BF0) {
  __imp__sub_82883BF0(ctx, base);
  g_last_status = ctx.r3.u32;
}

REX_FUNC(sub_8287E0C8) {
  static_assert(find::kFindFirstFile.address == 0x8287E0C8 && find::kFindFirstNative.address == 0x82883A68 &&
                find::kFindNextFile.address == 0x8287E158 && find::kFindNextNative.address == 0x82883BF0);
  const std::string path = ReadCString(base, ctx.r3.u32, 1024);
  const uint32_t find_data = ctx.r4.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  g_last_status = 0;
  __imp__sub_8287E0C8(ctx, base);
  if (!OnModsDevice(path)) return;
  const uint32_t handle = ctx.r3.u32;
  REXLOG_INFO("mods: diagnostics: find first \"{}\" (from 0x{:08X}) -> handle 0x{:08X}, status 0x{:08X}", path,
              caller, handle, g_last_status);
  if (handle == 0xFFFFFFFFu) return;
  {
    std::lock_guard lock(g_mutex);
    g_handles.insert(handle);
  }
  LogEntry(base, find_data, "entry");
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
    LogEntry(base, find_data, "entry");
    return;
  }
  REXLOG_INFO("mods: diagnostics: find next on 0x{:08X} ends, status 0x{:08X}", handle, g_last_status);
  std::lock_guard lock(g_mutex);
  g_handles.erase(handle);
}

}  // extern "C"

#endif  // TORCHLIGHT_MODS_DIAGNOSTICS
