// A third column in the skill tree of the character screen (CCharSkillsMenuXenon, game_ui.h
// skills_menu): the parts that need no guest.
//
// The Xbox tree has 3 panes x 6 rows x 2 columns of cells ("skillbg<pane>_<row>_<column>" in
// skillsmenu_xenon.uilayout). PC's trees have 3 columns, and so do the trees of PC mods' classes:
// a skill in the third column has no cell on the Xbox, and the game's table of the tree's skills,
// indexed 12 * pane + 2 * row + column, gives it the slot of the next row's first cell. The layout
// gets the third column's cells when it is loaded (AddSkillColumn); the hooks (skill_tree.cpp) use
// them only for a class that has a skill in that column. A class without one (the game's own
// three) keeps the game's cells, places and navigation unchanged.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace torchlight::game_menu::skill_tree {

inline constexpr int kPanes = 3;
inline constexpr int kRows = 6;
inline constexpr int kGameColumns = 2;
inline constexpr int kColumns = 3;

// The layout with a third column of cells, each a copy of the same row's second cell (its image,
// icon and level text, names ending in _2) starting hidden; nothing when the layout has no skill
// cells or already has the third column. Every original byte is kept: the cells are inserted.
std::optional<std::string> AddSkillColumn(std::string_view xml);

// The game's table slot of a cell (the builder's and the getter's 12 * pane + 2 * row + column,
// clamped to 35); -1 below 0, as the builder skips those.
int GameSlot(int pane, int row, int column);

// A skill of the player's list, as the tree builder reads it.
struct Skill {
  int pane = 0, row = 0, column = 0;
  uint64_t id = 0;
};

// What the table should hold after the builder ran: the slots of the first two columns' skills
// (the last skill of a cell wins, as in the builder) and the third column's skills by (pane, row).
// A third-column skill never takes a slot.
struct Table {
  std::vector<std::pair<int, uint64_t>> slots;  // in the list's order
  std::map<std::pair<int, int>, uint64_t> third;
};
Table BuildTable(const std::vector<Skill>& skills);
bool UsesThirdColumn(const std::vector<Skill>& skills);

struct Focus {
  int pane = 0, row = 0, column = 0;
  bool operator==(const Focus&) const = default;
};
using CellVisible = std::function<bool(int pane, int row, int column)>;

// Left and right along the focused row, across panes and wrapping, as the game's handlers do
// (sub_8235E488 and sub_8235E3A0) with `columns` per pane: hidden cells are skipped. Unlike the
// game's, a row with no visible cell stops after one turn and gives nothing (the focus stays).
std::optional<Focus> StepRight(Focus from, int columns, const CellVisible& visible);
std::optional<Focus> StepLeft(Focus from, int columns, const CellVisible& visible);

// The first visible cell in reading order (rows from the top, panes and columns from the left);
// nothing when no cell is visible.
std::optional<Focus> FirstVisible(int columns, const CellVisible& visible);

// A window's area as offsets from its parent's corner (CEGUI's UnifiedAreaRect with every scale
// 0, the form of the tree's cells); nothing for another form.
struct Rect {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool operator==(const Rect&) const = default;
};
std::optional<Rect> ParseOffsetRect(std::string_view area);
std::string FormatOffsetRect(const Rect& rect);

// Three cells of a row in the width the game's two take within a pane: from the first and second
// cells' areas, cells of kThreeColumnScale of their size, kThreeColumnGap of their width apart,
// centred where the two were, keeping the row's vertical centre.
inline constexpr double kThreeColumnScale = 0.78;
inline constexpr double kThreeColumnGap = 0.1;
std::array<Rect, 3> ThreeColumnCells(const Rect& first, const Rect& second);
// A cell's child (icon, level text) in a cell scaled by `scale`: its offsets scaled.
Rect ScaleChild(const Rect& child, double scale);

}  // namespace torchlight::game_menu::skill_tree
