#include "game_menu/mods_install.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>

#include "game_menu/guest_call.h"
#include "game_menu/save_mod_list.h"
#include "game_menu/video_menu.h"
#include "game_setup/game_files.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/mods.h"
#include "hooks/guest_path.h"
#include "mods/mod_list.h"
#include "mods/save_safety.h"

namespace torchlight::game_menu {

namespace {

namespace abi = torchlight::guest_abi;
namespace mods_abi = torchlight::guest_abi::mods;
namespace ui = torchlight::guest_abi::game_ui;

// Each mod's folder is its own device, "\Device\TorchlightMod<N>" linked as "tlmod<N>:"
// (hooks/guest_path.h ModDeviceLink), N its place in the plan: the guest never sees the folder's
// name, which may hold characters the kernel refuses in a path, as the Xbox does (a comma).
std::string DeviceMount(size_t index) { return "\\Device\\TorchlightMod" + std::to_string(index); }
constexpr const char* kRecordFile = "mod_set.txt";

// Set by InstallMods before the guest runs; read once by RegisterMods on the game's thread.
mods::ModPlan g_plan;
bool g_mounted = false;

std::optional<std::vector<uint8_t>> ReadFile(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

bool WriteFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

// A guest std::wstring built in the scratch area from ASCII text (a mod's device name), with the
// game's constructor; destroy it with kWStringDtor.
uint32_t GuestWString(GuestCall& call, uint8_t* base, const std::string& ascii) {
  const uint32_t text = call.Reserve(static_cast<uint32_t>(2 * (ascii.size() + 1)));
  const uint32_t str = call.Reserve(abi::ogre::stl_string::kSize.bytes);
  if (!text || !str) return 0;
  for (size_t i = 0; i < ascii.size(); ++i) {
    base[text + 2 * i] = 0;
    base[text + 2 * i + 1] = static_cast<uint8_t>(ascii[i]);
  }
  base[text + 2 * ascii.size()] = base[text + 2 * ascii.size() + 1] = 0;
  call.Call(ui::kWStringFromText.address, {str, text});
  return str;
}

#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
// Diagnostics: each mod's file map (CMod +36, a std::map keyed by std::wstring; header node at +4,
// its parent the root, nodes: left +0, right +8, key +12, "nil" byte +69, sub_82328868), its size
// and its first keys in order, to see how the game spells them.
std::string NarrowGuestWString(const uint8_t* base, uint32_t str) {
  const uint32_t length = abi::ReadU32(base, str + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + abi::ogre::stl_string::kCapacity.offset);
  const uint32_t text = capacity > mods_abi::manager::kWStringInlineCapacity ? abi::ReadU32(base, str) : str;
  std::string out;
  for (uint32_t i = 0; i < length && i < 200; ++i) {
    const uint16_t c = abi::ReadU16(base, text + 2 * i);
    out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
  }
  return out;
}

void LogModFileMaps(const uint8_t* base, uint32_t manager) {
  const uint32_t list = abi::ReadU32(base, manager + mods_abi::manager::kListData.offset);
  const uint32_t count = abi::ReadU32(base, manager + mods_abi::manager::kListCount.offset);
  for (uint32_t i = 0; i < count && i < 64; ++i) {
    const uint32_t mod = abi::ReadU32(base, list + 4 * i);
    if (!mod) continue;
    const uint32_t map = mod + mods_abi::mod::kFiles.offset;
    const uint32_t head = abi::ReadU32(base, map + 4);
    const uint32_t size = abi::ReadU32(base, map + 8);
    std::vector<std::string> keys;
    std::vector<uint32_t> stack;
    uint32_t node = head ? abi::ReadU32(base, head + 4) : 0;
    auto nil = [&](uint32_t n) { return !n || n == head || base[n + 69] != 0; };
    while ((!nil(node) || !stack.empty()) && keys.size() < 8) {
      while (!nil(node)) {
        stack.push_back(node);
        node = abi::ReadU32(base, node + 0);
      }
      node = stack.back();
      stack.pop_back();
      keys.push_back("\"" + NarrowGuestWString(base, node + 12) + "\" -> \"" +
                     NarrowGuestWString(base, node + 40) + "\"");
      node = abi::ReadU32(base, node + 8);
    }
    REXLOG_INFO("mods: diagnostics: mod {} \"{}\" file map: {} keys", i,
                NarrowGuestWString(base, mod + mods_abi::mod::kFolder.offset), size);
    for (const auto& k : keys) REXLOG_INFO("mods: diagnostics:   {}", k);
  }
}
#endif

}  // namespace

void InstallMods(rex::Runtime* runtime, const std::filesystem::path& data_dir,
                 const std::filesystem::path& user_data_root) {
  if (data_dir.empty()) return;
  const std::filesystem::path folder = data_dir / "mods";
  mods::ScanResult scan = mods::ScanModsFolder(folder);
  for (const std::string& line : scan.skipped) REXLOG_WARN("mods: skipped {}", line);
  std::optional<mods::ModList> list;
  if (const auto bytes = ReadFile(folder / mods::kListFile)) {
    std::string error;
    list = mods::ParseModList(*bytes, &error);
    if (!list) REXLOG_WARN("mods: {} unreadable ({}); rebuilt from the folders", mods::kListFile, error);
  }
  g_plan = mods::PlanMods(scan, list);
  if (g_plan.list_changed && !scan.mods.empty()) {
    if (WriteFile(folder / mods::kListFile, mods::WriteModList(g_plan.list))) {
      REXLOG_INFO("mods: {} updated", mods::kListFile);
    } else {
      REXLOG_WARN("mods: cannot write {}", mods::kListFile);
    }
  }

  // The save safety net, before anything can load a save.
  const std::string fingerprint = mods::ModSetFingerprint(g_plan, scan);
  const auto previous = mods::ReadModSetRecord(data_dir / kRecordFile);
  bool record = true;
  if (mods::ShouldBackUpSaves(previous, fingerprint)) {
    char title[16];
    std::snprintf(title, sizeof(title), "%08X", game_setup::kTitleId);
    const auto backup = mods::BackUpSaves(user_data_root, title, std::chrono::system_clock::now(),
                                          [](const std::string& line) { REXLOG_INFO("{}", line); });
    // A failed copy keeps the old record, so the next start tries again.
    record = backup.result != mods::SaveBackup::Result::kFailed;
  }
  if (record && !mods::WriteModSetRecord(data_dir / kRecordFile, fingerprint)) {
    REXLOG_WARN("mods: cannot record the set of mods");
  }

  if (!mods::ModManagerNeeded(g_plan)) {
    REXLOG_INFO("mods: none in {}", folder.string());
    return;
  }
  if (!runtime || !runtime->file_system()) {
    REXLOG_ERROR("mods: no runtime file system; mods off");
    g_plan = {};
    return;
  }
  // Writable: the game compiles a mod's text .DAT into a .ADM next to it.
  for (size_t i = 0; i < g_plan.mods.size(); ++i) {
    const mods::PlannedMod& mod = g_plan.mods[i];
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(DeviceMount(i), folder / mod.folder,
                                                                    /*read_only=*/false);
    if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
        !runtime->file_system()->RegisterSymbolicLink(hooks::ModDeviceLink(i), DeviceMount(i))) {
      REXLOG_ERROR("mods: cannot mount {}; mods off", (folder / mod.folder).string());
      g_plan = {};
      return;
    }
    REXLOG_INFO("mods: {} as {} (priority {}{})", mod.folder, hooks::ModDeviceLink(i), mod.priority,
                mod.priority < 0 ? ", disabled" : "");
  }
  g_mounted = true;
}

const mods::ModPlan* MountedModPlan() {
  return g_mounted && mods::ModManagerNeeded(g_plan) ? &g_plan : nullptr;
}

void RegisterMods(PPCContext& ctx, uint8_t* base, uint32_t data_manager) {
  static bool done = false;
  if (done || !g_mounted || !mods::ModManagerNeeded(g_plan) || !data_manager) return;
  done = true;
  GuestCall call(ctx, base);
  const uint32_t manager = call.Call(mods_abi::kAlloc.address, {0, mods_abi::manager::kSize.bytes});
  if (!manager) {
    REXLOG_ERROR("mods: cannot allocate the mod manager; mods off");
    return;
  }
  mods_abi::InitModManager(base, manager);
  call.WriteU32(mods_abi::kRunicCoreInstances, call.ReadU32(mods_abi::kRunicCoreInstances) + 1);
  mods_abi::PublishModManager(base, data_manager, manager);
  for (size_t i = 0; i < g_plan.mods.size(); ++i) {
    const mods::PlannedMod& mod = g_plan.mods[i];
    const uint32_t mark = call.Mark();
    const uint32_t folder = GuestWString(call, base, hooks::ModDeviceLink(i) + "\\");
    if (!folder) {
      REXLOG_ERROR("mods: no scratch space for {}", mod.folder);
      call.Release(mark);
      continue;
    }
    call.Call(mods_abi::kAddMod.address, {manager, folder});
    call.Call(ui::kWStringDtor.address, {folder});
    call.Release(mark);
  }
  // The guest numbered the mods in registration order; the disabled ones get their priority.
  const uint32_t list = call.ReadU32(manager + mods_abi::manager::kListData.offset);
  const uint32_t count = call.ReadU32(manager + mods_abi::manager::kListCount.offset);
  for (uint32_t i = 0; i < count && i < g_plan.mods.size(); ++i) {
    const uint32_t mod = call.ReadU32(list + 4 * i);
    if (mod && g_plan.mods[i].priority < 0) {
      call.WriteU32(mod + mods_abi::mod::kPriority.offset, static_cast<uint32_t>(g_plan.mods[i].priority));
    }
  }
  REXLOG_INFO("mods: manager 0x{:08X} with {} mods, {} active", manager, count,
              mods_abi::ActiveModCount(base, manager));
#ifdef TORCHLIGHT_MODS_DIAGNOSTICS
  LogModFileMaps(base, manager);
#endif
  for (size_t i = 0; i < g_plan.mods.size(); ++i) {
    if (g_plan.mods[i].priority < 0) continue;
    const std::string location = hooks::ModDeviceLink(i) + "\\";
    if (!AddFileSystemLocation(call, location, true)) REXLOG_ERROR("mods: cannot add {}", location);
  }
}

}  // namespace torchlight::game_menu

REX_EXTERN(__imp__sub_822A68F0);

namespace torchlight::game_menu {

namespace {

// The unit save writer (guest_abi/mods.h kUnitSaveWriter) with its mod-list bug repaired
// (save_mod_list.h). Units without names (every unit when there are no mods) only call the original.
void HookUnitSave(PPCContext& ctx, uint8_t* base) {
  const uint32_t save_data = ctx.r3.u32;
  const uint32_t stream = ctx.r4.u32;
  const UnitSaveOutcome outcome = WriteUnitSave(base, save_data, stream, [&] {
    ctx.r3.u64 = save_data;
    ctx.r4.u64 = stream;
    __imp__sub_822A68F0(ctx, base);
  });
  switch (outcome.kind) {
    case UnitSaveOutcome::Kind::kNoNames:
      break;
    case UnitSaveOutcome::Kind::kRepaired:
      REXLOG_INFO("mods: save's mod list repaired ({} names)", outcome.names);
      break;
    case UnitSaveOutcome::Kind::kWrittenWithoutList:
      REXLOG_WARN("mods: FALLBACK: save's mod list {} ({} names); the unit was written again "
                  "without it, so this save records no mods",
                  mods::ToString(outcome.repair), outcome.names);
      break;
  }
}

}  // namespace

}  // namespace torchlight::game_menu

extern "C" {

static_assert(torchlight::guest_abi::mods::kUnitSaveWriter.address == 0x822A68F0u, "mods mismatch");
REX_FUNC(sub_822A68F0) { torchlight::game_menu::HookUnitSave(ctx, base); }

}  // extern "C"
