// The in-game import of Torchlight PC saves: hooks on the "load character" menu (CContinueGameMenu,
// guest_abi/save_menu.h) and the start-up step. See docs/saves-research.md, section 4d.
//
// Opening the menu: if the import folder holds anything new, the save container is read through
// the game's own mount (file names, stashes, settings, and its host path), and a host thread reads
// the paks and converts. Then, from the menu's Update (every frame, on the game's thread), a XAM
// message box asks; it never blocks the guest thread: its memory is on the system heap and its
// XOVERLAPPED is polled. On "Yes" the characters are written through the game's mount and the list
// is rebuilt as the game does after deleting a character; the stash and settings are left for the
// next start (InstallSaveImport).

#include "game_menu/save_import_menu.h"

#include <cctype>
#include <chrono>
#include <cstring>
#include <future>
#include <optional>
#include <span>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/system/xtypes.h>

#include "game_menu/guest_call.h"
#include "game_menu/menu_strings.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/save_menu.h"
#include "live/install.h"
#include "platform/platform.h"
#include "platform/user_folders.h"
#include "mods/save_units_install.h"
#include "mods/save_units_notice.h"
#include "save_import/backup_retention.h"
#include "save_import/import_message.h"
#include "save_import/import_plan.h"
#include "save_import/pak.h"
#include "settings/host_settings.h"

// The runtime's XamShowMessageBoxUI export (the import thunk's target), called directly: GuestCall
// passes eight arguments, and this one takes nine.
extern "C" REX_FUNC(__imp__XamShowMessageBoxUI);

namespace torchlight::game_menu {
namespace {

namespace fs = std::filesystem;
namespace abi = torchlight::guest_abi;
using rex::X_RESULT;  // the X_ERROR_* and X_STATUS_* macros expand to them
using rex::X_STATUS;
namespace menu = torchlight::guest_abi::save_menu;
namespace si = torchlight::save_import;

constexpr const char* kSaveRoot = "SAVE:\\";
constexpr const char* kStringsFile = "tl_import_strings.txt";

enum class Stage { kOff, kIdle, kNotice, kPlanning, kAsking, kAskingStash };

// A message box shown with XamShowMessageBoxUI: its texts, buttons, result and XOVERLAPPED in one
// block of the system heap, alive until the box closes.
struct Box {
  uint32_t block = 0, result = 0, overlapped = 0;
};

struct State {
  rex::Runtime* runtime = nullptr;
  fs::path folder, game_pak;
  std::unique_ptr<si::Schema> schema;
  MenuStrings strings;
  std::string language = "en";
  Stage stage = Stage::kOff;
  bool asked_this_session = false;
  bool import_on = false;     // InstallSaveImport set the import up
  bool notice_shown = false;  // the saves' removed items (mods/save_units_notice.h)

  si::ImportFolder scanned;
  si::ContainerView view;
  fs::path container;
  std::future<si::ImportPlan> planning;
  si::ImportPlan plan;
  std::vector<size_t> stash_questions;  // indexes into plan.stashes still to ask about
  std::map<std::string, bool> replace_stash;
  Box box;
};

State g;

void Log(const std::string& line) {
  REXLOG_INFO("save import: {}", line);
  si::AppendImportLog(g.folder, line);
}

std::string Tr(const std::string& english) { return g.strings.Translate(g.language, english); }

// UTF-16BE, zero terminated, at `address`.
uint32_t PutText(uint8_t* base, uint32_t address, const std::string& utf8) {
  const std::u16string text = si::Utf16(utf8);
  for (char16_t unit : text) {
    uint8_t* bytes = abi::xbox_memory::HostAddress(base, address);
    bytes[0] = static_cast<uint8_t>(unit >> 8);
    bytes[1] = static_cast<uint8_t>(unit);
    address += 2;
  }
  uint8_t* end = abi::xbox_memory::HostAddress(base, address);
  end[0] = end[1] = 0;
  return address + 2;
}

size_t TextBytes(const std::string& utf8) { return (si::Utf16(utf8).size() + 1) * 2; }

void FreeBox() {
  if (g.box.block) g.runtime->memory()->SystemHeapFree(g.box.block);
  g.box = {};
}

// Opens the box; false if it could not be shown.
bool ShowBox(PPCContext& ctx, uint8_t* base, const si::ImportMessage& message) {
  FreeBox();
  // result (4) | XOVERLAPPED (28) | button pointers | texts
  const uint32_t fixed = 4 + 28 + 4 * static_cast<uint32_t>(message.buttons.size());
  size_t size = fixed + TextBytes(message.title) + TextBytes(message.text);
  for (const auto& button : message.buttons) size += TextBytes(button);
  const uint32_t block = g.runtime->memory()->SystemHeapAlloc(static_cast<uint32_t>(size + 16));
  if (!block) return false;
  std::memset(abi::xbox_memory::HostAddress(base, block), 0, size + 16);
  g.box = {block, block, block + 4};
  abi::WriteU32(base, g.box.overlapped, X_ERROR_IO_PENDING);  // XOVERLAPPED.result
  const uint32_t buttons = block + 32;
  const uint32_t title = buttons + 4 * static_cast<uint32_t>(message.buttons.size());
  const uint32_t text = PutText(base, title, message.title);
  uint32_t next = PutText(base, text, message.text);
  for (size_t i = 0; i < message.buttons.size(); ++i) {
    abi::WriteU32(base, buttons + 4 * static_cast<uint32_t>(i), next);
    next = PutText(base, next, message.buttons[i]);
  }

  // Nine arguments: the ninth on the stack, below the caller's (the hooked function has returned,
  // nothing there is live).
  PPCContext call = ctx;
  const uint32_t stack = (ctx.r1.u32 - 0x200) & ~0xFu;
  call.r1.u64 = stack;
  call.r3.u64 = 0;  // user index
  call.r4.u64 = title;
  call.r5.u64 = text;
  call.r6.u64 = message.buttons.size();
  call.r7.u64 = buttons;
  call.r8.u64 = static_cast<uint32_t>(message.active);
  call.r9.u64 = 0;  // flags: no icon
  call.r10.u64 = g.box.result;
  abi::WriteU32(base, stack + menu::kStackArgumentOffset, g.box.overlapped);
  __imp__XamShowMessageBoxUI(call, base);
  if (call.r3.u32 != X_ERROR_IO_PENDING && call.r3.u32 != X_ERROR_SUCCESS) {
    REXLOG_ERROR("save import: the message box did not open ({:08X})", call.r3.u32);
    FreeBox();
    return false;
  }
  return true;
}

// The chosen button once the box closed (-1 if it closed without one); none while it is open.
std::optional<int> PollBox(uint8_t* base) {
  if (!g.box.block) return -1;
  const uint32_t status = abi::ReadU32(base, g.box.overlapped);
  if (status == X_ERROR_IO_PENDING) return std::nullopt;
  const int button = status == X_ERROR_SUCCESS ? static_cast<int>(abi::ReadU32(base, g.box.result)) : -1;
  FreeBox();
  return button;
}

bool ReadSaveFile(const std::string& name, si::Bytes& out) {
  rex::filesystem::File* file = nullptr;
  rex::filesystem::FileAction action;
  if (g.runtime->file_system()->OpenFile(nullptr, kSaveRoot + name, rex::filesystem::FileDisposition::kOpen,
                                         rex::filesystem::FileAccess::kGenericRead, false, true, &file,
                                         &action) != X_STATUS_SUCCESS) {
    return false;
  }
  out.assign(file->entry() ? file->entry()->size() : 0, 0);
  size_t read = 0;
  file->ReadSync(std::span<uint8_t>(out), 0, &read);
  file->Destroy();
  out.resize(read);
  return true;
}

bool WriteNewSaveFile(const std::string& name, const si::Bytes& data) {
  rex::filesystem::File* file = nullptr;
  rex::filesystem::FileAction action;
  if (g.runtime->file_system()->OpenFile(nullptr, kSaveRoot + name, rex::filesystem::FileDisposition::kCreate,
                                         rex::filesystem::FileAccess::kGenericWrite, false, true, &file,
                                         &action) != X_STATUS_SUCCESS) {
    return false;
  }
  size_t written = 0;
  const X_STATUS status = file->WriteSync(std::span<const uint8_t>(data), 0, &written);
  file->Destroy();
  return status == X_STATUS_SUCCESS && written == data.size();
}

// With the game's mount: the container's files, stashes and settings, and its host path.
bool ReadContainer(GuestCall& call) {
  call.Call(menu::kMountSaves.address, {});
  rex::filesystem::Entry* root = g.runtime->file_system()->ResolvePath(kSaveRoot);
  bool ok = root != nullptr;
  if (ok) {
    g.view = {};
    for (const auto& child : root->children()) {
      const std::string name = child->name();
      g.view.file_names.push_back(name);
      std::string lower = name;
      for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      si::Bytes data;
      if ((lower == "sharedstash.bin" || lower == "sharedstashh.bin") && ReadSaveFile(name, data)) {
        g.view.stashes[lower] = std::move(data);
      } else if ((lower == "settings.txt" || lower == "local_settings.txt") && ReadSaveFile(name, data)) {
        g.view.settings[lower] = std::move(data);
      }
    }
    const auto* host = dynamic_cast<const rex::filesystem::HostPathEntry*>(root);
    g.container = host ? host->host_path() : fs::path();
  }
  call.Call(menu::kUnmountSaves.address, {});
  return ok;
}

void PickLanguage() {
  const std::string pack = live::LanguagePack();
  g.language = pack.empty() ? settings::LanguageCode(rex::cvar::Query<uint32_t>("user_language")) : pack;
}

// What the start took out of the saves (mods/save_units_install.h), told once, before the import
// question. True if the box opened.
bool ShowSaveUnitsNotice(PPCContext& ctx, uint8_t* base) {
  if (g.notice_shown) return false;
  g.notice_shown = true;
  const auto& changes = mods::SaveUnitsChanges();
  if (!changes) return false;
  PickLanguage();
  const auto message = mods::SaveUnitsNotice(*changes, Tr);
  return message && ShowBox(ctx, base, *message);
}

void Begin(PPCContext& ctx, uint8_t* base) {
  std::error_code ec;
  if (!fs::is_directory(g.folder, ec)) return;
  g.scanned = si::ScanImportFolder(g.folder);
  if (!g.scanned.HasWork() && g.scanned.notices.empty()) return;
  {
    GuestCall call(ctx, base);
    if (!ReadContainer(call)) {
      REXLOG_WARN("save import: the save container is not available; not asking now");
      return;
    }
  }
  PickLanguage();
  g.planning = std::async(std::launch::async, [scanned = g.scanned, view = g.view] {
    return si::BuildImportPlan(*g.schema, scanned, g.game_pak, view);
  });
  g.stage = Stage::kPlanning;
}

void AskNextStash(PPCContext& ctx, uint8_t* base, uint32_t menu_object);

void Finish(PPCContext& ctx, uint8_t* base, uint32_t menu_object) {
  GuestCall call(ctx, base);
  call.Call(menu::kMountSaves.address, {});
  // Characters whose file cannot be written stay in the folder, to be asked about again.
  std::vector<si::CharacterImport> written;
  for (auto& character : g.plan.characters) {
    if (character.ok() && !WriteNewSaveFile(character.destination(), character.converted)) {
      Log(character.source.filename().string() + ": could not write " + character.destination() +
          "; left in the import folder");
      continue;
    }
    written.push_back(std::move(character));
  }
  call.Call(menu::kUnmountSaves.address, {});
  g.plan.characters = std::move(written);

  std::string error;
  if (!si::ConfirmImport(g.plan, g.scanned, g.container, g.replace_stash, Log, error)) {
    Log("could not record the import: " + error);
  }
  // Rebuild the list as the game does after deleting a character.
  call.Call(menu::kBuildList.address, {menu_object, 0, 0});
  call.Call(menu::kAfterList.address, {menu_object});
  call.Call(menu::kRefresh.address, {menu_object, 0, 1});
  g.stage = Stage::kIdle;
}

void AskNextStash(PPCContext& ctx, uint8_t* base, uint32_t menu_object) {
  while (!g.stash_questions.empty()) {
    const auto& stash = g.plan.stashes[g.stash_questions.front()];
    if (ShowBox(ctx, base, si::ReplaceStashMessage(stash, Tr))) {
      g.stage = Stage::kAskingStash;
      return;
    }
    g.replace_stash[stash.destination] = false;  // could not ask: do not replace
    g.stash_questions.erase(g.stash_questions.begin());
  }
  Finish(ctx, base, menu_object);
}

void Step(PPCContext& ctx, uint8_t* base, uint32_t menu_object) {
  switch (g.stage) {
    case Stage::kNotice: {
      if (!PollBox(base)) return;
      g.stage = Stage::kIdle;
      if (g.import_on && !g.asked_this_session) Begin(ctx, base);
      return;
    }
    case Stage::kPlanning: {
      if (g.planning.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
      g.plan = g.planning.get();
      g.asked_this_session = true;
      if (!g.plan.HasSomethingToAsk()) {
        g.stage = Stage::kIdle;
        return;
      }
      g.stage = ShowBox(ctx, base, si::MainImportMessage(g.plan, g.folder, Tr)) ? Stage::kAsking : Stage::kIdle;
      return;
    }
    case Stage::kAsking: {
      const auto button = PollBox(base);
      if (!button) return;
      switch (si::MainImportAnswer(g.plan, *button)) {
        case si::MainAnswer::kNotNow:
          Log("asked; not now");
          g.stage = Stage::kIdle;
          return;
        case si::MainAnswer::kDoNotAskAgain: {
          std::string error;
          if (!si::SkipImport(g.plan, g.scanned, Log, error)) Log("could not skip: " + error);
          g.stage = Stage::kIdle;
          return;
        }
        case si::MainAnswer::kYes:
          g.replace_stash.clear();
          g.stash_questions.clear();
          for (size_t i = 0; i < g.plan.stashes.size(); ++i) {
            if (g.plan.stashes[i].needs_replace_confirmation()) g.stash_questions.push_back(i);
          }
          AskNextStash(ctx, base, menu_object);
          return;
      }
      return;
    }
    case Stage::kAskingStash: {
      const auto button = PollBox(base);
      if (!button) return;
      g.replace_stash[g.plan.stashes[g.stash_questions.front()].destination] = *button == 1;
      g.stash_questions.erase(g.stash_questions.begin());
      AskNextStash(ctx, base, menu_object);
      return;
    }
    default:
      return;
  }
}

}  // namespace

void InstallSaveImport(rex::Runtime* runtime, const fs::path& game_data_root,
                       const fs::path& user_data_root) {
  // The SDK backs up a save container before deleting it; keep the newest ten and the last 30 days.
  si::PruneSaveBackups(user_data_root, std::chrono::system_clock::now(),
                       [](const std::string& line) { REXLOG_INFO("save backups: {}", line); });
  if (!runtime || !runtime->file_system()) return;
  g.runtime = runtime;
  std::string error;
  {
    std::vector<std::string> warnings;
    const fs::path strings = fs::path(platform::ResourceDir()) / "data" / "ui" / kStringsFile;
    if (!g.strings.Load(strings.string(), warnings, error)) {
      REXLOG_WARN("save import: {}; messages in English", error);
    }
  }
  g.stage = Stage::kIdle;  // the saves' notice works without the import
  if (game_data_root.empty()) return;
  g.schema = si::Schema::Embedded(error);
  if (!g.schema) {
    REXLOG_ERROR("save import: {}; import off", error);
    return;
  }
  // The import folder belongs to the game's files folder (user_folders.h kGameData), wherever the
  // game data in use comes from; the 360 pak is the one the game reads.
  const fs::path game_files = platform::UserFolder(platform::UserFolderKind::kGameData);
  if (game_files.empty()) {
    REXLOG_WARN("save import: no game files folder on this platform; import off");
    return;
  }
  g.folder = game_files / "import";
  g.game_pak = game_data_root / "pak.zip";
  std::error_code ec;
  if (fs::is_directory(g.folder, ec)) {
    for (const auto& notice : si::ApplyPending(g.folder, *g.schema, Log)) {
      REXLOG_WARN("save import: something confirmed before was not applied ({}); the menu asks again",
                  notice.file);
    }
  }
  g.import_on = true;
}

}  // namespace torchlight::game_menu

extern "C" {

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(torchlight::guest_abi::save_menu::entry.address == 0x##addr##u, "guest_abi mismatch")

FUNCTION_ADDRESS_CHECK(kSetOpen, 823874D8);
REX_EXTERN(__imp__sub_823874D8);
REX_FUNC(sub_823874D8) {
  using namespace torchlight::game_menu;
  const bool open = (ctx.r4.u32 & 0xFF) != 0;
  __imp__sub_823874D8(ctx, base);
  if (!open || g.stage != Stage::kIdle) return;
  if (ShowSaveUnitsNotice(ctx, base)) {
    g.stage = Stage::kNotice;
  } else if (g.import_on && !g.asked_this_session) {
    Begin(ctx, base);
  }
}

FUNCTION_ADDRESS_CHECK(kUpdate, 8238A138);
REX_EXTERN(__imp__sub_8238A138);
REX_FUNC(sub_8238A138) {
  using namespace torchlight::game_menu;
  const uint32_t menu_object = ctx.r3.u32;
  __imp__sub_8238A138(ctx, base);
  if (g.stage != Stage::kOff && g.stage != Stage::kIdle) Step(ctx, base, menu_object);
}

}  // extern "C"
