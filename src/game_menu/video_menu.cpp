#include "game_menu/video_menu.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>

#include "game_menu/guest_call.h"
#include "game_menu/mods_install.h"
#include "game_menu/menu_strings.h"
#include "game_menu/video_menu_model.h"
#include "game_menu/wide_layout.h"
#include "frontend/zip_archive.h"
#include "hooks/video_mode_hooks.h"
#include "guest_abi/game_ui.h"
#include "live/install.h"
#include "platform/platform.h"

namespace ui = torchlight::guest_abi::game_ui;

namespace torchlight::game_menu {

namespace {

// The layouts as the guest sees them: a VFS device on data/ui/ next to the executable, linked as
// "tlhost:", added to OGRE's resources as a FileSystem location. OGRE 1.7 finds a file in any
// group (game_ui.h kResourceGroupManagerGlobal), so the default group does.
constexpr const char* kDeviceMount = "\\Device\\TorchlightData";
constexpr const char* kDeviceLink = "tlhost:";
constexpr const char* kResourceLocation = "tlhost:\\";
constexpr const char* kResourceType = "FileSystem";
constexpr const char* kResourceGroup = "General";
constexpr const char* kVideoLayout = "tl_video_settings.uilayout";
constexpr const char* kStringsFile = "tl_video_strings.txt";
// The game's framed layouts with their bands re-anchored for a frame wider than 16:9, generated
// into the local cache and served like a language pack's files (same paths, found first).
constexpr const char* kWideDeviceMount = "\\Device\\TorchlightWideLayouts";
constexpr const char* kWideDeviceLink = "tlwide:";
constexpr const char* kWideResourceLocation = "tlwide:\\";
bool g_wide_layouts = false;

// Our translations (loaded by Install) and the game's language: the console language the runtime
// reports, read when the column is built (the host settings set it before the guest runs).
MenuStrings g_strings;
std::string g_language = "en";
std::string Tr(const std::string& english) { return g_strings.Translate(g_language, english); }

// Set by Install on the main thread before the guest runs; read by the hooks on the game's thread.
std::atomic<bool> g_enabled{false};

// The column in the game's settings menu, built and used only on the game's thread (its hooks).
struct Column {
  uint32_t settings_menu = 0;
  // Per row (Row order): the window that takes the focus (bar or checkbox) and its value text.
  uint32_t focus[kRowCount] = {};
  uint32_t value[kRowCount] = {};
  uint32_t restart_note = 0;
  std::unique_ptr<VideoMenuModel> model;
};
Column g_column;

// Row names in the layout (tl_video_settings.uilayout), in Row order.
constexpr const char* kRowNames[kRowCount] = {"resolution", "aspect",   "fps_cap",
                                              "language",   "achievements", "vsync",
                                              "renderer",   "gpu"};

// Loads one of our layouts (CEGUI WindowManager::loadWindowLayout, as the game's menus do);
// returns its root window, 0 on failure.
uint32_t LoadLayout(GuestCall& call, const char* file) {
  const uint32_t window_manager = call.ReadU32(ui::kWindowManagerGlobal);
  if (!window_manager) return 0;
  const uint32_t mark = call.Mark();
  const uint32_t name = call.CeguiString(file);
  const uint32_t root =
      name ? call.Call(ui::kLoadWindowLayout.address, {window_manager, name}) : 0;
  call.DestroyCeguiString(name);
  call.Release(mark);
  return root;
}

// A child window by name (0 when absent).
uint32_t FindChild(GuestCall& call, uint32_t window, const char* child) {
  const uint32_t mark = call.Mark();
  const uint32_t name = call.CeguiString(child);
  const uint32_t found = name ? call.Call(ui::kFindChildWindow.address, {window, name}) : 0;
  call.DestroyCeguiString(name);
  call.Release(mark);
  return found;
}

void SetProperty(GuestCall& call, uint32_t window, const char* name, const std::string& value) {
  if (!window) return;
  const uint32_t mark = call.Mark();
  const uint32_t n = call.CeguiString(name);
  const uint32_t v = call.CeguiString(value);
  if (n && v) call.Call(ui::kWindowSetProperty.address, {window, n, v});
  call.DestroyCeguiString(v);
  call.DestroyCeguiString(n);
  call.Release(mark);
}

// The column shows the model: values, the checkbox and the restart notice.
void Refresh(GuestCall& call) {
  const VideoMenuModel& model = *g_column.model;
  for (int i = 0; i < kRowCount; ++i) {
    const Row row = Row(i);
    const bool enabled = model.Enabled(row);
    SetProperty(call, g_column.focus[i], "Disabled", enabled ? "False" : "True");
    SetProperty(call, g_column.focus[i], "Alpha", enabled ? "1" : "0.5");
    if (row == Row::kVsync) {
      SetProperty(call, g_column.focus[i], "Selected", model.Checked(row) ? "True" : "False");
    } else {
      SetProperty(call, g_column.value[i], "Text", "< " + Tr(model.Text(row)) + " >");
    }
  }
  SetProperty(call, g_column.restart_note, "Visible", model.RestartNeeded() ? "True" : "False");
}

}  // namespace

namespace {

// Writes the framed layouts of the user's pak with their bands re-anchored under `dir`/media/UI/;
// how many. Nothing of the game is kept in the repository: this runs on the user's own data.
int WriteWideLayouts(const std::filesystem::path& pak_path, const std::filesystem::path& dir) {
  frontend::ZipArchive pak;
  std::string error;
  if (!pak.Open(pak_path.string(), error)) {
    REXLOG_ERROR("wide layouts: {}", error);
    return -1;
  }
  std::error_code ec;
  std::filesystem::remove_all(dir / "media", ec);
  std::filesystem::create_directories(dir / "media" / "UI", ec);
  int written = 0;
  for (const std::string& name : pak.names()) {
    if (name.size() < 9 || name.compare(name.size() - 9, 9, ".uilayout") != 0) continue;
    const auto data = pak.Read(name);
    if (!data) continue;
    const auto widened = WidenBands(std::string(data->begin(), data->end()));
    if (!widened) continue;
    const std::string file = name.substr(name.find_last_of("/\\") + 1);
    std::ofstream out(dir / "media" / "UI" / file, std::ios::binary);
    out << widened->xml;
    if (!out) {
      REXLOG_ERROR("wide layouts: cannot write {}", file);
      return -1;
    }
    REXLOG_INFO("wide layouts: {} ({} band end(s) re-anchored)", file, widened->windows.size());
    ++written;
  }
  return written;
}

void InstallWideLayouts(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (!hooks::WiderThan16x9()) return;
  const std::string cache = platform::CacheDir("layouts");
  if (cache.empty()) {
    REXLOG_ERROR("wide layouts: no cache folder; menus keep their 16:9 frames");
    return;
  }
  if (WriteWideLayouts(game_data_root / "pak.zip", cache) <= 0) return;
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kWideDeviceMount, cache, true);
  if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
      !runtime->file_system()->RegisterSymbolicLink(kWideDeviceLink, kWideDeviceMount)) {
    REXLOG_ERROR("wide layouts: cannot mount {}", cache);
    return;
  }
  g_wide_layouts = true;
}

}  // namespace

void Install(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  if (!runtime || !runtime->file_system()) {
    REXLOG_ERROR("game menu: no runtime file system; video menu off");
    return;
  }
  const std::string resources = platform::ResourceDir();
  const std::filesystem::path data = std::filesystem::path(resources) / "data" / "ui";
  std::error_code ec;
  if (resources.empty() || !std::filesystem::exists(data / kVideoLayout, ec)) {
    REXLOG_ERROR("game menu: layouts not found in {}; video menu off", data.string());
    return;
  }
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kDeviceMount, data, true);
  if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
      !runtime->file_system()->RegisterSymbolicLink(kDeviceLink, kDeviceMount)) {
    REXLOG_ERROR("game menu: cannot mount {}; video menu off", data.string());
    return;
  }
  std::vector<std::string> warnings;
  std::string error;
  if (!g_strings.Load((data / kStringsFile).string(), warnings, error)) {
    REXLOG_WARN("game menu: {}; video settings in English", error);
  }
  for (const std::string& warning : warnings) REXLOG_WARN("game menu: {}: {}", kStringsFile, warning);
  g_enabled = true;
  REXLOG_INFO("game menu: layouts from {} as {}", data.string(), kDeviceLink);
  InstallWideLayouts(runtime, game_data_root);
}

// One FileSystem location in the default group; false on failure (video_menu.h).
bool AddFileSystemLocation(GuestCall& call, const std::string& path, bool recursive) {
  const uint32_t manager = call.ReadU32(ui::kResourceGroupManagerGlobal);
  const uint32_t mark = call.Mark();
  const uint32_t name = call.StdString(path);
  const uint32_t type = call.StdString(kResourceType);
  const uint32_t group = call.StdString(kResourceGroup);
  const bool ok = manager && name && type && group;
  if (ok) {
    call.Call(ui::kAddResourceLocation.address, {manager, name, type, group, recursive ? 1u : 0u});
  }
  call.DestroyStdString(group);
  call.DestroyStdString(type);
  call.DestroyStdString(name);
  call.Release(mark);
  return ok;
}

namespace {

// After the game's resources.cfg locations (once): our layouts (only mode), and a language pack's
// folder (any mode), recursive so its files keep their game paths (media/UI/...). The game's own
// files are in the resources.cfg groups; a file in the default group is found first when asked for
// there, which is how a pack's fonts take the place of the game's.
void AddResourceLocations(PPCContext& ctx, uint8_t* base) {
  static bool added = false;
  if (added) return;
  added = true;
  GuestCall call(ctx, base);
  if (g_enabled) {
    if (AddFileSystemLocation(call, kResourceLocation, false)) {
      REXLOG_INFO("game menu: resource location {} added", kResourceLocation);
    } else {
      REXLOG_ERROR("game menu: cannot add the resource location; video menu off");
      g_enabled = false;
    }
  }
  if (g_wide_layouts) {
    if (AddFileSystemLocation(call, kWideResourceLocation, true)) {
      REXLOG_INFO("wide layouts: resource location {} added", kWideResourceLocation);
    } else {
      REXLOG_ERROR("wide layouts: cannot add {}", kWideResourceLocation);
    }
  }
  const std::string pack = live::LanguagePack();
  if (!pack.empty()) {
    const std::string folder = "game:\\translations\\" + pack + "\\";
    if (AddFileSystemLocation(call, folder, true)) {
      REXLOG_INFO("language pack: resource location {} added", folder);
    } else {
      REXLOG_ERROR("language pack: cannot add {}", folder);
    }
  }
}

// The game's settings menu was built: our video column goes on its right, in the container that
// holds the menu's rows (the grandparent of "Storage"), before the menu is first opened (that is
// when its tab order is collected).
void AddVideoColumn(PPCContext& ctx, uint8_t* base, uint32_t settings_menu) {
  // The layout's windows have fixed names (the guest's unique-name generator clashes), so it can be
  // loaded once per process. CGameUI builds its menus once and never again (its creator only runs
  // with an empty slot, game_ui.h kGameUiCreateMenus; one settings menu per run in the logs).
  static bool added = false;
  if (added) {
    REXLOG_WARN("game menu: settings menu 0x{:08X} built again; video settings not added twice",
                settings_menu);
    return;
  }
  added = true;
  GuestCall call(ctx, base);
  const uint32_t root = call.ReadU32(settings_menu + ui::dropdown_menu::kRoot.offset);
  if (!root) return;
  const uint32_t storage = FindChild(call, root, "Storage");
  const uint32_t storage_parent = storage ? call.ReadU32(storage + ui::kWindowParent.offset) : 0;
  const uint32_t container =
      storage_parent ? call.ReadU32(storage_parent + ui::kWindowParent.offset) : 0;
  if (!container) {
    REXLOG_ERROR("game menu: the settings menu has no Storage row; video settings not added");
    return;
  }
  const uint32_t column = LoadLayout(call, kVideoLayout);
  if (!column) {
    REXLOG_ERROR("game menu: {} did not load", kVideoLayout);
    return;
  }
  // As the game prepares the menu's own layout (kSettingsMenuInit): scaled for the screen's
  // aspect and its texts translated.
  const uint32_t game_ui = call.ReadU32(settings_menu + ui::dropdown_menu::kGameUi.offset);
  call.Call(ui::kGameUiScaleLayout.address, {game_ui, column, 0});
  call.Call(ui::kAddChildWindow.address, {container, column});

  // The rows by name, and the model over the host settings.
  g_column.settings_menu = settings_menu;
  for (int i = 0; i < kRowCount; ++i) {
    const std::string row = std::string("tl_video/") + kRowNames[i];
    g_column.focus[i] = FindChild(call, column, row.c_str());
    g_column.value[i] = FindChild(call, column, (row + "/value").c_str());
  }
  g_column.restart_note = FindChild(call, column, "tl_video/restart_note");
  // Labels in the game's language (our translations; the layout has the English texts): the
  // runtime's console language, set from the host settings or by --user_language before the guest
  // ran.
  const std::string pack = live::LanguagePack();
  g_language =
      pack.empty() ? settings::LanguageCode(rex::cvar::Query<uint32_t>("user_language")) : pack;
  REXLOG_INFO("game menu: video settings in language {}", g_language);
  static const std::pair<const char*, const char*> kLabels[] = {
      {"tl_video/header", "Video"},
      {"tl_video/resolution/label", "Resolution"},
      {"tl_video/aspect/label", "Aspect Ratio"},
      {"tl_video/fps_cap/label", "Frame Rate Limit"},
      {"tl_video/language/label", "Language"},
      {"tl_video/achievements/label", "Achievements"},
      {"tl_video/vsync/label", "Vertical Sync"},
      {"tl_video/renderer/label", "Renderer"},
      {"tl_video/gpu/label", "GPU"},
      {"tl_video/restart_note", "Some changes need a restart"},
  };
  for (const auto& [name, english] : kLabels) {
    SetProperty(call, FindChild(call, column, name), "Text", Tr(english));
  }
  g_column.model = std::make_unique<VideoMenuModel>(live::HostCapabilities(),
                                                    live::CurrentHostSettings(),
                                                    live::StartupHostSettings(), g_language);
  Refresh(call);
  REXLOG_INFO("game menu: video settings 0x{:08X} added to the settings menu 0x{:08X}", column,
              settings_menu);
}

// A navigation event in the settings menu: ours when the focus is on one of our rows. Left and
// right move a value row, A toggles the checkbox; the settings are applied and saved at once, as
// the menu's own sliders take effect while it is open. Returns false to let the game handle it.
bool OnSettingsNavigation(PPCContext& ctx, uint8_t* base, uint32_t handler, uint32_t event) {
  if (!g_column.model) return false;
  GuestCall call(ctx, base);
  if (call.ReadU32(handler + ui::navigation_handler::kMenu.offset) != g_column.settings_menu) {
    return false;
  }
  const uint32_t window = call.ReadU32(event + ui::navigation::kEventWindow.offset);
  const uint32_t action = call.ReadU32(event + ui::navigation::kEventAction.offset);
  int row = -1;
  for (int i = 0; i < kRowCount; ++i) {
    if (window && window == g_column.focus[i]) row = i;
  }
  if (row < 0) return false;
  bool changed = false;
  if (action == ui::navigation::kActionLeft || action == ui::navigation::kActionRight) {
    changed = g_column.model->Step(Row(row), action == ui::navigation::kActionLeft ? -1 : +1);
  } else if (action == ui::navigation::kActionAccept) {
    changed = g_column.model->Toggle(Row(row));
  } else {
    return false;  // back, close and the rest: the menu's own handling
  }
  if (changed) {
    live::ChangeHostSettings(g_column.model->settings());
    Refresh(call);
  }
  return true;
}

// The menu's "Reset Defaults" resets the video settings too.
void OnSettingsResetDefaults(PPCContext& ctx, uint8_t* base, uint32_t settings_menu) {
  if (!g_column.model || settings_menu != g_column.settings_menu) return;
  if (!g_column.model->Reset()) return;
  live::ChangeHostSettings(g_column.model->settings());
  GuestCall call(ctx, base);
  Refresh(call);
}

}  // namespace

}  // namespace torchlight::game_menu

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(ui::entry.address == 0x##addr##u, "game_ui mismatch")

extern "C" {

FUNCTION_ADDRESS_CHECK(kResourcesCfgLoader, 8239B998);
REX_EXTERN(__imp__sub_8239B998);
REX_FUNC(sub_8239B998) {
  const uint32_t data_manager = ctx.r3.u32;
  __imp__sub_8239B998(ctx, base);
  torchlight::game_menu::AddResourceLocations(ctx, base);
  // The player's mods (mods_install.h), after the locations above: the last one added wins.
  torchlight::game_menu::RegisterMods(ctx, base, data_manager);
}

FUNCTION_ADDRESS_CHECK(kSettingsMenuInit, 823802F8);
REX_EXTERN(__imp__sub_823802F8);
REX_FUNC(sub_823802F8) {
  const uint32_t settings_menu = ctx.r3.u32;
  __imp__sub_823802F8(ctx, base);
  if (torchlight::game_menu::g_enabled) {
    torchlight::game_menu::AddVideoColumn(ctx, base, settings_menu);
  }
}

FUNCTION_ADDRESS_CHECK(kSettingsNavigationOnEvent, 8237FD30);
REX_EXTERN(__imp__sub_8237FD30);
REX_FUNC(sub_8237FD30) {
  if (torchlight::game_menu::g_enabled &&
      torchlight::game_menu::OnSettingsNavigation(ctx, base, ctx.r3.u32, ctx.r4.u32)) {
    ctx.r3.u64 = 1;  // handled, as the original returns for the menu's own widgets
    return;
  }
  __imp__sub_8237FD30(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kSettingsMenuResetDefaults, 82380F88);
REX_EXTERN(__imp__sub_82380F88);
REX_FUNC(sub_82380F88) {
  const uint32_t settings_menu = ctx.r3.u32;
  __imp__sub_82380F88(ctx, base);
  if (torchlight::game_menu::g_enabled) {
    torchlight::game_menu::OnSettingsResetDefaults(ctx, base, settings_menu);
  }
}

}  // extern "C"
