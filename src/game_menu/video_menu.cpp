#include "game_menu/video_menu.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/ui/windowed_app_context.h>

#include "game_menu/class_selector_model.h"
#include "game_menu/guest_call.h"
#include "game_menu/guest_message_box.h"
#include "game_menu/mod_import_message.h"
#include "game_menu/mods_install.h"
#include "game_menu/menu_strings.h"
#include "game_menu/skill_tree_model.h"
#include "game_menu/video_menu_model.h"
#include "game_menu/wide_layout.h"
#include "frontend/zip_archive.h"
#include "hooks/video_mode_hooks.h"
#include "guest_abi/game_ui.h"
#include "live/busy_notice.h"
#include "live/install.h"
#include "mods/mod_import.h"
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
// The game's layouts we change, generated from the user's pak into the local cache and served like
// a language pack's files (same paths, found first): the framed ones with their bands re-anchored
// for a frame wider than 16:9, the skill tree with a third column (skill_tree_model.h) and the
// character creation screen with labels for mods' classes (class_selector_model.h).
constexpr const char* kLayoutsDeviceMount = "\\Device\\TorchlightLayouts";
constexpr const char* kLayoutsDeviceLink = "tllayouts:";
constexpr const char* kLayoutsResourceLocation = "tllayouts:\\";
bool g_generated_layouts = false;

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
  // Per row (Row order): the window that takes the focus (bar or checkbox), its value text and its
  // label (a value row's, beside the bar; a checkbox's is inside it).
  uint32_t focus[kRowCount] = {};
  uint32_t value[kRowCount] = {};
  uint32_t label[kRowCount] = {};
  // Per section (Section order): its button and the button's text.
  uint32_t section_button[kSectionCount] = {};
  uint32_t section_text[kSectionCount] = {};
  // The section shown; kept while the game runs, as the menu is.
  Section section = Section::kVideo;
  uint32_t restart_note = 0;
  std::unique_ptr<VideoMenuModel> model;
};
Column g_column;

// Row names in the layout (tl_video_settings.uilayout), in Row order.
constexpr const char* kRowNames[kRowCount] = {"resolution",   "aspect",      "fps_cap", "msaa", "shadows",  "language",
                                              "achievements", "pad_prompts", "vsync",   "swap", "renderer", "gpu",
                                              "low_quality",  "import_mod"};
// Section button names in the layout, in Section order.
constexpr const char* kSectionNames[kSectionCount] = {"video", "controls", "extras"};
// The menu's yellow, for values and the chosen section's button; white for the others.
constexpr const char* kYellow = "FFFCD615";
constexpr const char* kWhite = "FFFFFFFF";

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

// The column shows the model: the chosen section's rows the model shows (the others hidden, which
// the menu's navigation passes over, as it passes over disabled windows), values, checkboxes and
// the restart notice.
void Refresh(GuestCall& call) {
  // The column may have been built before the backend reported what it applies.
  if (const uint32_t max_msaa = live::MaxMsaa()) g_column.model->SetMaxMsaa(max_msaa);
  const bool nintendo = platform::ConnectedGamepadKind() == platform::GamepadKind::kNintendo;
  g_column.model->SetNintendoPadConnected(nintendo);
  const VideoMenuModel& model = *g_column.model;
  for (int i = 0; i < kSectionCount; ++i) {
    SetProperty(call, g_column.section_text[i], "TextColour", Section(i) == g_column.section ? kYellow : kWhite);
  }
  for (int i = 0; i < kRowCount; ++i) {
    const Row row = Row(i);
    const char* shown = SectionOf(row) == g_column.section && model.Shown(row) ? "True" : "False";
    SetProperty(call, g_column.focus[i], "Visible", shown);
    SetProperty(call, g_column.label[i], "Visible", shown);
    const bool enabled = model.Enabled(row);
    SetProperty(call, g_column.focus[i], "Disabled", enabled ? "False" : "True");
    SetProperty(call, g_column.focus[i], "Alpha", enabled ? "1" : "0.5");
    if (IsCheckbox(row)) {
      SetProperty(call, g_column.focus[i], "Selected", model.Checked(row) ? "True" : "False");
    } else if (IsAction(row)) {
      SetProperty(call, g_column.value[i], "Text", Tr(model.Text(row)));
    } else {
      SetProperty(call, g_column.value[i], "Text", "< " + Tr(model.Text(row)) + " >");
    }
  }
  SetProperty(call, g_column.restart_note, "Visible", model.RestartNeeded() ? "True" : "False");
}

// Import Mod (mods/mod_import.h). The column starts it on the game's thread; a worker thread has
// the UI thread show the system's dialogs (they belong to it: SDL pumps its events there, and
// macOS's pickers run only on the main thread), imports with the busy notice showing its progress,
// and leaves the box with the result, which the game's thread shows from CGameUI's update
// (OnGameUiFrame) with the game's message box. The game keeps running meanwhile.
struct ModImport {
  rex::ui::WindowedAppContext* app_context = nullptr;  // set by Install
  std::atomic<bool> running{false};
  std::atomic<bool> finished{false};  // the worker left its answer below
  std::mutex mutex;
  std::optional<MenuMessage> message;  // under mutex; none when the player cancelled the picker
  bool installed = false;              // under mutex
  // Game's thread: the box being shown, or waiting to be.
  GuestMessageBox box;
  std::optional<MenuMessage> to_show;
  int show_attempts = 0;
  bool mod_imported = false;  // a mod was installed this run: the column says a restart is needed
};
ModImport g_import;

// The mods folder's parent (mods_install.h ModsFolder), empty when the mods were not set up.
std::filesystem::path ImportDataDir() {
  std::filesystem::path folder = ModsFolder();
  if (!folder.has_filename()) folder = folder.parent_path();
  return folder.empty() ? folder : folder.parent_path();
}

// The worker thread: the dialogs on the UI thread, then the import.
void RunModImport(std::filesystem::path data_dir) {
  platform::SetCurrentThreadName("Mod import");
  std::string path, error;
  platform::PickResult picked = platform::PickResult::kCancelled;
  // Where both pickers work, the player chooses; Android's picks files only (a zip).
  g_import.app_context->CallInUIThreadSynchronous([&] {
    bool folder = false;
    if (platform::CanPickFolder()) {
      const int choice = platform::AskChoice(Tr(kImportTitle), Tr(kImportAskSource),
                                             {Tr(kImportZipButton), Tr(kImportFolderButton),
                                              Tr(kImportCancelButton)});
      if (choice != 0 && choice != 1) return;
      folder = choice == 1;
    }
    picked = folder ? platform::PickFolder(Tr(kImportPickFolder), path, error)
                    : platform::PickFileUncopied(Tr(kImportPickZip), path, error,
                                                 {{Tr(kImportZipFilter), "zip"}, {Tr(kImportAllFilter), "*"}});
  });
  // Android's document is copied here, off the UI thread, with the busy notice showing it from
  // the first chunk (elsewhere nothing is copied, and no notice flashes).
  if (picked == platform::PickResult::kChosen) {
    bool shown = false;
    picked = platform::CopyPickedFile(path, error, [&shown](uint64_t done, uint64_t total) {
      if (!shown) live::ShowBusyNotice(Tr(kImportCopying));
      shown = true;
      live::SetBusyNoticeProgress(size_t(done), size_t(total));
      return true;
    });
    if (shown) live::HideBusyNotice();
  }
  std::optional<MenuMessage> message;
  bool installed = false;
  switch (picked) {
    case platform::PickResult::kCancelled:
      REXLOG_INFO("mod import: nothing chosen");
      break;
    case platform::PickResult::kFailed:
      REXLOG_WARN("mod import: no picker: {}", error);
      message = ModPickMessage(PickProblem::kNoPicker, error, Tr);
      break;
    case platform::PickResult::kUncopied:
      REXLOG_WARN("mod import: the chosen file was not copied: {}", error);
      message = ModPickMessage(PickProblem::kNotCopied, error, Tr);
      break;
    case platform::PickResult::kChosen: {
      REXLOG_INFO("mod import: from {}", path);
      live::ShowBusyNotice(Tr(kImportBusy));
      const mods::ImportResult result =
          mods::ImportMod(path, data_dir, [](const mods::ImportProgress& progress) {
            live::SetBusyNoticeProgress(size_t(progress.bytes_done), size_t(progress.bytes_total));
          });
      live::HideBusyNotice();
      for (const std::string& skipped : result.skipped) {
        REXLOG_INFO("mod import: left out {} (not part of a mod)", skipped);
      }
      if (result.ok()) REXLOG_INFO("mod import: {}", result.message());
      else REXLOG_WARN("mod import: {}", result.message());
      installed = result.ok();
      message = ModImportMessage(result, Tr);
      break;
    }
  }
  // Android's copy of the chosen document, used now.
  platform::ForgetPickedFiles();
  {
    std::lock_guard lock(g_import.mutex);
    g_import.message = std::move(message);
    g_import.installed = installed;
  }
  g_import.finished = true;
}

// A on Import Mod (game's thread).
void StartModImport() {
  const std::filesystem::path data_dir = ImportDataDir();
  if (!g_import.app_context || data_dir.empty() || g_import.running.exchange(true)) return;
  g_import.finished = false;
  // Detached: it ends by itself, and a game closed meanwhile does not wait for the picker. What an
  // import interrupted that way leaves is a .partial folder, never loaded as a mod.
  std::thread(RunModImport, data_dir).detach();
}

}  // namespace

namespace {

// Writes the layouts of the user's pak we change under `dir`/media/UI/: the framed ones with their
// bands re-anchored when `widen`, the skill tree with its third column and the creation screen with
// its class labels; how many. Nothing of the
// game is kept in the repository: this runs on the user's own data.
// `path`'s file name is `file`, in any case.
bool IsNamed(const std::string& path, std::string_view file) {
  const std::string name = path.substr(path.find_last_of("/\\") + 1);
  return name.size() == file.size() &&
         std::equal(name.begin(), name.end(), file.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
         });
}

int WriteLayouts(const std::filesystem::path& pak_path, const std::filesystem::path& dir, bool widen) {
  frontend::ZipArchive pak;
  std::string error;
  if (!pak.Open(pak_path.string(), error)) {
    REXLOG_ERROR("layouts: {}", error);
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
    std::string xml(data->begin(), data->end());
    std::vector<std::string> changes;
    if (widen) {
      if (auto widened = WidenBands(xml)) {
        xml = std::move(widened->xml);
        changes.push_back(std::to_string(widened->windows.size()) + " band end(s) re-anchored");
      }
    }
    if (auto columns = skill_tree::AddSkillColumn(xml)) {
      xml = std::move(*columns);
      changes.push_back("skill tree third column");
    }
    if (auto labels = IsNamed(name, ui::kNewGameMenuLayout) ? class_selector::AddClassLabels(xml)
                                                             : std::nullopt) {
      xml = std::move(*labels);
      changes.push_back("class button labels");
    }
    if (changes.empty()) continue;
    const std::string file = name.substr(name.find_last_of("/\\") + 1);
    std::ofstream out(dir / "media" / "UI" / file, std::ios::binary);
    out << xml;
    if (!out) {
      REXLOG_ERROR("layouts: cannot write {}", file);
      return -1;
    }
    std::string list;
    for (const std::string& change : changes) list += (list.empty() ? "" : ", ") + change;
    REXLOG_INFO("layouts: {} ({})", file, list);
    ++written;
  }
  return written;
}

void InstallGeneratedLayouts(rex::Runtime* runtime, const std::filesystem::path& game_data_root) {
  const std::string cache = platform::CacheDir("layouts");
  if (cache.empty()) {
    REXLOG_ERROR("layouts: no cache folder; menus keep their 16:9 frames and two skill columns");
    return;
  }
  if (WriteLayouts(game_data_root / "pak.zip", cache, hooks::WiderThan16x9()) <= 0) return;
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kLayoutsDeviceMount, cache, true);
  if (!device->Initialize() || !runtime->file_system()->RegisterDevice(std::move(device)) ||
      !runtime->file_system()->RegisterSymbolicLink(kLayoutsDeviceLink, kLayoutsDeviceMount)) {
    REXLOG_ERROR("layouts: cannot mount {}", cache);
    return;
  }
  g_generated_layouts = true;
}

}  // namespace

void Install(rex::Runtime* runtime, const std::filesystem::path& game_data_root,
             rex::ui::WindowedAppContext* app_context) {
  g_import.app_context = app_context;
  g_import.box.set_runtime(runtime);
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
  InstallGeneratedLayouts(runtime, game_data_root);
}

// One FileSystem location in a group (the default one unless told); false on failure (video_menu.h).
bool AddFileSystemLocation(GuestCall& call, const std::string& path, bool recursive, const std::string& group_name) {
  const uint32_t manager = call.ReadU32(ui::kResourceGroupManagerGlobal);
  const uint32_t mark = call.Mark();
  const uint32_t name = call.StdString(path);
  const uint32_t type = call.StdString(kResourceType);
  const uint32_t group = call.StdString(group_name);
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

// One resource group initialised; false on failure (video_menu.h).
bool InitialiseResourceGroup(GuestCall& call, const std::string& group_name) {
  const uint32_t manager = call.ReadU32(ui::kResourceGroupManagerGlobal);
  const uint32_t mark = call.Mark();
  const uint32_t group = call.StdString(group_name);
  const bool ok = manager && group;
  if (ok) call.Call(ui::kInitialiseResourceGroup.address, {manager, group});
  call.DestroyStdString(group);
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
  if (g_generated_layouts) {
    if (AddFileSystemLocation(call, kLayoutsResourceLocation, true)) {
      REXLOG_INFO("layouts: resource location {} added", kLayoutsResourceLocation);
    } else {
      REXLOG_ERROR("layouts: cannot add {}", kLayoutsResourceLocation);
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
    g_column.label[i] = FindChild(call, column, (row + "/label").c_str());
  }
  for (int i = 0; i < kSectionCount; ++i) {
    const std::string button = std::string("tl_video/section_") + kSectionNames[i];
    g_column.section_button[i] = FindChild(call, column, button.c_str());
    g_column.section_text[i] = FindChild(call, column, (button + "/text").c_str());
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
      {"tl_video/section_video/text", "Video"},
      {"tl_video/section_controls/text", "Controls"},
      {"tl_video/section_extras/text", "Extras"},
      {"tl_video/resolution/label", "Resolution"},
      {"tl_video/aspect/label", "Aspect Ratio"},
      {"tl_video/fps_cap/label", "Frame Rate Limit"},
      {"tl_video/msaa/label", "Antialiasing"},
      {"tl_video/shadows/label", "Shadows"},
      {"tl_video/low_quality/label", "Low-End Devices"},
      {"tl_video/language/label", "Language"},
      {"tl_video/achievements/label", "Achievements"},
      {"tl_video/vsync/label", "Vertical Sync"},
      {"tl_video/pad_prompts/label", "Button Prompts"},
      {"tl_video/swap/label", "Swap A/B"},
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
  // Import Mod: with the UI thread for the pickers and a mods folder (InstallMods ran).
  g_column.model->SetImportAvailable(g_import.app_context && !ImportDataDir().empty());
  // Not in a gamescope session (the Deck's Game Mode): the desktop's picker may open unseen there.
  g_column.model->SetImportHidden(platform::InGamescopeSession());
  // The column is made anew each time the menu opens: an import still running, or one done this run.
  g_column.model->SetImportRunning(g_import.running);
  if (g_import.mod_imported) g_column.model->NoteModImported();
  Refresh(call);
  REXLOG_INFO("game menu: video settings 0x{:08X} added to the settings menu 0x{:08X}", column,
              settings_menu);
}

// A navigation event in the settings menu: ours when the focus is on one of our rows or section
// buttons. A on a section button shows that section; left and right move a value row, A toggles a
// checkbox; the settings are applied and saved at once, as the menu's own sliders take effect while
// it is open. Returns false to let the game handle it.
bool OnSettingsNavigation(PPCContext& ctx, uint8_t* base, uint32_t handler, uint32_t event) {
  if (!g_column.model) return false;
  GuestCall call(ctx, base);
  if (call.ReadU32(handler + ui::navigation_handler::kMenu.offset) != g_column.settings_menu) {
    return false;
  }
  const uint32_t window = call.ReadU32(event + ui::navigation::kEventWindow.offset);
  const uint32_t action = call.ReadU32(event + ui::navigation::kEventAction.offset);
  for (int i = 0; i < kSectionCount; ++i) {
    if (!window || window != g_column.section_button[i]) continue;
    if (action != ui::navigation::kActionAccept) return false;
    if (g_column.section != Section(i)) {
      g_column.section = Section(i);
      Refresh(call);
    }
    return true;
  }
  int row = -1;
  for (int i = 0; i < kRowCount; ++i) {
    if (window && window == g_column.focus[i]) row = i;
  }
  if (row < 0) return false;
  if (const uint32_t max_msaa = live::MaxMsaa()) g_column.model->SetMaxMsaa(max_msaa);
  bool changed = false;
  if (action == ui::navigation::kActionLeft || action == ui::navigation::kActionRight) {
    changed = g_column.model->Step(Row(row), action == ui::navigation::kActionLeft ? -1 : +1);
  } else if (action == ui::navigation::kActionAccept && IsAction(Row(row))) {
    if (g_column.model->Activate(Row(row))) {
      StartModImport();
      g_column.model->SetImportRunning(g_import.running);
      Refresh(call);
    }
    return true;
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

void OnGameUiFrame(PPCContext& ctx, uint8_t* base) {
  if (g_import.finished.exchange(false)) {
    std::lock_guard lock(g_import.mutex);
    g_import.to_show = std::move(g_import.message);
    g_import.message.reset();
    g_import.show_attempts = 0;
    if (g_import.installed) g_import.mod_imported = true;
    if (g_column.model) {
      if (g_import.installed) g_column.model->NoteModImported();
      g_column.model->SetImportRunning(false);
    }
    g_import.running = false;
    if (g_column.model) {
      GuestCall call(ctx, base);
      Refresh(call);
    }
  }
  if (g_import.box.open()) {
    g_import.box.Poll(base);  // OK is its only button: nothing to do with the answer
    return;
  }
  if (!g_import.to_show) return;
  // Another box may be open for a while (the game's own): tried again about every second, ten
  // times, and then the log keeps the result.
  if (g_import.show_attempts++ % 60 != 0) return;
  const MenuMessage& message = *g_import.to_show;
  if (g_import.box.Show(ctx, base, message.title, WrapText(message.text, kBoxLineLength), message.buttons, 0)) {
    g_import.to_show.reset();
  } else if (g_import.show_attempts > 10 * 60) {
    REXLOG_WARN("mod import: the result box could not be shown: {}", message.text);
    g_import.to_show.reset();
  }
}

}  // namespace torchlight::game_menu

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(ui::entry.address == 0x##addr##u, "game_ui mismatch")

extern "C" {

FUNCTION_ADDRESS_CHECK(kResourcesCfgLoader, 8239B998);
REX_EXTERN(__imp__sub_8239B998);
REX_FUNC(sub_8239B998) {
  const uint32_t data_manager = ctx.r3.u32;
  torchlight::game_menu::NoteDataManager(data_manager);
  __imp__sub_8239B998(ctx, base);
  torchlight::game_menu::AddResourceLocations(ctx, base);
  // The player's mods (mods_install.h): each in its own resource group, initialised after the
  // game's groups, as on PC.
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
