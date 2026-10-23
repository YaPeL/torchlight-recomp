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
// On the replacements' device (tlrepl:) the search and the search for the next file
// (FindNextFileA) leave out the files SetHiddenReplacements named (find_file_filter.h): the
// entries are skipped, and a search that would return only hidden files fails as one that finds
// none. Every way the guest lists or tests a file goes through these two: OGRE's
// FileSystemArchive::findFiles (sub_824B3858, through the C library's _findfirst sub_82862F80 and
// _findnext sub_828630B0), the game's own folder walks (sub_823A0D08, sub_823A1010) and its
// existence checks (the other callers of FindFirstFileA). OGRE opens a file through the archive its
// index names, so a name the replacements' listing leaves out is opened from the pak.
//
// The native search under FindFirstFileA (guest_abi xapi_files.h kNativeSearch) remembers the
// game's files that do not exist (missing_file_cache.h): asked again for one, it gives back the
// kernel's status from the first time without opening anything, and FindFirstFileA turns it into
// the same last error and -1 as before.

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/xapi_files.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/find_file_filter.h"
#include "hooks/missing_file_cache.h"
#include "hooks/guest_path.h"
#include "live/guest_events.h"
#include "mods/replacements_guest_view.h"

namespace {

namespace find = torchlight::guest_abi::xapi::find;

static_assert(find::kFindFirstFile.address == 0x8287E0C8 &&
              torchlight::guest_abi::functions::kFileAttributes.address == find::kFindFirstFile.address);

std::string ReadCString(const uint8_t* base, uint32_t at, uint32_t max_length) {
  std::string out;
  for (uint32_t i = 0; i < max_length && base[at + i]; ++i) out += char(base[at + i]);
  return out;
}

constexpr uint32_t kMaxPath = 1024;  // read up to here; the guest's own limit is MAX_PATH (260)

// WIN32_FIND_DATAA as the guest fills it: dwFileAttributes first, cFileName at 44 (the C library's
// _findfirst copies the name from there, @0x82862FE4 addi r5,r1,124 with the data at r1+80).
constexpr uint32_t kFindDataAttributes = 0;
constexpr uint32_t kFindDataName = 44;
constexpr uint32_t kAttributeDirectory = 0x10;
constexpr uint32_t kInvalidHandle = 0xFFFFFFFFu;

std::mutex g_mutex;
std::shared_ptr<const std::unordered_set<std::string>> g_hidden;  // under g_mutex
std::unordered_map<uint32_t, std::string> g_searches;  // handle -> its folder's key prefix, under g_mutex

std::shared_ptr<const std::unordered_set<std::string>> Hidden() {
  std::lock_guard lock(g_mutex);
  return g_hidden;
}

// The key prefix of a search's folder on the replacements' device ("MEDIA/LEVELSETS/" for
// "tlrepl:\media\levelsets\*.*"), or none for a search elsewhere.
std::optional<std::string> ReplacementsFolder(const std::string& path) {
  if (!torchlight::hooks::OnReplacementsDevice(path)) return std::nullopt;
  const size_t colon = path.find(':');
  const size_t last = path.find_last_of("\\/");
  if (last == std::string::npos || last < colon) return std::string();
  std::string folder = torchlight::mods::GuestViewKey(path.substr(colon + 1, last - colon - 1));
  if (!folder.empty()) folder += '/';
  return folder;
}

// Whether the entry the search just filled is one of the hidden files.
bool HiddenEntry(const uint8_t* base, uint32_t data, const std::string& folder,
                 const std::unordered_set<std::string>& hidden) {
  const uint32_t attributes = torchlight::guest_abi::ReadU32(base, data + kFindDataAttributes);
  if (attributes & kAttributeDirectory) return false;
  return hidden.contains(torchlight::mods::GuestViewKey(folder + ReadCString(base, data + kFindDataName, 260)));
}



}  // namespace

REX_EXTERN(__imp__sub_8287E0C8);
REX_EXTERN(__imp__sub_8287E158);
REX_EXTERN(__imp__sub_8287DC98);
REX_EXTERN(__imp__sub_82883A68);

namespace {

static_assert(find::kNativeSearch.address == 0x82883A68);

torchlight::hooks::MissingFileCache g_missing;

// The path of the native search's OBJECT_ATTRIBUTES (kNativeSearch: ObjectName at +4, an
// ANSI_STRING {Length, MaximumLength, Buffer}).
std::string SearchPath(const uint8_t* base, uint32_t object_attributes) {
  using torchlight::guest_abi::ReadU16;
  using torchlight::guest_abi::ReadU32;
  const uint32_t name = ReadU32(base, object_attributes + 4);
  if (!name) return {};
  const uint16_t length = ReadU16(base, name);
  const uint32_t buffer = ReadU32(base, name + 4);
  if (!buffer || length > kMaxPath) return {};  // no key: never cut a path short
  std::string path;
  path.reserve(length);
  for (uint16_t i = 0; i < length && i < kMaxPath; ++i) path += char(base[buffer + i]);
  return path;
}

}  // namespace

namespace torchlight::hooks {

void SetHiddenReplacements(std::unordered_set<std::string> keys) {
  std::lock_guard lock(g_mutex);
  g_hidden = keys.empty() ? nullptr : std::make_shared<const std::unordered_set<std::string>>(std::move(keys));
}

}  // namespace torchlight::hooks

extern "C" {

REX_FUNC(sub_82883A68) {
  const auto key = torchlight::hooks::MissingFileCache::Key(SearchPath(base, ctx.r3.u32));
  if (key) {
    if (const auto status = g_missing.Find(*key)) {
      ctx.r3.u64 = *status;  // the first answer; nothing opened, nothing filled
      return;
    }
  }
  __imp__sub_82883A68(ctx, base);
  if (key) g_missing.Remember(*key, ctx.r3.u32);
}

REX_FUNC(sub_8287E0C8) {
  const std::string path = ReadCString(base, ctx.r3.u32, kMaxPath);
  const uint32_t data = ctx.r4.u32;
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
  // The replacements' hidden files: skipped; a search that finds only those finds nothing.
  const uint32_t handle = ctx.r3.u32;
  if (handle != kInvalidHandle) {
    const auto hidden = Hidden();
    const auto folder = hidden ? ReplacementsFolder(path) : std::nullopt;
    {
      std::lock_guard lock(g_mutex);
      if (folder) g_searches[handle] = *folder;
      else g_searches.erase(handle);
    }
    if (folder) {
      bool found = true;
      while (found && HiddenEntry(base, data, *folder, *hidden)) {
        ctx.r3.u64 = handle;
        ctx.r4.u64 = data;
        __imp__sub_8287E158(ctx, base);
        found = ctx.r3.u32 != 0;
      }
      if (!found) {
        ctx.r3.u64 = handle;
        __imp__sub_8287DC98(ctx, base);  // FindClose
        {
          std::lock_guard lock(g_mutex);
          g_searches.erase(handle);
        }
        ctx.r3.u64 = kInvalidHandle;
      } else {
        ctx.r3.u64 = handle;
      }
    }
  }
  torchlight::live::GuestEvents::Get().FileCheck(
      path, ctx.r3.u32 != 0xFFFFFFFFu,
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}


// FindNextFileA(handle r3, WIN32_FIND_DATAA* r4) -> nonzero when it filled the next entry
// (@0x8287E158: the native NtQueryDirectoryFile, then the entry copied to r4, r3 = 1). On a search
// of the replacements' device the hidden files are skipped.
REX_FUNC(sub_8287E158) {
  const uint32_t handle = ctx.r3.u32, data = ctx.r4.u32;
  __imp__sub_8287E158(ctx, base);
  if (ctx.r3.u32 == 0) return;
  std::shared_ptr<const std::unordered_set<std::string>> hidden;
  std::string folder;
  {
    std::lock_guard lock(g_mutex);
    const auto it = g_searches.find(handle);
    if (it == g_searches.end() || !g_hidden) return;
    hidden = g_hidden;
    folder = it->second;
  }
  while (ctx.r3.u32 != 0 && HiddenEntry(base, data, folder, *hidden)) {
    ctx.r3.u64 = handle;
    ctx.r4.u64 = data;
    __imp__sub_8287E158(ctx, base);
  }
}

}  // extern "C"
