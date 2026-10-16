#include "mods/unit_index_install.h"

#include <chrono>
#include <fstream>
#include <iterator>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>


#include "game_menu/guest_call.h"
#include "game_menu/save_import_menu.h"
#include "game_menu/video_menu.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/mods.h"
#include "guest_abi/unit_index.h"
#include "live/busy_notice.h"
#include "live/busy_notice_text.h"
#include "mods/save_units_install.h"
#include "mods/unit_cache.h"
#include "mods/unit_index.h"
#include "save_import/pak.h"

REX_EXTERN(__imp__sub_823296D0);


namespace torchlight::mods {

namespace {

namespace abi = torchlight::guest_abi;
namespace ui = torchlight::guest_abi::game_ui;
namespace mods_abi = torchlight::guest_abi::mods;
namespace units_abi = torchlight::guest_abi::unit_index;
using game_menu::GuestCall;

constexpr const char* kDeviceMount = "\\Device\\TorchlightUnitIndex";
constexpr const char* kDeviceLink = "tlunits:";

struct State {
  bool active = false;      // the hook does something
  bool ready = false;       // the cached index exists for `key`
  bool mounted = false;     // tlunits: mounted (once the file exists)
  bool located = false;     // tlunits: added as a resource location
  rex::Runtime* runtime = nullptr;
  std::filesystem::path folder, pak;
  std::string key;
  std::string name;  // the cached index loaded: the key, or IncompleteUnitIndexName when units were left out
  std::vector<std::u16string> unit_paths;  // the mods' unit definitions, by priority
};
State g;

// A guest std::wstring in the scratch area from UTF-16 text (the game's constructor); destroy it
// with ui::kWStringDtor.
uint32_t ScratchWString(GuestCall& call, std::u16string_view text) {
  const uint32_t chars = call.Reserve(static_cast<uint32_t>(2 * (text.size() + 1)));
  const uint32_t str = call.Reserve(abi::ogre::stl_string::kSize.bytes);
  if (!chars || !str) return 0;
  uint8_t* base = call.base();
  for (size_t i = 0; i <= text.size(); ++i) {
    const char16_t c = i < text.size() ? text[i] : u'\0';
    base[chars + 2 * i] = static_cast<uint8_t>(c >> 8);
    base[chars + 2 * i + 1] = static_cast<uint8_t>(c);
  }
  call.Call(ui::kWStringFromText.address, {str, chars});
  return str;
}

std::u16string ReadWString(const uint8_t* base, uint32_t str) {
  const uint32_t length = abi::ReadU32(base, str + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + abi::ogre::stl_string::kCapacity.offset);
  if (length > 0xFFFF) return {};
  const uint32_t text = capacity > mods_abi::manager::kWStringInlineCapacity ? abi::ReadU32(base, str) : str;
  std::u16string out(length, u'\0');
  for (uint32_t i = 0; i < length; ++i) out[i] = static_cast<char16_t>(abi::ReadU16(base, text + 2 * i));
  return out;
}

std::u16string AsciiUpper(std::u16string s) {
  for (char16_t& c : s) {
    if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - u'a' + u'A');
  }
  return s;
}

std::optional<int64_t> ParseGuid(const std::u16string& text) {
  std::string ascii;
  for (char16_t c : text) {
    if (c > 0x7F) return std::nullopt;
    ascii.push_back(static_cast<char>(c));
  }
  return ParseUnitGuid(ascii);
}


// One unit definition read the way the game's index builder reads it (guest_abi/unit_index.h):
// the definition and its BASEFILE chain loaded by the game, each value through the game's
// property reads with the defaults the Xbox index shows (docs/mods.md, 7e).
std::optional<UnitEntry> ReadUnit(GuestCall& call, const std::u16string& game_path, std::string* why) {
  uint8_t* base = call.base();
  const uint32_t mark = call.Mark();
  const uint32_t list = call.Reserve(units_abi::node_list::kSize.bytes);
  const uint32_t path = ScratchWString(call, game_path);
  if (!list || !path) {
    *why = "no scratch space";
    call.Release(mark);
    return std::nullopt;
  }
  call.WriteU32(list + units_abi::node_list::kGrowth.offset, units_abi::node_list::kGrowthValue);
  call.Call(units_abi::kLoadUnitDefinition.address, {path, list});

  std::optional<UnitEntry> entry;
  if (call.ReadU32(list + units_abi::node_list::kCount.offset) == 0) {
    *why = "the game could not load it";
  } else {
    auto read_u32 = [&](std::u16string_view name, uint32_t fallback, bool is_signed) {
      const uint32_t m = call.Mark();
      const uint32_t key = ScratchWString(call, name);
      const uint32_t value = call.Reserve(4);
      call.WriteU32(value, fallback);
      const uint32_t out = call.Call(is_signed ? units_abi::kReadS32.address : units_abi::kReadU32.address,
                                     {key, value, list});
      call.Call(ui::kWStringDtor.address, {key});
      call.Release(m);
      return out;
    };
    auto read_text = [&](std::u16string_view name) {
      const uint32_t m = call.Mark();
      const uint32_t key = ScratchWString(call, name);
      const uint32_t fallback = ScratchWString(call, u"");
      const uint32_t out = call.Reserve(abi::ogre::stl_string::kSize.bytes);
      call.Call(units_abi::kReadString.address, {out, key, fallback, list});
      std::u16string value = ReadWString(base, out);
      call.Call(ui::kWStringDtor.address, {out});
      call.Call(ui::kWStringDtor.address, {fallback});
      call.Call(ui::kWStringDtor.address, {key});
      call.Release(m);
      return value;
    };
    UnitEntry e;
    const auto guid = ParseGuid(read_text(u"UNIT_GUID"));
    if (!guid) {
      *why = "no UNIT_GUID";
    } else {
      e.guid = *guid;
      e.name = AsciiUpper(read_text(u"NAME"));
      e.file = game_path;
      e.equipment = read_text(u"CREATEAS") == u"EQUIPMENT";
      e.level = read_u32(u"LEVEL", 1, false);
      e.min_level = read_u32(u"MINLEVEL", 0, false);
      e.max_level = read_u32(u"MAXLEVEL", 0, false);
      // RARITY defaults to 1 and RARITY_HARDCORE to RARITY (as PC reads them for spawning,
      // 0x537541 and 0x53759B). A unit without a NAME (a base the game never spawns) has both at 0
      // in the Xbox file whatever it sets: all 118 such entries do (docs/mods.md, 7e).
      if (!e.name.empty()) {
        e.rarity = read_u32(u"RARITY", 1, true);
        e.rarity_hardcore = read_u32(u"RARITY_HARDCORE", e.rarity, true);
      }
      e.unit_type = read_text(u"UNITTYPE");
      entry = e;
    }
  }
  // The nodes are kept, not emptied with kClearNodeList as the game's own builder does: emptying
  // them released the names of their properties, and later definitions went on using names the
  // global table no longer had (guest_abi unit_index.h, names; docs/mods.md 7g). The memory they
  // hold stays with the game for the session.
  call.Call(ui::kWStringDtor.address, {path});
  call.Release(mark);
  return entry;
}

// The notice over the still screen while the index is built and the saves checked against it
// (live/busy_notice.h): with a large pack it takes seconds (1984 units: about 4 s on Linux, 17 s on
// a Windows pass), and nothing else is drawn meanwhile.
struct BuildNotice {
  BuildNotice() { live::ShowBusyNotice(game_menu::NoticeText(live::kPreparingModsText)); }
  ~BuildNotice() { live::HideBusyNotice(); }
};

void BuildIndex(GuestCall& call) {
  std::string error;
  const auto start = std::chrono::steady_clock::now();
  const auto base_index = ReadPakUnitIndex(g.pak, &error);
  if (!base_index) {
    REXLOG_ERROR("mods: cannot read the game's unit index ({}); mods' units left out", error);
    return;
  }
  std::vector<UnitEntry> units;
  size_t left_out = 0;
  int logged_percent = 0;
  for (size_t i = 0; i < g.unit_paths.size(); ++i) {
    live::SetBusyNoticeProgress(i, g.unit_paths.size());
    if (const int percent = live::BusyPercent(i, g.unit_paths.size()); percent > logged_percent) {
      logged_percent = percent;
      REXLOG_INFO("mods: building the unit index, {}% ({} of {} units)", percent, i, g.unit_paths.size());
    }
    const std::u16string& path = g.unit_paths[i];
    std::string why;
    if (auto e = ReadUnit(call, path, &why)) {
      units.push_back(std::move(*e));
    } else {
      ++left_out;
      REXLOG_WARN("mods: unit {} left out: {}", save_import::Utf8(path), why);
    }
  }
  std::vector<std::u16string> skipped;
  const UnitIndex merged = MergeUnitIndex(*base_index, units, &skipped);
  for (const auto& path : skipped) REXLOG_WARN("mods: unit {} is outside MEDIA/UNITS/<group>/", save_import::Utf8(path));
  // Units left out may be read next time (a fault of ours or of the moment, not of the mod's files,
  // which the key covers): such an index serves this start only.
  g.name = left_out ? IncompleteUnitIndexName(g.key) : g.key;
  if (left_out) REXLOG_WARN("mods: {} mods' units left out; the index is built again at the next start", left_out);
  if (!StoreCachedUnitIndex(g.folder, g.name, merged, &error)) {
    REXLOG_ERROR("mods: cannot store the unit index ({}); mods' units left out", error);
    return;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  REXLOG_INFO("mods: unit index built in {} ms: {} mods' units, {} entries ({})", ms.count(), units.size(),
              merged.size(), g.key);
  g.ready = true;
}

void HookLoadIndex(PPCContext& ctx, uint8_t* base);

}  // namespace

void InstallUnitIndex(rex::Runtime* runtime, const std::vector<ModUnitFile>& files,
                      const std::filesystem::path& cache_dir, const std::filesystem::path& pak) {
  g = {};
  g.pak = pak;
  g.folder = cache_dir / "unitdata";
  if (files.empty()) return;  // the Xbox index as it is
  using Clock = std::chrono::steady_clock;
  const auto ms = [](Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
  };
  const auto t1 = Clock::now();
  g.active = true;

  const auto identity = PakIdentity(pak);
  const auto t2 = Clock::now();
  if (!identity || !runtime || !runtime->file_system()) {
    REXLOG_ERROR("mods: cannot identify {}; mods' units left out", pak.string());
    g.active = false;
    return;
  }
  g.unit_paths = UnitPathsByPriority(files);
  g.key = UnitCacheKey(files, *identity);
  std::string log;
  g.name = g.key;
  const auto t3 = Clock::now();
  g.ready = FindCachedUnitIndex(g.folder, g.key, &log).has_value();
  const auto t4 = Clock::now();
  if (!log.empty()) REXLOG_WARN("mods: {}", log);
  g.runtime = runtime;
  REXLOG_INFO("mods: {} unit definitions from mods; cached index {} (pak identity {} ms, key {} ms, cache lookup "
              "{} ms)",
              g.unit_paths.size(), g.ready ? "found" : "to build", ms(t1, t2), ms(t2, t3), ms(t3, t4));
}

bool UnitIndexBuiltInGame() { return g.active && !g.ready; }

namespace {

std::unordered_set<int64_t> GuidsOf(const UnitIndex& index) {
  std::unordered_set<int64_t> guids;
  for (const auto& group : index.groups) {
    for (const UnitEntry& e : group) guids.insert(e.guid);
  }
  return guids;
}

std::optional<UnitIndex> ReadOurIndex(std::string* error) {
  std::string log;
  const auto path = FindCachedUnitIndex(g.folder, g.name, &log);
  if (!path) {
    *error = log.empty() ? "not in the cache" : log;
    return std::nullopt;
  }
  std::ifstream in(*path, std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
  return ParseUnitIndex(bytes, error);
}

// The Xbox index loaded by the game (its own path): what the saves' comparison gets.
void CompareWithXboxIndex(uint32_t loaded) {
  std::string error;
  const auto xbox = ReadPakUnitIndex(g.pak, &error);
  if (xbox && CheckLoadedIndex(loaded, LoadedUnitCount(*xbox)) == LoadedIndex::kComplete) {
    CompareWithLoadedUnits(GuidsOf(*xbox));
    return;
  }
  REXLOG_ERROR("mods: the game's own unit index: {} units loaded ({})", loaded,
               xbox ? std::to_string(LoadedUnitCount(*xbox)) + " in the file" : error);
  CompareWithLoadedUnits(std::nullopt);
}

// After the game loaded our index: the size of its GUID map against what it keeps of the file we
// wrote (LoadedUnitCount).
// Nothing loaded (the file was not read): the game's own index is loaded instead, with its own
// path; nothing was inserted, so that is the load the game would have done. Part of it: left as it
// is, since going back is not safe (a replaced entry is freed but stays filed under its other
// names; guest_abi unit_index.h kLoadIndex).
void VerifyLoadedIndex(PPCContext& ctx, uint8_t* base, uint32_t index, uint32_t original_path) {
  const auto loaded_count = [&] { return abi::ReadU32(base, index + units_abi::index::kGuidMapSize.offset); };
  const uint32_t loaded = loaded_count();
  std::string error;
  const auto ours = ReadOurIndex(&error);
  if (!ours) {
    REXLOG_ERROR("mods: the unit index the game loaded ({}) cannot be read back ({}); {} units loaded", UnitIndexFileName(g.name),
                 error, loaded);
    CompareWithLoadedUnits(std::nullopt);
    return;
  }
  const size_t expected = LoadedUnitCount(*ours);
  switch (CheckLoadedIndex(loaded, expected)) {
    case LoadedIndex::kComplete:
      REXLOG_INFO("mods: the game loaded the unit index: {} units", loaded);
      CompareWithLoadedUnits(GuidsOf(*ours));
      return;
    case LoadedIndex::kEmpty:
      REXLOG_ERROR("mods: the game loaded 0 of the {} units in {} (the file was not read); loading the game's "
                   "own unit index instead, without the mods' units",
                   expected, UnitIndexFileName(g.name));
      ctx.r3.u64 = index;
      ctx.r4.u64 = original_path;
      __imp__sub_823296D0(ctx, base);
      REXLOG_INFO("mods: the game's own unit index loaded: {} units", loaded_count());
      CompareWithXboxIndex(loaded_count());
      return;
    case LoadedIndex::kIncomplete:
    case LoadedIndex::kMore:
      REXLOG_ERROR("mods: the game loaded {} of the {} units in {}; left as it is (going back to the game's own "
                   "index after a partial load is not safe)",
                   loaded, expected, UnitIndexFileName(g.name));
      CompareWithLoadedUnits(std::nullopt);
      return;
  }
}

// The cache folder as tlunits:, mounted only once the index file is in it. The device reads the
// folder when it is mounted, and listing it returns what was there then (docs/mods.md, section 7e,
// folders we mount): mounted before the file was written, the folder listed empty to OGRE and the
// game never found the index. Registering a device while the guest runs is safe: the file system
// takes its global lock to register and to resolve.
bool MountIndexFolder() {
  if (!g.runtime || !g.runtime->file_system()) return false;
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kDeviceMount, g.folder, /*read_only=*/true);
  if (!device->Initialize() || !g.runtime->file_system()->RegisterDevice(std::move(device)) ||
      !g.runtime->file_system()->RegisterSymbolicLink(kDeviceLink, kDeviceMount)) {
    REXLOG_ERROR("mods: cannot mount {}; the game's own unit index is loaded", g.folder.string());
    return false;
  }
  return true;
}

// The game's unit index loader (guest_abi/unit_index.h kLoadIndex): with mods' units, our index
// is built if needed and loaded in place of the Xbox file, by a name under tlunits: (mounted and
// added as a resource location once the file exists, since both list the folder then), and what the
// game kept is checked (VerifyLoadedIndex). The saves were checked before the guest ran
// (save_units_install.h); this only compares.
void HookLoadIndex(PPCContext& ctx, uint8_t* base) {
  if (!g.active) {
    __imp__sub_823296D0(ctx, base);
    return;
  }
  const uint32_t index = ctx.r3.u32;
  const uint32_t original_path = ctx.r4.u32;
  uint32_t our_path = 0, our_text = 0;
  {
    GuestCall call(ctx, base, 0x4000);
    if (!g.unit_paths.empty() && !g.ready) {
      const BuildNotice notice;
      BuildIndex(call);
      live::SetBusyNoticeProgress(1, 1);
      CheckDeferredSaves();
    }
    if (g.ready && !g.mounted) g.mounted = MountIndexFolder();
    if (g.ready && g.mounted && !g.located) {
      g.located = game_menu::AddFileSystemLocation(call, std::string(kDeviceLink) + "\\", false);
      if (!g.located) REXLOG_ERROR("mods: cannot add {} as a resource location", kDeviceLink);
    }
    if (g.ready && g.mounted && g.located) {
      // On the guest heap: the scratch area is below the stack pointer the loader will use.
      const std::string file = UnitIndexFileName(g.name);
      const std::u16string name(file.begin(), file.end());
      our_text = call.Call(mods_abi::kAlloc.address, {0, static_cast<uint32_t>(2 * (name.size() + 1))});
      our_path = call.Call(mods_abi::kAlloc.address, {0, abi::ogre::stl_string::kSize.bytes});
      if (our_text && our_path) {
        for (size_t i = 0; i <= name.size(); ++i) {
          const char16_t c = i < name.size() ? name[i] : u'\0';
          base[our_text + 2 * i] = static_cast<uint8_t>(c >> 8);
          base[our_text + 2 * i + 1] = static_cast<uint8_t>(c);
        }
        call.Call(ui::kWStringFromText.address, {our_path, our_text});
        REXLOG_INFO("mods: loading the unit index {}", save_import::Utf8(name));
      } else {
        our_path = 0;
      }
    }
  }
  ctx.r3.u64 = index;
  ctx.r4.u64 = our_path ? our_path : original_path;
  __imp__sub_823296D0(ctx, base);
  if (our_path) {
    VerifyLoadedIndex(ctx, base, index, original_path);
  } else if (!g.unit_paths.empty()) {
    CompareWithXboxIndex(abi::ReadU32(base, index + units_abi::index::kGuidMapSize.offset));
  }
  if (our_path || our_text) {
    GuestCall call(ctx, base);
    if (our_path) {
      call.Call(ui::kWStringDtor.address, {our_path});
      call.Call(mods_abi::kFree.address, {our_path});
    }
    if (our_text) call.Call(mods_abi::kFree.address, {our_text});
  }
}

}  // namespace

}  // namespace torchlight::mods

extern "C" {
static_assert(torchlight::guest_abi::unit_index::kLoadIndex.address == 0x823296D0u, "unit_index mismatch");
REX_FUNC(sub_823296D0) { torchlight::mods::HookLoadIndex(ctx, base); }
}
