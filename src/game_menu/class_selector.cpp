// The character creation screen's class and pet selectors with mods (class_selector_model.h;
// guest_abi game_ui.h CNewGameMenu): hooks on the screen's show, pad move and command, and on the
// class and pet selections.
//
// The lists (mods/class_list.h) are read when the screen is first shown. A row with the game's
// three choices only runs the game's code unchanged. With more, its three buttons show a window
// onto the list: a move off either end of the row scrolls it, A picks what the focused button shows
// (the screen sends the button's window name, which is mapped to that choice), and a choice without
// an Xbox button image shows its mod's own PC icon when it has one (mods/class_list.h ModClassIcons,
// ModPetIcons for a pet whose mod gives its button images:
// the imageset is loaded into CEGUI the first time, game_ui.h kImagesetManagerCreate), or else an
// empty frame with its name on the label the layout cache added (AddClassLabels).

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/class_selector_model.h"
#include "game_menu/guest_call.h"
#include "game_menu/mods_install.h"
#include "guest_abi/game_ui.h"
#include "mods/class_list.h"
#include "platform/platform.h"

namespace abi = torchlight::guest_abi;
namespace ui = torchlight::guest_abi::game_ui;
namespace cs = torchlight::game_menu::class_selector;

namespace torchlight::game_menu {

namespace {

// A choice without an Xbox button image: the empty frame of the skill tree's cells.
constexpr const char* kFrameImage = "set:widgets2 image:skills_window_active";
constexpr const char* kFrameRolloverImage = "set:widgets2 image:skills_window_active_rollover";
constexpr const char* kImageProperties[] = {"NormalImage", "HoverImage", "PushedImage"};

// One row of the screen: the classes or the pets.
struct Row {
  Row(const char* what_, std::span<const std::string_view> game_, int32_t grid_row_, uint32_t command_)
      : what(what_), game(game_), grid_row(grid_row_), command(command_) {}

  const char* what;                         // for the log
  std::span<const std::string_view> game;   // the game's three, in the row's order
  int32_t grid_row;                         // the pad grid's row (game_ui.h)
  uint32_t command;                         // OnCommand's command for it
  std::vector<std::string> list;
  std::map<std::string, std::string> shown_names;  // a choice's label (by ClassKey), if not its name
  std::map<std::string, mods::ClassIcon> icons;     // a choice's own PC icon (by ClassKey), if any
  cs::Carousel carousel{0};
  bool images_saved = false;
  std::string images[cs::kSlots][3];  // each game choice's button images, as the layout had them
  bool picking = false;               // inside OnCommand(command)

  bool Active() const { return carousel.Active(); }
  // The game choice `name` is (its slot in the game's order), or none.
  std::optional<size_t> GameSlot(std::string_view name) const {
    for (size_t i = 0; i < cs::kSlots; ++i) {
      if (mods::ClassKey(game[i]) == mods::ClassKey(name)) return i;
    }
    return std::nullopt;
  }
};
Row g_classes("classes", mods::kGameClasses, int32_t(ui::kNewGameClassRow), ui::kCommandSelectClass);
Row g_pets("pets", mods::kGamePets, int32_t(ui::kNewGamePetRow), ui::kCommandSelectPet);
bool g_listed = false;

void Log(const Row& row) {
  std::string names;
  for (const std::string& c : row.list) names += (names.empty() ? "" : ", ") + c;
  REXLOG_INFO("class selector: {} {} ({}){}", row.list.size(), row.what, names,
              row.Active() ? "" : "; the game's row as it is");
}

void List() {
  if (g_listed) return;
  g_listed = true;
  const mods::ModPlan* plan = MountedModPlan();
  std::vector<std::vector<std::string>> class_buttons, pet_buttons;
  std::set<std::string> players, monsters;
  if (plan) {
    const std::filesystem::path folder = std::filesystem::path(platform::DataDir()) / "mods";
    class_buttons = mods::ModClassButtons(folder, *plan);
    players = mods::ModPlayerUnits(folder, *plan);
    pet_buttons = mods::ModPetButtons(folder, *plan);
    monsters = mods::ModMonsterUnits(folder, *plan);
    g_pets.shown_names = mods::ModMonsterDisplayNames(folder, *plan);
    const auto imagesets = mods::ModImagesets(folder, *plan);
    g_classes.icons = mods::ModClassIcons(folder, *plan, imagesets);
    g_pets.icons = mods::ModPetIcons(folder, *plan, imagesets);
  }
  g_classes.list = mods::ClassList(class_buttons, players);
  g_pets.list = mods::PetList(pet_buttons, monsters);
  for (Row* row : {&g_classes, &g_pets}) {
    row->carousel = cs::Carousel(row->list.size());
    Log(*row);
  }
}

uint32_t FindWindow(GuestCall& call, uint32_t root, const std::string& name) {
  const uint32_t mark = call.Mark();
  const uint32_t text = call.CeguiString(name);
  const uint32_t window = text ? call.Call(ui::kFindDescendant.address, {root, text}) : 0;
  call.DestroyCeguiString(text);
  call.Release(mark);
  return window;
}

std::string ReadCeguiString(uint8_t* base, uint32_t str) {
  namespace s = ui::cegui_string;
  const uint32_t length = abi::ReadU32(base, str + s::kLength.offset);
  const uint32_t reserve = abi::ReadU32(base, str + s::kReserve.offset);
  const uint32_t chars = reserve > s::kInlineCapacity ? abi::ReadU32(base, str + s::kHeapBuffer.offset)
                                                      : str + s::kInlineBuffer.offset;
  std::u16string text;
  for (uint32_t i = 0; i < length && i < 256; ++i) {
    const uint32_t c = abi::ReadU32(base, chars + 4 * i);
    if (c < 0x10000) {
      text.push_back(char16_t(c));
    } else {
      text.push_back(char16_t(0xD800 + ((c - 0x10000) >> 10)));
      text.push_back(char16_t(0xDC00 + ((c - 0x10000) & 0x3FF)));
    }
  }
  return mods::Utf16ToUtf8(text);
}

std::string GetProperty(GuestCall& call, uint32_t window, const char* name) {
  const uint32_t mark = call.Mark();
  const uint32_t key = call.CeguiString(name);
  const uint32_t result = call.Reserve(ui::kCeguiStringSize);
  std::string value;
  if (key && result) {
    call.Call(ui::kWindowGetProperty.address, {result, window, key});
    value = ReadCeguiString(call.base(), result);
    call.DestroyCeguiString(result);
  }
  call.DestroyCeguiString(key);
  call.Release(mark);
  return value;
}

void SetProperty(GuestCall& call, uint32_t window, const char* name, const std::string& value) {
  if (!window) return;
  const uint32_t mark = call.Mark();
  const uint32_t key = call.CeguiString(name);
  const uint32_t text = call.CeguiString(value);
  if (key && text) call.Call(ui::kWindowSetProperty.address, {window, key, text});
  call.DestroyCeguiString(text);
  call.DestroyCeguiString(key);
  call.Release(mark);
}

// Whether a mod's imageset is in CEGUI for its icons. The game loads the mods' item icon imagesets
// itself; one it has not loaded is created from the mod's file in the mod's resource group the
// first time. Not when the name is one of the game's own imagesets (its images are not the mod's:
// setting one CEGUI does not have would throw), when the mod has no group, or when the creation did
// not leave it there. (ModImagesets leaves out a name two files define, so an imageset of that
// name the game holds is this mod's file.)
bool EnsureImageset(GuestCall& call, uint8_t* base, const mods::ModImageset& set) {
  static std::map<std::string, bool> known;
  if (const auto it = known.find(set.info.name); it != known.end()) return it->second;
  bool& ok = known[set.info.name];
  if (GameImagesetNames().contains(set.info.name)) {
    REXLOG_WARN("class selector: {} is one of the game's imagesets; {}'s icons from it not used", set.info.name,
                set.mod_folder);
    return ok = false;
  }
  const uint32_t manager = abi::ReadU32(base, ui::kImagesetManagerGlobal);
  const auto group = ModResourceGroup(set.mod_folder);
  if (!manager || !group) {
    REXLOG_WARN("class selector: imageset {} not loaded ({})", set.info.name,
                manager ? "the mod has no resource group" : "no imageset manager");
    return ok = false;
  }
  const uint32_t mark = call.Mark();
  const uint32_t name = call.CeguiString(set.info.name);
  if (name && call.Call(ui::kImagesetManagerFind.address, {manager, name})) {
    REXLOG_INFO("class selector: imageset {} already loaded by the game", set.info.name);
    ok = true;
  } else if (name) {
    const uint32_t file = call.CeguiString(set.file);
    const uint32_t group_text = call.CeguiString(*group);
    if (file && group_text) call.Call(ui::kImagesetManagerCreate.address, {manager, file, group_text});
    ok = call.Call(ui::kImagesetManagerFind.address, {manager, name}) != 0;
    REXLOG_INFO("class selector: imageset {} from {} ({}) {}", set.info.name, set.file, *group,
                ok ? "loaded" : "NOT loaded");
    call.DestroyCeguiString(group_text);
    call.DestroyCeguiString(file);
  }
  call.DestroyCeguiString(name);
  call.Release(mark);
  return ok;
}

// A label's text: the name the game shows for the choice (a pet's DISPLAYNAME), else its name as
// written with its underscores as spaces ("Pet_Wolf").
std::string LabelText(const Row& row, std::string name) {
  if (const auto it = row.shown_names.find(mods::ClassKey(name)); it != row.shown_names.end()) return it->second;
  std::replace(name.begin(), name.end(), '_', ' ');
  return name;
}

// The row's three buttons show its carousel's choices.
void Refresh(PPCContext& ctx, uint8_t* base, uint32_t menu, Row& row) {
  if (!row.Active()) return;
  GuestCall call(ctx, base);
  const uint32_t root = abi::ReadU32(base, menu + ui::dropdown_menu::kRoot.offset);
  if (!root) return;
  uint32_t buttons[cs::kSlots], labels[cs::kSlots];
  for (size_t s = 0; s < cs::kSlots; ++s) {
    buttons[s] = FindWindow(call, root, std::string(row.game[s]));
    labels[s] = FindWindow(call, root, cs::LabelName(row.game[s]));
    if (!buttons[s]) {
      REXLOG_ERROR("class selector: no button {}; the screen keeps its {}", row.game[s], row.what);
      row.carousel = cs::Carousel(0);
      return;
    }
  }
  if (!row.images_saved) {
    for (size_t s = 0; s < cs::kSlots; ++s) {
      for (size_t i = 0; i < 3; ++i) row.images[s][i] = GetProperty(call, buttons[s], kImageProperties[i]);
    }
    row.images_saved = true;
  }
  for (size_t s = 0; s < cs::kSlots; ++s) {
    const std::string& shown = row.list[row.carousel.ClassAt(s)];
    if (const auto game = row.GameSlot(shown)) {
      for (size_t i = 0; i < 3; ++i) SetProperty(call, buttons[s], kImageProperties[i], row.images[*game][i]);
      SetProperty(call, labels[s], "Visible", "False");
    } else if (const auto icon = row.icons.find(mods::ClassKey(shown));
               icon != row.icons.end() && EnsureImageset(call, base, icon->second.imageset)) {
      const auto& images = icon->second.images;
      auto ref = [&](const std::string& image) { return "set:" + images.imageset + " image:" + image; };
      SetProperty(call, buttons[s], "NormalImage", ref(images.normal));
      SetProperty(call, buttons[s], "HoverImage", ref(images.hover));
      SetProperty(call, buttons[s], "PushedImage", ref(images.pushed));
      SetProperty(call, labels[s], "Visible", "False");
    } else {
      SetProperty(call, buttons[s], "NormalImage", kFrameImage);
      SetProperty(call, buttons[s], "HoverImage", kFrameRolloverImage);
      SetProperty(call, buttons[s], "PushedImage", kFrameRolloverImage);
      SetProperty(call, labels[s], "Text", LabelText(row, shown));
      SetProperty(call, labels[s], "Visible", "True");
    }
  }
}

// The name as the game's std::wstring (UTF-16 in the guest; the inline buffer while the capacity is
// below 8, ogre_layout.h stl_string in wchar_t units), as UTF-8.
std::string ReadWString(uint8_t* base, uint32_t str) {
  namespace s = abi::ogre::stl_string;
  const uint32_t length = abi::ReadU32(base, str + s::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + s::kCapacity.offset);
  const uint32_t chars = capacity >= 8 ? abi::ReadU32(base, str + s::kBuffer.offset) : str + s::kBuffer.offset;
  std::u16string text;
  for (uint32_t i = 0; i < length && i < 256; ++i) text.push_back(char16_t(abi::ReadU16(base, chars + 2 * i)));
  return mods::Utf16ToUtf8(text);
}

// A guest std::wstring holding `utf8`, on the guest heap, for one selection: made before it and
// destroyed after (DestroyName). One kept across selections broke the second pick of the same
// class (the unit lookup found no unit by that name).
uint32_t MakeName(PPCContext& ctx, uint8_t* base, const std::string& utf8) {
  GuestCall call(ctx, base);
  const std::u16string name = mods::Utf8ToUtf16(utf8);
  const uint32_t text = call.Call(ui::kAlloc.address, {0, uint32_t(2 * (name.size() + 1))});
  const uint32_t str = call.Call(ui::kAlloc.address, {0, abi::ogre::stl_string::kSize.bytes});
  if (!text || !str) return 0;
  for (size_t i = 0; i <= name.size(); ++i) {
    const uint16_t c = i < name.size() ? uint16_t(name[i]) : 0;
    const uint8_t big[2] = {uint8_t(c >> 8), uint8_t(c)};
    abi::WriteBytes(base, text + uint32_t(2 * i), big, 2);
  }
  call.Call(ui::kWStringFromText.address, {str, text});
  call.Call(ui::kFree.address, {text});
  return str;
}

void DestroyName(PPCContext& ctx, uint8_t* base, uint32_t str) {
  GuestCall call(ctx, base);
  call.Call(ui::kWStringDtor.address, {str});
  call.Call(ui::kFree.address, {str});
}

// A move of the pad's focus: scrolls a row's list instead of leaving the row at either end.
bool Scroll(PPCContext& ctx, uint8_t* base, uint32_t menu, int columns, int rows) {
  if (rows != 0 || columns == 0) return false;
  const int32_t focus_row = int32_t(abi::ReadU32(base, menu + ui::new_game_menu::kFocusRow.offset));
  const int32_t column = int32_t(abi::ReadU32(base, menu + ui::new_game_menu::kFocusColumn.offset));
  for (Row* row : {&g_classes, &g_pets}) {
    if (!row->Active() || focus_row != row->grid_row || column < 0) continue;
    if (!row->carousel.Scrolls(size_t(column), columns)) return false;
    row->carousel.Scroll(columns > 0 ? 1 : -1);
    Refresh(ctx, base, menu, *row);
    return true;  // the focus stays on the button, which now shows the next choice
  }
  return false;
}

// A selection by a button's name: the name of what the button shows instead (0: the button's own).
uint32_t PickedName(PPCContext& ctx, uint8_t* base, Row& row, uint32_t name_arg) {
  if (!row.picking) return 0;
  const std::string button = ReadWString(base, name_arg);
  const auto slot = row.GameSlot(button);
  if (!slot) return 0;
  const std::string& shown = row.list[row.carousel.ClassAt(*slot)];
  if (mods::ClassKey(shown) == mods::ClassKey(button)) return 0;
  const uint32_t name = MakeName(ctx, base, shown);
  if (name) REXLOG_INFO("class selector: button {} picks {}", button, shown);
  return name;
}

}  // namespace

}  // namespace torchlight::game_menu

extern "C" {

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(ui::entry.address == 0x##addr##u, "game_ui mismatch")

FUNCTION_ADDRESS_CHECK(kNewGameMenuOnShow, 8238DCA8);
REX_EXTERN(__imp__sub_8238DCA8);
REX_FUNC(sub_8238DCA8) {
  using namespace torchlight::game_menu;
  const uint32_t menu = ctx.r3.u32;
  List();
  // The game selects the Destroyer and the Dog as it shows the screen: the lists start with them.
  g_classes.carousel.Reset();
  g_pets.carousel.Reset();
  __imp__sub_8238DCA8(ctx, base);
  Refresh(ctx, base, menu, g_classes);
  Refresh(ctx, base, menu, g_pets);
}

FUNCTION_ADDRESS_CHECK(kNewGameMenuMoveFocus, 8238F4F8);
REX_EXTERN(__imp__sub_8238F4F8);
REX_FUNC(sub_8238F4F8) {
  using namespace torchlight::game_menu;
  if (Scroll(ctx, base, ctx.r3.u32, int(ctx.r4.s32), int(ctx.r5.s32))) return;
  __imp__sub_8238F4F8(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kNewGameMenuOnCommand, 8238E7F0);
REX_EXTERN(__imp__sub_8238E7F0);
REX_FUNC(sub_8238E7F0) {
  using namespace torchlight::game_menu;
  g_classes.picking = g_classes.Active() && ctx.r4.u32 == g_classes.command;
  g_pets.picking = g_pets.Active() && ctx.r4.u32 == g_pets.command;
  __imp__sub_8238E7F0(ctx, base);
  g_classes.picking = g_pets.picking = false;
}

FUNCTION_ADDRESS_CHECK(kSelectClass, 82214000);
REX_EXTERN(__imp__sub_82214000);
REX_FUNC(sub_82214000) {
  using namespace torchlight::game_menu;
  const uint32_t name = PickedName(ctx, base, g_classes, ctx.r4.u32);
  if (name) ctx.r4.u64 = name;
  __imp__sub_82214000(ctx, base);
  if (name) DestroyName(ctx, base, name);
}

FUNCTION_ADDRESS_CHECK(kSelectPet, 82214188);
REX_EXTERN(__imp__sub_82214188);
REX_FUNC(sub_82214188) {
  using namespace torchlight::game_menu;
  const uint32_t name = PickedName(ctx, base, g_pets, ctx.r4.u32);
  if (name) ctx.r4.u64 = name;
  __imp__sub_82214188(ctx, base);
  if (name) DestroyName(ctx, base, name);
}

}  // extern "C"
