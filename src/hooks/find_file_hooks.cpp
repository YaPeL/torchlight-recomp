// The guest's FindFirstFileA with Windows path semantics. The game, written for Windows, builds
// some search paths with '/' (a mod's subfolders: its folder, kept with '/', plus "/*.*"); the Xbox
// library's search splits the path only at '\' and fails without one (guest_abi xapi_files.h), so
// no subfolder of a mod was ever listed. The path is given to the guest with '\' separators
// (guest_path.h), copied below the stack pointer for the call; a path already so goes through as it
// is. The guest's other file functions need nothing: the kernel takes both separators.
//
// With TORCHLIGHT_MODS_DIAGNOSTICS the searches go to the log: those on tlmods: and any whose path
// the hook changed, as asked and as answered (status, then each entry's name and whether it is a
// folder).

#include <cstdint>
#include <string>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/xapi_files.h"
#include "hooks/guest_path.h"

#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
#include <cctype>
#include <mutex>
#include <set>
#endif

namespace {

namespace find = torchlight::guest_abi::xapi::find;

static_assert(find::kFindFirstFile.address == 0x8287E0C8);

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

bool OnModsDevice(const std::string& path) {
  static constexpr char kDevice[] = "tlmods:";
  if (path.size() < sizeof(kDevice) - 1) return false;
  for (size_t i = 0; i + 1 < sizeof(kDevice); ++i) {
    if (std::tolower(static_cast<unsigned char>(path[i])) != kDevice[i]) return false;
  }
  return true;
}

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
  const auto windows = torchlight::hooks::WindowsSeparators(path);
#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
  const uint32_t find_data = ctx.r4.u32;
  const uint32_t caller = uint32_t(ctx.lr);
  g_last_status = 0;
#endif
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
#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
  if (!windows && !OnModsDevice(path)) return;
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
