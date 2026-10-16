#include "game_menu/mods_install.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <set>
#include <mutex>
#include <map>
#include <string>
#include <vector>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>

#include "game_menu/guest_call.h"
#include "game_menu/mod_scripts.h"
#include "game_menu/save_mod_list.h"
#include "game_menu/video_menu.h"
#include "game_setup/game_files.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/mods.h"
#include "guest_abi/xinput.h"
#include "hooks/guest_path.h"
#include "live/named_texture_refresh.h"
#include "live/ui_gamepad.h"
#include "platform/platform.h"
#include "mods/class_list.h"
#include "mods/icon_atlas.h"
#include "mods/mod_list.h"
#include "mods/pad_prompts.h"
#include "save_import/pak.h"
#include "mods/save_safety.h"
#include "mods/texture_files.h"


namespace torchlight::game_menu {

namespace {

namespace abi = torchlight::guest_abi;
namespace mods_abi = torchlight::guest_abi::mods;
namespace ui = torchlight::guest_abi::game_ui;

// Each mod's folder is its own device (hooks/guest_path.h ModDeviceMount, linked as ModDeviceLink),
// numbered by its place in the plan: the guest never sees the folder's name, which may hold
// characters the kernel refuses in a path, as the Xbox does (a comma).
constexpr const char* kRecordFile = "mod_set.txt";
// Files read in place of the pak's at the same path, as if they were inside it (docs/mods.md 7k).
constexpr const char* kReplacementsFolder = "replacements";

// Set by InstallMods before the guest runs; read once by RegisterMods on the game's thread.
mods::ModPlan g_plan;
bool g_mounted = false;
// The replacements folder (docs/mods.md 7k), mounted as hooks::kReplacementsDeviceLink.
bool g_replacements = false;
// Where the frontend and the capture look for named textures (mods/texture_files.h): the
// replacements folder from InstallMods, the mods' groups and folders from RegisterMods.
mods::TextureLocations g_texture_locations;
std::filesystem::path g_mods_folder;
std::filesystem::path g_game_pak;
// The data manager the resources.cfg loader is building (NoteDataManager), for its group list.
uint32_t g_data_manager = 0;
// The active mods' resource groups (RegisterMods), to tell a mod's material script.
ModScriptGroups g_mod_groups;
// The pad prompts' textures (mods/pad_prompts.h), mounted as hooks::kPromptsDeviceLink by
// InstallPadPrompts; and whether the guest's pad reads swap confirm and cancel.
bool g_prompts = false;
std::atomic<bool> g_swap_confirm_cancel{false};  // set on SDL's event thread, read on the game's
// The merged icon atlases (mods/icon_atlas.h), written and mounted by InstallMods: the imagesets'
// keys, as the game's data loader asks for them ("MEDIA/UI/ITEMICONS/ITEMICONS16.IMAGESET"), set
// before the guest runs and read on the game's thread afterwards.
std::set<std::string> g_atlas_keys;

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

// A mod's name when it has no NAME: its folder (guest_abi mods.h, mod::kName). The constructor took
// the last part of the folder's path, which is the mod's device (tlmod<N>:); a name the game read
// from mod.dat is kept. The group (+52) is narrowed again from the name with the constructor's own
// calls, so it is what the game would have made. True when the name changed.
bool NameModAfterFolder(GuestCall& call, uint8_t* base, uint32_t mod, size_t index, const std::string& folder) {
  const uint32_t name = mod + mods_abi::mod::kName.offset;
  const uint32_t length = abi::ReadU32(base, name + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, name + abi::ogre::stl_string::kCapacity.offset);
  const uint32_t text = capacity > mods_abi::manager::kWStringInlineCapacity ? abi::ReadU32(base, name) : name;
  const std::string device = hooks::ModDeviceLink(index);
  if (length != device.size()) return false;
  for (uint32_t i = 0; i < length; ++i) {
    if (abi::ReadU16(base, text + 2 * i) != static_cast<uint8_t>(device[i])) return false;
  }
  const std::u16string wide = save_import::Utf16(folder);
  const uint32_t mark = call.Mark();
  const uint32_t chars = call.Reserve(static_cast<uint32_t>(2 * (wide.size() + 1)));
  const uint32_t source = call.Reserve(abi::ogre::stl_string::kSize.bytes);
  const uint32_t narrow = call.Reserve(abi::ogre::stl_string::kSize.bytes);
  if (!chars || !source || !narrow) {
    call.Release(mark);
    return false;
  }
  for (size_t i = 0; i <= wide.size(); ++i) {  // big-endian UTF-16, with its terminator
    const char16_t c = i < wide.size() ? wide[i] : u'\0';
    base[chars + 2 * i] = static_cast<uint8_t>(c >> 8);
    base[chars + 2 * i + 1] = static_cast<uint8_t>(c & 0xFF);
  }
  call.Call(ui::kWStringFromText.address, {source, chars});
  call.Call(mods_abi::mod::kWStringAssign.address, {name, source, 0, 0xFFFFFFFFu});
  call.Call(ui::kWStringDtor.address, {source});
  call.Call(mods_abi::mod::kNarrowWString.address, {narrow, name});
  call.Call(mods_abi::mod::kStringAssignString.address, {mod + mods_abi::mod::kResourceGroup.offset, narrow});
  call.Call(mods_abi::mod::kStringDtor.address, {narrow});
  call.Release(mark);
  return true;
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

// The replacements folder, when it holds any file: mounted read-only, its files are added to the
// pak's groups before they are initialised (HookInitialiseAll).
void MountReplacements(rex::Runtime* runtime, const std::filesystem::path& data_dir) {
  const std::filesystem::path folder = data_dir / kReplacementsFolder;
  std::error_code ec;
  bool any = false;
  for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      any = true;
      break;
    }
  }
  if (!any) return;
  if (!runtime || !runtime->file_system()) {
    REXLOG_ERROR("replacements: no runtime file system; {} not used", folder.string());
    return;
  }
  const std::string mount(hooks::kReplacementsDeviceMount);
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, folder, /*read_only=*/true);
  if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
      !runtime->file_system()->RegisterSymbolicLink(std::string(hooks::kReplacementsDeviceLink), mount)) {
    REXLOG_ERROR("replacements: cannot mount {}", folder.string());
    return;
  }
  g_replacements = true;
  g_texture_locations.replacements = folder;
  mods::SetTextureLocations(g_texture_locations);
  REXLOG_INFO("replacements: {} as {}", folder.string(), hooks::kReplacementsDeviceLink);
}

// The mods' imagesets that hide images of the game's own file, merged with it (mods/icon_atlas.h)
// into the cache folder, regenerated at every start, and mounted for the guest (HookModFileLookup)
// and the host's texture lookup.
void InstallAtlases(rex::Runtime* runtime) {
  const std::string cache = platform::CacheDir("atlases");
  if (cache.empty()) {
    REXLOG_ERROR("icon atlases: no cache folder; the mods' imagesets are used as they are");
    return;
  }
  std::vector<std::string> log;
  const auto written =
      mods::WriteMergedAtlases(g_game_pak, g_mods_folder, g_plan, cache, std::string(hooks::kAtlasDeviceLink), log);
  for (const std::string& line : log) REXLOG_INFO("icon atlases: {}", line);
  if (written.empty()) return;
  const std::string mount(hooks::kAtlasDeviceMount);
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, cache, /*read_only=*/true);
  if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
      !runtime->file_system()->RegisterSymbolicLink(std::string(hooks::kAtlasDeviceLink), mount)) {
    REXLOG_ERROR("icon atlases: cannot mount {}; the mods' imagesets are used as they are", cache);
    return;
  }
  for (const auto& atlas : written) {
    g_atlas_keys.insert(atlas.key);
    REXLOG_INFO("icon atlases: {} of {} merged with the game's ({} of the game's images kept, {} of the mod's)",
                atlas.key, atlas.mod_folder, atlas.game_only, atlas.from_mod);
  }
  REXLOG_INFO("icon atlases: {} merged, served as {}", written.size(), hooks::kAtlasDeviceLink);
  g_texture_locations.generated = cache;
  g_texture_locations.generated_device = std::string(hooks::kAtlasDeviceLink);
  mods::SetTextureLocations(g_texture_locations);
}

}  // namespace

void InstallMods(rex::Runtime* runtime, const std::filesystem::path& data_dir,
                 const std::filesystem::path& user_data_root, const std::filesystem::path& game_pak) {
  g_game_pak = game_pak;
  if (data_dir.empty()) return;
  MountReplacements(runtime, data_dir);
  const std::filesystem::path folder = data_dir / "mods";
  g_mods_folder = folder;
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
  if (g_plan.mods.size() > hooks::kMaxModDevices) {
    REXLOG_ERROR("mods: {} mods, more than {}; mods off", g_plan.mods.size(), hooks::kMaxModDevices);
    g_plan = {};
    return;
  }
  for (size_t i = 0; i < g_plan.mods.size(); ++i) {
    const mods::PlannedMod& mod = g_plan.mods[i];
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(hooks::ModDeviceMount(i), folder / mod.folder,
                                                                    /*read_only=*/false);
    if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
        !runtime->file_system()->RegisterSymbolicLink(hooks::ModDeviceLink(i), hooks::ModDeviceMount(i))) {
      REXLOG_ERROR("mods: cannot mount {}; mods off", (folder / mod.folder).string());
      g_plan = {};
      return;
    }
    REXLOG_INFO("mods: {} as {} (priority {}{})", mod.folder, hooks::ModDeviceLink(i), mod.priority,
                mod.priority < 0 ? ", disabled" : "");
  }
  g_mounted = true;
  InstallAtlases(runtime);
}

namespace {

mods::PadKind ToPadKind(platform::GamepadKind kind) {
  switch (kind) {
    case platform::GamepadKind::kXbox:
      return mods::PadKind::kXbox;
    case platform::GamepadKind::kPlayStation:
      return mods::PadKind::kPlayStation;
    case platform::GamepadKind::kNintendo:
      return mods::PadKind::kNintendo;
    case platform::GamepadKind::kOther:
      return mods::PadKind::kOther;
    default:
      return mods::PadKind::kNone;
  }
}

// The pad prompts' styles written at start (each in its own folder below the cache's prompts/), the
// one in effect, and the texture names they redraw. Changed on SDL's event thread when the pad
// changes (automatic style), read by the texture lookup through SetTextureLocations.
struct PromptStyles {
  std::mutex mutex;
  bool swap_setting = false;
  std::map<mods::PadStyle, std::filesystem::path> folders;
  std::vector<std::string> textures;
  mods::PadStyle style = mods::PadStyle::kXbox;
};
PromptStyles g_prompt_styles;

// Puts `style` in effect for the host's texture lookup and the A/B swap. Called with the mutex held.
void ApplyPromptStyle(mods::PadStyle style) {
  g_prompt_styles.style = style;
  g_swap_confirm_cancel = mods::SwapsConfirmCancel(style, g_prompt_styles.swap_setting);
  live::SetConfirmCancelSwapped(g_swap_confirm_cancel);  // the host's dialogs too
  const auto folder = g_prompt_styles.folders.find(style);
  g_texture_locations.prompts = folder == g_prompt_styles.folders.end() ? std::filesystem::path() : folder->second;
  mods::SetTextureLocations(g_texture_locations);
}

}  // namespace

void InstallPadPrompts(rex::Runtime* runtime, const std::filesystem::path& game_pak,
                       std::optional<mods::PadStyle> chosen, bool swap) {
  const mods::PadStyle start = mods::EffectivePadStyle(chosen, ToPadKind(platform::ConnectedGamepadKind()));
  const std::string cache = platform::CacheDir("prompts");
  const std::string resources = platform::ResourceDir();
  std::lock_guard lock(g_prompt_styles.mutex);
  g_prompt_styles.swap_setting = swap;
  if (cache.empty() || resources.empty()) {
    REXLOG_ERROR("pad prompts: no cache or data folder; the game's prompts");
    ApplyPromptStyle(mods::PadStyle::kXbox);
    return;
  }
  std::error_code ec;
  std::filesystem::remove_all(cache, ec);
  // A chosen style is the only one; automatic writes every style, for a pad changed while playing.
  std::vector<mods::PadStyle> styles;
  if (chosen) {
    if (*chosen != mods::PadStyle::kXbox) styles.push_back(*chosen);
  } else {
    styles = {mods::PadStyle::kPlayStation, mods::PadStyle::kNintendo};
  }
  std::set<std::string> textures;
  for (const mods::PadStyle style : styles) {
    std::vector<std::string> log;
    const std::filesystem::path folder = std::filesystem::path(cache) / std::string(mods::PadStyleName(style));
    const auto written = mods::WritePromptTextures(
        game_pak, std::filesystem::path(resources) / "data" / "ui" / "prompts", style, swap, folder, log);
    for (const std::string& line : log) REXLOG_WARN("pad prompts: {}", line);
    if (written.empty()) continue;
    g_prompt_styles.folders[style] = folder;
    for (const auto& texture : written) {
      textures.insert(texture.texture);
      REXLOG_INFO("pad prompts: {} {} with {} prompts redrawn", mods::PadStyleName(style), texture.texture,
                  texture.replaced);
    }
  }
  g_prompt_styles.textures.assign(textures.begin(), textures.end());
  // The guest reads the style it starts with (its own copy, which the Xenos mode draws); the host
  // draws from its lookup, which follows the pad.
  if (const auto folder = g_prompt_styles.folders.find(start); folder != g_prompt_styles.folders.end()) {
    const std::string mount(hooks::kPromptsDeviceMount);
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(mount, folder->second, /*read_only=*/true);
    if (runtime && runtime->file_system() && device->Initialize() &&
        runtime->file_system()->RegisterDevice(std::move(device)) &&
        runtime->file_system()->RegisterSymbolicLink(std::string(hooks::kPromptsDeviceLink), mount)) {
      g_prompts = true;
    } else {
      REXLOG_ERROR("pad prompts: cannot mount {}; the guest keeps the game's prompts", folder->second.string());
    }
  }
  ApplyPromptStyle(start);
  REXLOG_INFO("pad prompts: {} ({}){}", mods::PadStyleName(start), chosen ? "chosen" : "the connected pad's",
              g_swap_confirm_cancel ? ", confirm and cancel swapped" : "");
  if (chosen) return;
  platform::WatchGamepadKind([](platform::GamepadKind kind) {
    std::vector<std::string> changed;
    {
      std::lock_guard lock(g_prompt_styles.mutex);
      const mods::PadStyle style = mods::EffectivePadStyle(std::nullopt, ToPadKind(kind));
      if (style == g_prompt_styles.style) return;
      ApplyPromptStyle(style);
      REXLOG_INFO("pad prompts: the pad changed; {}{}", mods::PadStyleName(style),
                  g_swap_confirm_cancel ? ", confirm and cancel swapped" : "");
      changed = g_prompt_styles.textures;
    }
    live::RefreshNamedTextures(changed);
  });
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
    // The CMod just made is the list's last.
    const uint32_t made = call.ReadU32(manager + mods_abi::manager::kListCount.offset);
    const uint32_t cmod = made ? call.ReadU32(call.ReadU32(manager + mods_abi::manager::kListData.offset) + 4 * (made - 1))
                               : 0;
    if (cmod && NameModAfterFolder(call, base, cmod, i, mod.folder)) {
      REXLOG_INFO("mods: {} has no NAME in its mod.dat: named after its folder", mod.folder);
    }
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
  // As PC does (docs/mods.md 7j): each active mod's folder goes only into the mod's own resource
  // group (guest_abi mods.h, mod::kResourceGroup; OGRE makes the group when a location is added to
  // it), which is then initialised, after the game's groups and in mods.dat's order (ModPlan
  // activation), so the mod's scripts are parsed. A material named like one already defined is not
  // compiled over it (HookScriptListener), and a file OGRE does not find in the group it is asked
  // for is looked up in the other groups, which is how a mod's new files are found.
  for (const size_t i : g_plan.activation) {
    const std::string location = hooks::ModDeviceLink(i) + "\\";
    const uint32_t mod = i < count ? call.ReadU32(list + 4 * i) : 0;
    const std::string group = mod ? abi::ogre::ReadString(base, mod + mods_abi::mod::kResourceGroup.offset, 256) : "";
    if (group.empty()) {
      REXLOG_ERROR("mods: {} has no resource group; its assets are not available", location);
      continue;
    }
    if (!AddFileSystemLocation(call, location, true, group)) {
      REXLOG_ERROR("mods: cannot add {} to resource group \"{}\"", location, group);
      continue;
    }
    g_mod_groups.Add(group);
    {
      std::lock_guard lock(g_prompt_styles.mutex);  // the pad prompts change them on SDL's thread
      g_texture_locations.mods.push_back({group, g_mods_folder / g_plan.mods[i].folder, hooks::ModDeviceLink(i)});
    }
    if (InitialiseResourceGroup(call, group)) {
      REXLOG_INFO("mods: {} in resource group \"{}\", initialised", location, group);
    } else {
      REXLOG_ERROR("mods: cannot initialise resource group \"{}\"", group);
    }
  }
  // The merged icon atlases: the imageset the game is handed (HookModFileLookup) and its texture
  // are named on their device, which a location in the default group resolves.
  if (!g_atlas_keys.empty()) {
    const std::string location = std::string(hooks::kAtlasDeviceLink) + "\\";
    if (AddFileSystemLocation(call, location, true)) {
      REXLOG_INFO("icon atlases: {} in the default resource group", location);
    } else {
      REXLOG_ERROR("icon atlases: cannot add {}", location);
      g_atlas_keys.clear();
    }
  }
  std::lock_guard lock(g_prompt_styles.mutex);
  mods::SetTextureLocations(g_texture_locations);  // before any texture is loaded
}

void NoteDataManager(uint32_t data_manager) { g_data_manager = data_manager; }

const std::set<std::string>& GameImagesetNames() {
  static const std::set<std::string> names = [] {
    std::string error;
    auto names = mods::PakImagesetNames(g_game_pak, &error);
    if (!error.empty()) REXLOG_WARN("mods: the game's imagesets not read ({})", error);
    return names;
  }();
  return names;
}

std::optional<std::string> ModResourceGroup(std::string_view folder) {
  std::lock_guard lock(g_prompt_styles.mutex);
  for (const auto& mod : g_texture_locations.mods) {
    if (mod.folder.filename() == std::filesystem::path(folder)) return mod.group;
  }
  return std::nullopt;
}

namespace {

// initialiseAllResourceGroups (guest_abi game_ui.h kInitialiseAllResourceGroups), the first time:
// before the original parses the groups, the pad prompts' folder and then the replacements folder
// go into each of the data manager's groups (the pak's), after the pak. In each group its files then come before the pak's
// (the location added last wins the group's index) and its scripts are parsed after the pak's, so
// its definitions are the ones kept, as if its files were inside the pak.
void HookInitialiseAll(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*)) {
  static bool done = false;
  if (!done && (g_replacements || g_prompts) && g_data_manager) {
    done = true;
    GuestCall call(ctx, base);
    const uint32_t data = call.ReadU32(g_data_manager + ui::data_manager::kGroupsData.offset);
    const uint32_t count = call.ReadU32(g_data_manager + ui::data_manager::kGroupsCount.offset);
    // The prompts first, so the player's replacements, added after them, win.
    const std::string prompts = std::string(hooks::kPromptsDeviceLink) + "\\";
    const std::string replacements = std::string(hooks::kReplacementsDeviceLink) + "\\";
    for (uint32_t i = 0; i < count && i < 64; ++i) {
      const std::string group = abi::ogre::ReadString(base, data + i * ui::data_manager::kGroupStride, 256);
      if (group.empty()) continue;
      if (g_prompts) {
        if (AddFileSystemLocation(call, prompts, true, group)) {
          REXLOG_INFO("pad prompts: in resource group \"{}\"", group);
        } else {
          REXLOG_ERROR("pad prompts: cannot add to resource group \"{}\"", group);
        }
      }
      if (!g_replacements) continue;
      if (AddFileSystemLocation(call, replacements, true, group)) {
        REXLOG_INFO("replacements: in resource group \"{}\"", group);
      } else {
        REXLOG_ERROR("replacements: cannot add to resource group \"{}\"", group);
      }
    }
  }
  original(ctx, base);
}

uint64_t NextHandle(const uint8_t* base, uint32_t manager) {
  return abi::ReadU64(base, manager + ui::kResourceManagerNextHandle.offset);
}

// The game's script listener (guest_abi game_ui.h kScriptListenerHandleEvent), made to behave as
// PC's for our devices:
// - "createMaterial" for a mod's group: when the listener hands back a material that already
//   existed (no create, so OGRE's next handle did not move), a new one is created instead, which
//   OGRE leaves unregistered because the name exists; the script is compiled into it, and the
//   first definition (the game's) is kept, as on PC. The replacements' scripts keep the listener's
//   own behaviour: they redefine, as files inside the pak would.
// - "processResourceName" for a texture: the name without our device, as PC names it.
void HookScriptListener(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*)) {
  namespace event = ui::script_event;
  const uint32_t ev = ctx.r5.u32, retval = ctx.r6.u32;
  if ((!g_mounted && !g_replacements) || !ev) {
    original(ctx, base);
    return;
  }
  const std::string type = abi::ogre::ReadString(base, ev + event::kType.offset, 64);
  if (type == "createMaterial") {
    const bool from_mod =
        g_mod_groups.FromMod(abi::ogre::ReadString(base, ev + event::kMaterialFile.offset, 1024),
                             abi::ogre::ReadString(base, ev + event::kMaterialGroup.offset, 256));
    const uint32_t manager = abi::ReadU32(base, ui::kMaterialManagerGlobal);
    const uint64_t before = manager ? NextHandle(base, manager) : 0;
    original(ctx, base);
    const bool handled = (ctx.r3.u32 & 0xFF) != 0;
    if (!handled || !from_mod || !manager || !retval || NextHandle(base, manager) != before) return;
    const uint32_t existing = abi::ReadU32(base, retval);
    if (!existing) return;
    GuestCall call(ctx, base);
    const uint32_t out = call.Reserve(16);
    if (!out) return;
    const uint32_t create =
        call.ReadU32(call.ReadU32(manager) + ui::kResourceManagerCreate.byte_offset());
    call.Call(create, {out, manager, existing + abi::ogre::resource::kName.offset,
                       ev + event::kMaterialGroup.offset, 0, 0, 0});
    const uint32_t made = call.ReadU32(out + 4);
    if (made) call.WriteU32(retval, made);
    call.Call(ui::kMaterialPtrRelease.address, {out});
    static uint32_t kept = 0;
    if (++kept <= 3 || kept % 100 == 0) {
      REXLOG_INFO("mods: material \"{}\" already defined, the mod's ignored as on PC ({} so far)",
                  abi::ogre::ReadString(base, existing + abi::ogre::resource::kName.offset, 256), kept);
    }
    return;
  }
  original(ctx, base);
  if (type != "processResourceName" ||
      abi::ReadU32(base, ev + event::kResourceKind.offset) != event::kTextureKind) {
    return;
  }
  const uint32_t name = ev + event::kResourceName.offset;
  const auto stripped = hooks::WithoutOurDevice(abi::ogre::ReadString(base, name, 1024));
  if (!stripped) return;
  GuestCall call(ctx, base);
  const uint32_t text = call.PushUtf8(*stripped);
  if (text) call.Call(ui::kStringAssign.address, {name, text, static_cast<uint32_t>(stripped->size())});
}

}  // namespace

}  // namespace torchlight::game_menu

REX_EXTERN(__imp__sub_823AA988);
REX_EXTERN(__imp__sub_821FEA58);
REX_EXTERN(__imp__sub_821FEA68);
REX_EXTERN(__imp__sub_8239B490);
REX_EXTERN(__imp__sub_82429D78);
REX_EXTERN(__imp__sub_822A68F0);

namespace torchlight::game_menu {

namespace {

// A guest std::wstring's text (ASCII paths; anything else as '?').
std::string ReadAsciiWString(const uint8_t* base, uint32_t str) {
  const uint32_t length = abi::ReadU32(base, str + abi::ogre::stl_string::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + abi::ogre::stl_string::kCapacity.offset);
  if (length > 1024) return {};
  const uint32_t text = capacity > mods_abi::manager::kWStringInlineCapacity ? abi::ReadU32(base, str) : str;
  std::string out;
  for (uint32_t i = 0; i < length; ++i) {
    const uint16_t c = uint16_t(base[text + 2 * i] << 8 | base[text + 2 * i + 1]);
    out += c < 0x80 ? char(c) : '?';
  }
  return out;
}

// The mods' file lookup (guest_abi/mods.h kModFileLookup): when a mod has an imageset that was
// merged with the game's (mods/icon_atlas.h), the game is handed the merged file on our device
// instead of the mod's. The other outputs (the mod's group) stay the mod's. Every other request
// only calls the original.
void HookModFileLookup(PPCContext& ctx, uint8_t* base) {
  const uint32_t result = ctx.r3.u32, request = ctx.r5.u32;
  __imp__sub_823AA988(ctx, base);
  if (g_atlas_keys.empty() || !result || !request) return;
  const std::string key = mods::TextureNameKey(ReadAsciiWString(base, request));
  if (!g_atlas_keys.contains(key)) return;
  if (abi::ReadU32(base, result + abi::ogre::stl_string::kLength.offset) == 0) return;  // no mod has it
  GuestCall call(ctx, base);
  const uint32_t mark = call.Mark();
  const std::string path = std::string(hooks::kAtlasDeviceLink) + "/" + key;
  const uint32_t merged = GuestWString(call, base, path);
  if (merged) {
    call.Call(mods_abi::mod::kWStringAssign.address, {result, merged, 0, 0xFFFFFFFFu});
    call.Call(ui::kWStringDtor.address, {merged});
    static std::set<std::string> told;
    if (told.insert(key).second) REXLOG_INFO("icon atlases: the game gets {}", path);
  }
  call.Release(mark);
}

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


namespace xi = torchlight::guest_abi::xinput;

// The game's pad state read (guest_abi/xinput.h kGetState): with confirm and cancel swapped
// (InstallPadPrompts), the A and B buttons trade places in what the game gets.
void HookGetState(PPCContext& ctx, uint8_t* base) {
  const uint32_t state = ctx.r4.u32;
  __imp__sub_821FEA58(ctx, base);
  if (!g_swap_confirm_cancel || !state || ctx.r3.u32 != xi::kSuccess) return;
  const uint32_t at = state + xi::kStateButtons;
  abi::WriteU16(base, at, mods::SwapButtonBits(abi::ReadU16(base, at), xi::kButtonA, xi::kButtonB));
}

// The game's keystroke read (kGetKeystroke): the same swap for the A and B keys.
void HookGetKeystroke(PPCContext& ctx, uint8_t* base) {
  const uint32_t keystroke = ctx.r5.u32;
  __imp__sub_821FEA68(ctx, base);
  if (!g_swap_confirm_cancel || !keystroke || ctx.r3.u32 != xi::kSuccess) return;
  const uint32_t at = keystroke + xi::kKeystrokeVirtualKey;
  abi::WriteU16(base, at, mods::SwapVirtualKey(abi::ReadU16(base, at), xi::kVkPadA, xi::kVkPadB));
}

}  // namespace

}  // namespace torchlight::game_menu

extern "C" {

static_assert(torchlight::guest_abi::mods::kUnitSaveWriter.address == 0x822A68F0u, "mods mismatch");
REX_FUNC(sub_822A68F0) { torchlight::game_menu::HookUnitSave(ctx, base); }

static_assert(torchlight::guest_abi::mods::kModFileLookup.address == 0x823AA988u, "mods mismatch");
REX_FUNC(sub_823AA988) { torchlight::game_menu::HookModFileLookup(ctx, base); }

static_assert(torchlight::guest_abi::game_ui::kScriptListenerHandleEvent.address == 0x8239B490u,
              "game_ui mismatch");
REX_FUNC(sub_8239B490) { torchlight::game_menu::HookScriptListener(ctx, base, __imp__sub_8239B490); }

static_assert(torchlight::guest_abi::game_ui::kInitialiseAllResourceGroups.address == 0x82429D78u,
              "game_ui mismatch");
REX_FUNC(sub_82429D78) { torchlight::game_menu::HookInitialiseAll(ctx, base, __imp__sub_82429D78); }

static_assert(torchlight::guest_abi::xinput::kGetState.address == 0x821FEA58u, "xinput mismatch");
REX_FUNC(sub_821FEA58) { torchlight::game_menu::HookGetState(ctx, base); }

static_assert(torchlight::guest_abi::xinput::kGetKeystroke.address == 0x821FEA68u, "xinput mismatch");
REX_FUNC(sub_821FEA68) { torchlight::game_menu::HookGetKeystroke(ctx, base); }


}  // extern "C"
