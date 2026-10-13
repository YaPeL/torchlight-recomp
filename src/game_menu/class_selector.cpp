// The character creation screen's class selector with mods (class_selector_model.h; guest_abi
// game_ui.h CNewGameMenu): hooks on the screen's show, pad move and command, and on the class
// selection.
//
// The class list (mods/class_list.h) is read when the screen is first shown. With the game's three
// classes only, every hook runs the game's code unchanged. With more, the three buttons show a
// window onto the list: a move off either end of the class row scrolls it, A picks the class the
// focused button shows (the screen sends the button's window name, which is mapped to that class),
// and a class without an Xbox button image shows an empty frame with its name on the label the
// layout cache added (AddClassLabels).

#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
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

// A class without an Xbox button image: the empty frame of the skill tree's cells.
constexpr const char* kFrameImage = "set:widgets2 image:skills_window_active";
constexpr const char* kFrameRolloverImage = "set:widgets2 image:skills_window_active_rollover";
constexpr const char* kImageProperties[] = {"NormalImage", "HoverImage", "PushedImage"};

struct Selector {
  bool listed = false;
  std::vector<std::string> classes;
  cs::Carousel carousel{0};
  bool images_saved = false;
  std::string images[cs::kSlots][3];  // each game class's button images, as the layout had them
  bool picking = false;               // inside OnCommand(GUISELECT1)
};
Selector g;

bool Active() { return g.listed && g.carousel.Active(); }

std::string Upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

// The game class `name` is (its slot in the game's order), or none.
std::optional<size_t> GameClass(std::string_view name) {
  for (size_t i = 0; i < cs::kSlots; ++i) {
    if (Upper(mods::kGameClasses[i]) == Upper(name)) return i;
  }
  return std::nullopt;
}

void List() {
  if (g.listed) return;
  g.listed = true;
  const mods::ModPlan* plan = MountedModPlan();
  std::vector<std::vector<std::string>> buttons;
  std::set<std::string> units;
  if (plan) {
    const std::filesystem::path folder = std::filesystem::path(platform::DataDir()) / "mods";
    buttons = mods::ModClassButtons(folder, *plan);
    units = mods::ModPlayerUnits(folder, *plan);
  }
  g.classes = mods::ClassList(buttons, units);
  g.carousel = cs::Carousel(g.classes.size());
  std::string list;
  for (const std::string& c : g.classes) list += (list.empty() ? "" : ", ") + c;
  REXLOG_INFO("class selector: {} classes ({}){}", g.classes.size(), list,
              g.carousel.Active() ? "" : "; the game's screen as it is");
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
  std::string text;
  for (uint32_t i = 0; i < length && i < 256; ++i) {
    const uint32_t c = abi::ReadU32(base, chars + 4 * i);
    text.push_back(c < 0x80 ? char(c) : '?');
  }
  return text;
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

// The three buttons show the carousel's classes.
void Refresh(PPCContext& ctx, uint8_t* base, uint32_t menu) {
  GuestCall call(ctx, base);
  const uint32_t root = abi::ReadU32(base, menu + ui::dropdown_menu::kRoot.offset);
  if (!root) return;
  uint32_t buttons[cs::kSlots], labels[cs::kSlots];
  for (size_t s = 0; s < cs::kSlots; ++s) {
    buttons[s] = FindWindow(call, root, std::string(mods::kGameClasses[s]));
    labels[s] = FindWindow(call, root, cs::LabelName(mods::kGameClasses[s]));
    if (!buttons[s]) {
      REXLOG_ERROR("class selector: no button {}; the screen keeps its classes", mods::kGameClasses[s]);
      g.carousel = cs::Carousel(0);
      return;
    }
  }
  if (!g.images_saved) {
    for (size_t s = 0; s < cs::kSlots; ++s) {
      for (size_t i = 0; i < 3; ++i) g.images[s][i] = GetProperty(call, buttons[s], kImageProperties[i]);
    }
    g.images_saved = true;
  }
  for (size_t s = 0; s < cs::kSlots; ++s) {
    const std::string& shown = g.classes[g.carousel.ClassAt(s)];
    if (const auto game = GameClass(shown)) {
      for (size_t i = 0; i < 3; ++i) SetProperty(call, buttons[s], kImageProperties[i], g.images[*game][i]);
      SetProperty(call, labels[s], "Visible", "False");
    } else {
      SetProperty(call, buttons[s], "NormalImage", kFrameImage);
      SetProperty(call, buttons[s], "HoverImage", kFrameRolloverImage);
      SetProperty(call, buttons[s], "PushedImage", kFrameRolloverImage);
      SetProperty(call, labels[s], "Text", shown);
      SetProperty(call, labels[s], "Visible", "True");
    }
  }
}

// The class name as the game's std::wstring (UTF-16 in the guest; the inline buffer while the
// capacity is below 8, ogre_layout.h stl_string in wchar_t units).
std::string ReadWString(uint8_t* base, uint32_t str) {
  namespace s = abi::ogre::stl_string;
  const uint32_t length = abi::ReadU32(base, str + s::kLength.offset);
  const uint32_t capacity = abi::ReadU32(base, str + s::kCapacity.offset);
  const uint32_t chars = capacity >= 8 ? abi::ReadU32(base, str + s::kBuffer.offset) : str + s::kBuffer.offset;
  std::string text;
  for (uint32_t i = 0; i < length && i < 256; ++i) {
    const uint16_t c = abi::ReadU16(base, chars + 2 * i);
    text.push_back(c < 0x80 ? char(c) : '?');
  }
  return text;
}

// A guest std::wstring holding class `index`'s name, on the guest heap, for one selection: made
// before it and destroyed after (DestroyName). One kept across selections broke the second pick of
// the same class (the unit lookup found no unit by that name).
uint32_t MakeName(PPCContext& ctx, uint8_t* base, size_t index) {
  GuestCall call(ctx, base);
  const std::string& name = g.classes[index];
  const uint32_t text = call.Call(ui::kAlloc.address, {0, uint32_t(2 * (name.size() + 1))});
  const uint32_t str = call.Call(ui::kAlloc.address, {0, abi::ogre::stl_string::kSize.bytes});
  if (!text || !str) return 0;
  for (size_t i = 0; i <= name.size(); ++i) {
    const uint16_t c = i < name.size() ? uint16_t(static_cast<unsigned char>(name[i])) : 0;
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
  // The game selects the Destroyer as it shows the screen: the list starts with it again.
  g.carousel.Reset();
  __imp__sub_8238DCA8(ctx, base);
  if (Active()) Refresh(ctx, base, menu);
}

FUNCTION_ADDRESS_CHECK(kNewGameMenuMoveFocus, 8238F4F8);
REX_EXTERN(__imp__sub_8238F4F8);
REX_FUNC(sub_8238F4F8) {
  using namespace torchlight::game_menu;
  const uint32_t menu = ctx.r3.u32;
  const int columns = int(ctx.r4.s32), rows = int(ctx.r5.s32);
  if (Active() && rows == 0 && columns != 0) {
    const int32_t row = int32_t(abi::ReadU32(base, menu + ui::new_game_menu::kFocusRow.offset));
    const int32_t column = int32_t(abi::ReadU32(base, menu + ui::new_game_menu::kFocusColumn.offset));
    if (row == int32_t(ui::kNewGameClassRow) && column >= 0 &&
        g.carousel.Scrolls(size_t(column), columns)) {
      g.carousel.Scroll(columns > 0 ? 1 : -1);
      Refresh(ctx, base, menu);
      return;  // the focus stays on the button, which now shows the next class
    }
  }
  __imp__sub_8238F4F8(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kNewGameMenuOnCommand, 8238E7F0);
REX_EXTERN(__imp__sub_8238E7F0);
REX_FUNC(sub_8238E7F0) {
  using namespace torchlight::game_menu;
  g.picking = Active() && ctx.r4.u32 == ui::kCommandSelectClass;
  __imp__sub_8238E7F0(ctx, base);
  g.picking = false;
}

FUNCTION_ADDRESS_CHECK(kSelectClass, 82214000);
REX_EXTERN(__imp__sub_82214000);
REX_FUNC(sub_82214000) {
  using namespace torchlight::game_menu;
  uint32_t name = 0;
  if (g.picking) {
    // The button's window name: the class it shows instead.
    const std::string button = ReadWString(base, ctx.r4.u32);
    if (const auto slot = GameClass(button)) {
      const size_t index = g.carousel.ClassAt(*slot);
      if (Upper(g.classes[index]) != Upper(button)) {
        name = MakeName(ctx, base, index);
        if (name) {
          REXLOG_INFO("class selector: button {} picks {}", button, g.classes[index]);
          ctx.r4.u64 = name;
        }
      }
    }
  }
  __imp__sub_82214000(ctx, base);
  if (name) DestroyName(ctx, base, name);
}

}  // extern "C"
