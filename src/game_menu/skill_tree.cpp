// The skill tree's third column (skill_tree_model.h) in the character screen's skill page
// (CCharSkillsMenuXenon, game_ui.h): hooks on the page's tree builder, its cell reset, its left and
// right handlers and its focused-skill getter.
//
// The layout's third-column cells come from the layout cache (video_menu.cpp, AddSkillColumn).
// After every build the table of the tree's skills is put right (a third-column skill no longer
// holds the slot of the cell below it) and the focus moves off a hidden cell to the first one shown.
// For a class with a skill in the third column, the three columns are placed in each pane's width,
// left and right move through them, and the third column's skills are found by the getter; for any
// other class the cells keep their places and the game's own handlers run.

#include <array>
#include <bit>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "game_menu/skill_tree_model.h"
#include "guest_abi/game_ui.h"

namespace abi = torchlight::guest_abi;
namespace ui = torchlight::guest_abi::game_ui;
namespace st = torchlight::game_menu::skill_tree;

namespace torchlight::game_menu {

namespace {

// A cell and its children, looked up once per menu (the windows live as long as the menu).
struct CellWindows {
  uint32_t cell = 0, icon = 0, level = 0;
};

struct MenuState {
  bool looked_up = false;
  CellWindows cells[st::kPanes][st::kRows][st::kColumns] = {};
  bool third_cells = false;   // the layout has the third column (the cache added it)
  bool three = false;         // the current class uses it and its cells are placed
  bool geometry_failed = false;
  std::map<uint32_t, std::string> original_areas;  // by window, before the first placement
  std::map<std::pair<int, int>, uint64_t> third;   // the third column's skills
};
std::map<uint32_t, MenuState> g_menus;

uint32_t Read32(uint8_t* base, uint32_t address) { return abi::ReadU32(base, address); }
int ReadS16Field(uint8_t* base, uint32_t object, abi::Field field) {
  return int16_t(abi::ReadU32(base, object + field.offset) & 0xFFFF);
}
int ReadFocus(uint8_t* base, uint32_t address) { return int16_t(abi::ReadU16(base, address)); }
void WriteFocus(uint8_t* base, uint32_t address, int value) {
  const uint16_t big = std::byteswap(uint16_t(int16_t(value)));
  std::memcpy(abi::xbox_memory::HostAddress(base, address), &big, 2);
}

st::Focus FocusOf(uint8_t* base, uint32_t menu) {
  return {ReadFocus(base, menu + ui::skills_menu::kFocusPane.offset),
          ReadFocus(base, menu + ui::skills_menu::kFocusRow.offset),
          ReadFocus(base, menu + ui::skills_menu::kFocusColumn.offset)};
}
void SetFocus(uint8_t* base, uint32_t menu, const st::Focus& f) {
  WriteFocus(base, menu + ui::skills_menu::kFocusPane.offset, f.pane);
  WriteFocus(base, menu + ui::skills_menu::kFocusRow.offset, f.row);
  WriteFocus(base, menu + ui::skills_menu::kFocusColumn.offset, f.column);
}
bool OnSpellRow(uint8_t* base, uint32_t menu) {
  return abi::ReadU8(base, menu + ui::skills_menu::kSpellRow.offset) != 0;
}

uint32_t FindWindow(GuestCall& call, uint32_t root, const std::string& name) {
  const uint32_t mark = call.Mark();
  const uint32_t text = call.CeguiString(name);
  const uint32_t window = text ? call.Call(ui::kFindDescendant.address, {root, text}) : 0;
  call.DestroyCeguiString(text);
  call.Release(mark);
  return window;
}

void LookUp(GuestCall& call, uint32_t menu, MenuState& state) {
  if (state.looked_up) return;
  const uint32_t root = call.CallVirtual(menu, ui::kSkillsMenuRootSlot.index, {});
  if (!root) return;
  state.looked_up = true;
  for (int p = 0; p < st::kPanes; ++p) {
    for (int r = 0; r < st::kRows; ++r) {
      for (int c = 0; c < st::kColumns; ++c) {
        const std::string id = std::to_string(p) + "_" + std::to_string(r) + "_" + std::to_string(c);
        CellWindows& w = state.cells[p][r][c];
        w.cell = FindWindow(call, root, "skillbg" + id);
        w.icon = FindWindow(call, root, "skillicon" + id);
        w.level = FindWindow(call, root, "skilllvl" + id);
      }
    }
  }
  state.third_cells = true;
  for (auto& pane : state.cells) {
    for (auto& row : pane) state.third_cells = state.third_cells && row[2].cell;
  }
  REXLOG_INFO("skill tree: menu 0x{:08X}, third column {}", menu,
              state.third_cells ? "in the layout" : "not in the layout (two columns only)");
}

bool Shown(uint8_t* base, const MenuState& state, int p, int r, int c) {
  if (p < 0 || p >= st::kPanes || r < 0 || r >= st::kRows || c < 0 || c >= st::kColumns) return false;
  const uint32_t cell = state.cells[p][r][c].cell;
  return cell && abi::ReadU8(base, cell + ui::kWindowVisible.offset) != 0;
}

std::vector<st::Skill> ReadSkills(uint8_t* base, uint32_t menu) {
  std::vector<st::Skill> skills;
  const uint32_t character = Read32(base, menu + ui::skills_menu::kCharacter.offset);
  if (!character) return skills;
  const uint32_t list = Read32(base, character + ui::skills_menu::kCharacterSkills.offset);
  if (!list) return skills;
  const int32_t count = int32_t(Read32(base, list + ui::skill_list::kCount.offset));
  const uint32_t bound = Read32(base, list + ui::skill_list::kBound.offset);
  const uint32_t data = Read32(base, list + ui::skill_list::kData.offset);
  for (int32_t i = 0; i < count; ++i) {
    const uint32_t skill = Read32(base, uint32_t(i) < bound ? data + 4 * uint32_t(i) : data);
    if (!skill) continue;
    skills.push_back({ReadS16Field(base, skill, ui::skill::kPane),
                      ReadS16Field(base, skill, ui::skill::kRow),
                      ReadS16Field(base, skill, ui::skill::kColumn),
                      abi::ReadU64(base, skill + ui::skill::kId.offset)});
  }
  return skills;
}

std::string ReadCeguiString(uint8_t* base, uint32_t str) {
  namespace cs = ui::cegui_string;
  const uint32_t length = Read32(base, str + cs::kLength.offset);
  const uint32_t reserve = Read32(base, str + cs::kReserve.offset);
  const uint32_t chars = reserve > cs::kInlineCapacity ? Read32(base, str + cs::kHeapBuffer.offset)
                                                       : str + cs::kInlineBuffer.offset;
  std::string text;
  for (uint32_t i = 0; i < length && i < 256; ++i) {
    const uint32_t c = Read32(base, chars + 4 * i);
    text.push_back(c < 0x80 ? char(c) : '?');
  }
  return text;
}

std::string GetArea(GuestCall& call, uint32_t window) {
  const uint32_t mark = call.Mark();
  const uint32_t name = call.CeguiString("UnifiedAreaRect");
  const uint32_t result = call.Reserve(ui::kCeguiStringSize);
  std::string area;
  if (name && result) {
    call.Call(ui::kWindowGetProperty.address, {result, window, name});
    area = ReadCeguiString(call.base(), result);
    call.DestroyCeguiString(result);
  }
  call.DestroyCeguiString(name);
  call.Release(mark);
  return area;
}

void SetArea(GuestCall& call, uint32_t window, const std::string& area) {
  const uint32_t mark = call.Mark();
  const uint32_t name = call.CeguiString("UnifiedAreaRect");
  const uint32_t value = call.CeguiString(area);
  if (name && value) call.Call(ui::kWindowSetProperty.address, {window, name, value});
  call.DestroyCeguiString(value);
  call.DestroyCeguiString(name);
  call.Release(mark);
}

// The windows' areas as the layout left them (scaled for the screen), read before the first
// placement; false when one is missing or not in offsets.
bool SaveOriginalAreas(GuestCall& call, MenuState& state) {
  for (auto& pane : state.cells) {
    for (auto& row : pane) {
      for (const CellWindows& w : row) {
        for (uint32_t window : {w.cell, w.icon, w.level}) {
          if (!window) return false;
          if (state.original_areas.contains(window)) continue;
          const std::string area = GetArea(call, window);
          if (!st::ParseOffsetRect(area)) {
            REXLOG_WARN("skill tree: area \"{}\" of window 0x{:08X} is not in offsets", area, window);
            return false;
          }
          state.original_areas[window] = area;
        }
      }
    }
  }
  return true;
}

// Three columns in each pane's width, or the layout's places back.
bool PlaceCells(GuestCall& call, MenuState& state, bool three) {
  if (!three) {
    for (const auto& [window, area] : state.original_areas) SetArea(call, window, area);
    return true;
  }
  if (!SaveOriginalAreas(call, state)) return false;
  for (int p = 0; p < st::kPanes; ++p) {
    for (int r = 0; r < st::kRows; ++r) {
      const auto first = st::ParseOffsetRect(state.original_areas[state.cells[p][r][0].cell]);
      const auto second = st::ParseOffsetRect(state.original_areas[state.cells[p][r][1].cell]);
      const auto placed = st::ThreeColumnCells(*first, *second);
      for (int c = 0; c < st::kColumns; ++c) {
        const CellWindows& w = state.cells[p][r][c];
        SetArea(call, w.cell, st::FormatOffsetRect(placed[c]));
        for (uint32_t child : {w.icon, w.level}) {
          const auto area = st::ParseOffsetRect(state.original_areas[child]);
          SetArea(call, child, st::FormatOffsetRect(st::ScaleChild(*area, st::kThreeColumnScale)));
        }
      }
    }
  }
  return true;
}

void HideThirdColumn(GuestCall& call, const MenuState& state) {
  for (const auto& pane : state.cells) {
    for (const auto& row : pane) {
      if (row[2].cell) call.Call(ui::kWindowSetVisible.address, {row[2].cell, 0});
    }
  }
}

int g_build_depth = 0;

// After the game built the tree.
void AfterBuild(PPCContext& ctx, uint8_t* base, uint32_t menu) {
  GuestCall call(ctx, base);
  MenuState& state = g_menus[menu];
  LookUp(call, menu, state);
  if (!state.looked_up) return;

  const std::vector<st::Skill> skills = ReadSkills(base, menu);
  const st::Table table = st::BuildTable(skills);
  for (const auto& [slot, id] : table.slots) {
    const uint32_t at = menu + ui::skills_menu::kTable.offset + 8 * uint32_t(slot);
    const uint64_t big = std::byteswap(id);
    std::memcpy(abi::xbox_memory::HostAddress(base, at), &big, 8);
  }
  state.third = table.third;

  const bool wanted = state.third_cells && !state.geometry_failed && st::UsesThirdColumn(skills);
  if (wanted != state.three) {
    if (PlaceCells(call, state, wanted)) {
      state.three = wanted;
      REXLOG_INFO("skill tree: {} columns ({} skill(s) in the third)", wanted ? 3 : 2,
                  table.third.size());
    } else {
      state.geometry_failed = true;
      REXLOG_WARN("skill tree: the cells could not be placed; two columns only");
    }
  }
  if (!state.three) HideThirdColumn(call, state);

  // The focus on a hidden cell (the game starts at the first one, whatever the class has there):
  // to the first cell shown, and the tree built again to describe its skill.
  if (g_build_depth > 1 || OnSpellRow(base, menu)) return;
  const int columns = state.three ? st::kColumns : st::kGameColumns;
  const st::Focus focus = FocusOf(base, menu);
  const auto shown = [&](int p, int r, int c) { return c < columns && Shown(base, state, p, r, c); };
  if (shown(focus.pane, focus.row, focus.column)) return;
  const auto first = st::FirstVisible(columns, shown);
  if (!first) return;
  SetFocus(base, menu, *first);
  call.CallVirtual(menu, ui::kSkillsMenuBuildTreeSlot.index, {});
}

// Left or right with three columns; false to let the game's handler run.
bool Step(PPCContext& ctx, uint8_t* base, uint32_t menu, bool right) {
  const auto it = g_menus.find(menu);
  if (it == g_menus.end() || !it->second.three || OnSpellRow(base, menu)) return false;
  const MenuState& state = it->second;
  const auto shown = [&](int p, int r, int c) { return Shown(base, state, p, r, c); };
  const st::Focus from = FocusOf(base, menu);
  const auto to = right ? st::StepRight(from, st::kColumns, shown) : st::StepLeft(from, st::kColumns, shown);
  GuestCall call(ctx, base);
  if (to) SetFocus(base, menu, *to);
  call.CallVirtual(menu, ui::kSkillsMenuBuildTreeSlot.index, {});
  return true;
}

}  // namespace

}  // namespace torchlight::game_menu

extern "C" {

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(ui::entry.address == 0x##addr##u, "game_ui mismatch")

REX_EXTERN(sub_821F58F8);

FUNCTION_ADDRESS_CHECK(kSkillsMenuSetCharacter, 8235BD40);
REX_EXTERN(__imp__sub_8235BD40);
REX_FUNC(sub_8235BD40) {
  using namespace torchlight::game_menu;
  const uint32_t menu = ctx.r3.u32;
  __imp__sub_8235BD40(ctx, base);
  // The game hides its own two columns' cells; the third's too.
  const auto it = g_menus.find(menu);
  if (it == g_menus.end()) return;
  GuestCall call(ctx, base);
  HideThirdColumn(call, it->second);
}

FUNCTION_ADDRESS_CHECK(kSkillsMenuBuildTree, 8235BE88);
REX_EXTERN(__imp__sub_8235BE88);
REX_FUNC(sub_8235BE88) {
  using namespace torchlight::game_menu;
  const uint32_t menu = ctx.r3.u32;
  ++g_build_depth;
  __imp__sub_8235BE88(ctx, base);
  AfterBuild(ctx, base, menu);
  --g_build_depth;
}

FUNCTION_ADDRESS_CHECK(kSkillsMenuFocusRight, 8235E3A0);
REX_EXTERN(__imp__sub_8235E3A0);
REX_FUNC(sub_8235E3A0) {
  if (torchlight::game_menu::Step(ctx, base, ctx.r3.u32, true)) return;
  __imp__sub_8235E3A0(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kSkillsMenuFocusLeft, 8235E488);
REX_EXTERN(__imp__sub_8235E488);
REX_FUNC(sub_8235E488) {
  if (torchlight::game_menu::Step(ctx, base, ctx.r3.u32, false)) return;
  __imp__sub_8235E488(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kSkillsMenuFocusedSkill, 8235E830);
FUNCTION_ADDRESS_CHECK(kSkillListFind, 821F58F8);
REX_EXTERN(__imp__sub_8235E830);
REX_FUNC(sub_8235E830) {
  using namespace torchlight::game_menu;
  const uint32_t menu = ctx.r3.u32;
  const auto it = g_menus.find(menu);
  if (it != g_menus.end() && it->second.three && !OnSpellRow(base, menu)) {
    const st::Focus f = FocusOf(base, menu);
    if (f.column == st::kColumns - 1) {
      // The third column's skill, looked up as the game looks up the others.
      const auto skill = it->second.third.find({f.pane, f.row});
      const uint32_t character = Read32(base, menu + ui::skills_menu::kCharacter.offset);
      ctx.r3.u64 = Read32(base, character + ui::skills_menu::kCharacterSkills.offset);
      ctx.r4.u64 = skill == it->second.third.end() ? 0 : skill->second;
      sub_821F58F8(ctx, base);
      return;
    }
  }
  __imp__sub_8235E830(ctx, base);
}

}  // extern "C"
