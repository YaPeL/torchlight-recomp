// Tests for the skill tree's third column (skill_tree_model.h) on synthetic layouts and trees:
// the layout keeps every original byte, a tree without a third-column skill keeps the game's table,
// navigation and cells, and a tree with one gets its cells, slots and focus.

#include "game_menu/skill_tree_model.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

namespace st = torchlight::game_menu::skill_tree;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string Prop(const std::string& name, const std::string& value) {
  return "<Property Name=\"" + name + "\" Value=\"" + value + "\" />";
}
std::string Cell(int pane, int row, int column) {
  const std::string id = std::to_string(pane) + "_" + std::to_string(row) + "_" + std::to_string(column);
  const int x = 17 + 92 * column, y = 24 + 70 * row;
  return "<Window Type=\"w/StaticImage\" Name=\"skillbg" + id + "\" >" +
         Prop("Image", "set:a image:cell") +
         Prop("UnifiedAreaRect", "{{0," + std::to_string(x) + "},{0," + std::to_string(y) + "},{0," +
                                     std::to_string(x + 72) + "},{0," + std::to_string(y + 72) + "}}") +
         "<Window Type=\"w/StaticImage\" Name=\"skillicon" + id + "\" >" +
         Prop("UnifiedAreaRect", "{{0,9},{0,6},{0,63},{0,60}}") + "</Window>" +
         "<Window Type=\"w/StaticText\" Name=\"skilllvl" + id + "\" >" + Prop("Text", "0") +
         "</Window></Window>";
}
// A tree like the game's: three panes of 6 rows x 2 columns, plus a window that is not a cell.
std::string TreeLayout() {
  std::string body = "<GUILayout ><Window Type=\"w/DefaultWindow\" Name=\"root\" >";
  for (int pane = 0; pane < st::kPanes; ++pane) {
    body += "<Window Type=\"w/DefaultWindow\" Name=\"pane" + std::to_string(pane) + "\" >";
    for (int row = 0; row < st::kRows; ++row) {
      for (int column = 0; column < st::kGameColumns; ++column) body += Cell(pane, row, column);
    }
    body += "<Window Type=\"w/StaticText\" Name=\"PanelName" + std::to_string(pane) + "\" />";
    body += "</Window>";
  }
  body += "<Window Type=\"w/StaticImage\" Name=\"skillbg9_9_1\" /></Window></GUILayout>";
  return body;
}

// The game's own left and right handlers (sub_8235E3A0 / sub_8235E488): two columns, recursing past
// hidden cells (a row without a visible cell is never reached in these tests).
st::Focus GameRight(st::Focus f, const st::CellVisible& visible) {
  if (++f.column > 1) {
    f.column = 0;
    if (++f.pane > 2) f.pane = 0;
  }
  return visible(f.pane, f.row, f.column) ? f : GameRight(f, visible);
}
st::Focus GameLeft(st::Focus f, const st::CellVisible& visible) {
  if (--f.column < 0) {
    if (--f.pane < 0) f.pane = 2;
    f.column = 1;
  }
  return visible(f.pane, f.row, f.column) ? f : GameLeft(f, visible);
}

// The table the game's builder leaves (sub_8235BE88: every skill writes its slot, last wins).
std::map<int, uint64_t> GameTable(const std::vector<st::Skill>& skills) {
  std::map<int, uint64_t> table;
  for (const st::Skill& s : skills) {
    const int slot = st::GameSlot(s.pane, s.row, s.column);
    if (slot >= 0) table[slot] = s.id;
  }
  return table;
}
std::map<int, uint64_t> Repaired(const std::vector<st::Skill>& skills) {
  std::map<int, uint64_t> table = GameTable(skills);
  for (const auto& [slot, id] : st::BuildTable(skills).slots) table[slot] = id;
  return table;
}

}  // namespace

int main() {
  // The layout: 18 cells added, hidden, with the second column's children renamed; nothing else
  // changes.
  {
    const std::string in = TreeLayout();
    const auto out = st::AddSkillColumn(in);
    Check(bool(out), "a tree gets a third column");
    if (out) {
      int added = 0;
      for (int pane = 0; pane < st::kPanes; ++pane) {
        for (int row = 0; row < st::kRows; ++row) {
          const std::string id = std::to_string(pane) + "_" + std::to_string(row) + "_2";
          added += Has(*out, "Name=\"skillbg" + id + "\" >" + Prop("Visible", "False")) &&
                   Has(*out, "Name=\"skillicon" + id + "\"") && Has(*out, "Name=\"skilllvl" + id + "\"");
        }
      }
      Check(added == 18, "18 hidden cells, each with its icon and level text");
      Check(!Has(*out, "skillbg9_9_2"), "a window outside the tree is not copied");
      // Removing the added cells gives the layout back byte for byte.
      std::string back = *out;
      for (int pane = 0; pane < st::kPanes; ++pane) {
        for (int row = 0; row < st::kRows; ++row) {
          const std::string id = std::to_string(pane) + "_" + std::to_string(row);
          std::string cell = Cell(pane, row, 1);
          for (const char* name : {"skillbg", "skillicon", "skilllvl"}) {
            const std::string from = std::string(name) + id + "_1\"", to = std::string(name) + id + "_2\"";
            cell.replace(cell.find(from), from.size(), to);
          }
          cell.insert(cell.find('>') + 1, Prop("Visible", "False"));
          const size_t at = back.find(cell);
          Check(at != std::string::npos, "each added cell is the second one's copy");
          if (at != std::string::npos) back.erase(at, cell.size());
        }
      }
      Check(back == in, "every original byte is kept");
      Check(!st::AddSkillColumn(*out), "a layout with the third column is left alone");
    }
    Check(!st::AddSkillColumn("<GUILayout ><Window Type=\"w\" Name=\"a\" /></GUILayout>"),
          "a layout without cells is left alone");
  }

  // Slots: the game's formula, clamped.
  Check(st::GameSlot(0, 0, 0) == 0 && st::GameSlot(1, 2, 1) == 17 && st::GameSlot(2, 5, 1) == 35,
        "slots of the game's cells");
  Check(st::GameSlot(0, 4, 2) == st::GameSlot(0, 5, 0), "a third-column skill aliases the next row");
  Check(st::GameSlot(2, 5, 2) == 35, "the last third-column cell is clamped");

  // A tree with two columns only (as the game's classes): no third column, and the table is the
  // game's.
  std::mt19937 random(7);
  for (int round = 0; round < 200; ++round) {
    std::vector<st::Skill> skills;
    for (int i = 0; i < 40; ++i) {
      if (random() % 3 == 0) continue;
      skills.push_back({int(random() % 3), int(random() % 6), int(random() % 2), random()});
    }
    if (round == 0) skills.push_back({-1, -1, -1, 99});  // a skill with no cell
    Check(!st::UsesThirdColumn(skills), "a two-column tree has no third column");
    Check(Repaired(skills) == GameTable(skills), "a two-column tree keeps the game's table");
    Check(st::BuildTable(skills).third.empty(), "nothing in the third column's table");
  }

  // A third-column skill: kept apart, and the cell it aliased gets its own skill back.
  {
    const std::vector<st::Skill> skills = {
        {1, 1, 2, 11},  // aliases (1, 2, 0)
        {1, 2, 0, 20},
        {2, 2, 0, 30},
        {2, 1, 2, 31},  // aliases (2, 2, 0), later in the list: the game shows 30 but holds 31
    };
    Check(st::UsesThirdColumn(skills), "a third-column skill is seen");
    Check(GameTable(skills).at(st::GameSlot(2, 2, 0)) == 31, "the game's table holds the alias");
    const auto repaired = Repaired(skills);
    Check(repaired.at(st::GameSlot(1, 2, 0)) == 20 && repaired.at(st::GameSlot(2, 2, 0)) == 30,
          "each cell holds its own skill");
    const st::Table table = st::BuildTable(skills);
    Check(table.third.size() == 2 && table.third.at({1, 1}) == 11 && table.third.at({2, 1}) == 31,
          "third-column skills by pane and row");
  }

  // Navigation with two columns: the same cells as the game's handlers, whatever is hidden.
  for (int round = 0; round < 500; ++round) {
    bool shown[3][6][3] = {};
    for (auto& pane : shown) {
      for (auto& row : pane) {
        row[0] = random() % 2;
        row[1] = random() % 2;
      }
    }
    const int pane = random() % 3, row = random() % 6, column = random() % 2;
    shown[pane][row][column] = true;  // the focused cell is visible, as in the game
    const st::CellVisible two = [&](int p, int r, int c) { return c < 2 && shown[p][r][c]; };
    const st::Focus from{pane, row, column};
    Check(st::StepRight(from, 2, two) == GameRight(from, two), "right as the game's");
    Check(st::StepLeft(from, 2, two) == GameLeft(from, two), "left as the game's");
    // With a third column that is hidden throughout, the same cells too.
    Check(st::StepRight(from, 3, two) == GameRight(from, two), "right with a hidden third column");
    Check(st::StepLeft(from, 3, two) == GameLeft(from, two), "left with a hidden third column");
  }

  // Navigation with three columns reaches the third, and never loops.
  {
    const st::CellVisible all = [](int, int, int) { return true; };
    Check(st::StepRight({0, 0, 1}, 3, all) == st::Focus{0, 0, 2}, "right into the third column");
    Check(st::StepRight({2, 0, 2}, 3, all) == st::Focus{0, 0, 0}, "right wraps to the first pane");
    Check(st::StepLeft({1, 3, 0}, 3, all) == st::Focus{0, 3, 2}, "left into the previous pane's third");
    const st::CellVisible none = [](int, int, int) { return false; };
    Check(!st::StepRight({0, 0, 0}, 3, none) && !st::StepLeft({0, 0, 0}, 2, none),
          "a row with nothing visible gives nothing");
    Check(st::StepRight({1, 2, 0}, 3, [](int p, int r, int c) { return p == 1 && r == 2 && c == 0; }) ==
              st::Focus{1, 2, 0},
          "a row with one cell stays on it");
  }

  // The first visible cell, in reading order.
  {
    Check(st::FirstVisible(2, [](int, int, int) { return true; }) == st::Focus{0, 0, 0},
          "a full tree starts at the game's first cell");
    Check(st::FirstVisible(3, [](int p, int r, int c) { return (p == 2 && r == 0 && c == 1) || r == 1; }) ==
              st::Focus{2, 0, 1},
          "the top row first, whatever its pane");
    Check(st::FirstVisible(3, [](int p, int r, int c) { return p == 0 && r == 3 && c == 2; }) ==
              st::Focus{0, 3, 2},
          "a third-column cell can be the first");
    Check(!st::FirstVisible(3, [](int, int, int) { return false; }), "nothing visible: nothing");
  }

  // Areas.
  {
    Check(st::ParseOffsetRect("{{0,17},{0,24},{0,89},{0,96}}") == st::Rect{17, 24, 89, 96},
          "an offset area");
    Check(st::ParseOffsetRect("{{0,13.6},{0,19.2},{0,71.2},{0,76.8}}") == st::Rect{13.6, 19.2, 71.2, 76.8},
          "a scaled offset area");
    Check(!st::ParseOffsetRect("{{0.5,17},{0,24},{0,89},{0,96}}"), "an area with a scale is not ours");
    Check(!st::ParseOffsetRect("17,24"), "not an area");
    const auto back = st::ParseOffsetRect(st::FormatOffsetRect({13.6, 19.2, 71.25, 76.8}));
    Check(back && *back == st::Rect{13.6, 19.2, 71.25, 76.8}, "an area survives formatting");
  }

  // Three cells where the game has two: inside the pane, apart, same row.
  {
    const st::Rect first{17, 94, 89, 166}, second{109, 94, 181, 166};
    const auto cells = st::ThreeColumnCells(first, second);
    Check(cells[0].x0 >= 0 && cells[2].x1 <= 194, "the three cells fit the pane");
    Check(cells[0].x1 < cells[1].x0 && cells[1].x1 < cells[2].x0, "the cells do not overlap");
    for (const st::Rect& c : cells) {
      Check(std::fabs((c.y0 + c.y1) / 2 - 130) < 1e-9, "each cell keeps the row's centre");
      Check(std::fabs(c.x1 - c.x0 - 72 * st::kThreeColumnScale) < 1e-9, "each cell is scaled");
    }
    Check(std::fabs((cells[0].x0 + cells[2].x1) / 2 - (first.x0 + second.x1) / 2) < 1e-9,
          "centred where the two were");
    Check(st::ScaleChild({9, 6, 63, 60}, 0.5) == st::Rect{4.5, 3, 31.5, 30}, "a child scales with its cell");
  }

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("skill_tree_model_test: ok\n");
  return EXIT_SUCCESS;
}
