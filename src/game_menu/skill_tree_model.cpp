#include "game_menu/skill_tree_model.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace torchlight::game_menu::skill_tree {

namespace {

// The game's cell names ("skillbg%d_%d_%d", the builder's format @0x8235C190).
constexpr std::string_view kCellPrefix = "skillbg";

// Where the Window element whose start tag begins at `start` ends (past its </Window>); npos when
// the elements do not close.
size_t WindowEnd(std::string_view xml, size_t start) {
  int depth = 0;
  size_t pos = start;
  while ((pos = xml.find('<', pos)) != std::string_view::npos) {
    const size_t close = xml.find('>', pos);
    if (close == std::string_view::npos) return std::string_view::npos;
    const std::string_view tag = xml.substr(pos, close - pos + 1);
    if (tag.starts_with("<Window ") || tag.starts_with("<Window>")) {
      if (!tag.ends_with("/>")) ++depth;
      else if (depth == 0) return close + 1;
    } else if (tag.starts_with("</Window")) {
      if (--depth == 0) return close + 1;
    }
    pos = close + 1;
  }
  return std::string_view::npos;
}

// The pane and row of a cell named skillbg<pane>_<row>_<column> with `column`; nothing otherwise.
std::optional<std::pair<int, int>> CellOf(std::string_view name, int column) {
  if (!name.starts_with(kCellPrefix)) return std::nullopt;
  int pane = 0, row = 0, col = 0;
  char tail = 0;
  const std::string text(name.substr(kCellPrefix.size()));
  if (std::sscanf(text.c_str(), "%d_%d_%d%c", &pane, &row, &col, &tail) != 3) return std::nullopt;
  if (col != column || pane < 0 || pane >= kPanes || row < 0 || row >= kRows) return std::nullopt;
  if (text != std::to_string(pane) + "_" + std::to_string(row) + "_" + std::to_string(col)) {
    return std::nullopt;
  }
  return std::pair{pane, row};
}

}  // namespace

std::optional<std::string> AddSkillColumn(std::string_view xml) {
  // The cells of the second column (to copy) and those of the third (already there).
  struct Source {
    int pane, row;
    size_t begin, end;  // the whole element
    size_t start_tag_end;
  };
  std::vector<Source> second;
  std::set<std::pair<int, int>> third;
  for (size_t pos = 0; (pos = xml.find("<Window ", pos)) != std::string_view::npos; ++pos) {
    const size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string_view::npos) return std::nullopt;
    const std::string_view tag = xml.substr(pos, tag_end - pos);
    const size_t name_at = tag.find("Name=\"");
    if (name_at == std::string_view::npos) continue;
    const size_t name_end = tag.find('"', name_at + 6);
    if (name_end == std::string_view::npos) continue;
    const std::string_view name = tag.substr(name_at + 6, name_end - name_at - 6);
    if (auto cell = CellOf(name, kGameColumns - 1)) {
      const size_t end = WindowEnd(xml, pos);
      if (end == std::string_view::npos) return std::nullopt;
      second.push_back({cell->first, cell->second, pos, end, tag_end + 1});
    } else if (auto cell3 = CellOf(name, kColumns - 1)) {
      third.insert(*cell3);
    }
  }
  if (second.empty() || !third.empty()) return std::nullopt;

  std::string out;
  out.reserve(xml.size() + second.size() * 2048);
  size_t copied = 0;
  for (const Source& s : second) {
    out.append(xml.substr(copied, s.end - copied));
    // The copy: every "<name><pane>_<row>_1" window name in it ends in _2 instead.
    std::string cell(xml.substr(s.begin, s.end - s.begin));
    const std::string cell_suffix = std::to_string(s.pane) + "_" + std::to_string(s.row) + "_";
    for (size_t at = 0; (at = cell.find("Name=\"", at)) != std::string::npos; at += 6) {
      const size_t value = at + 6;
      const size_t end = cell.find('"', value);
      if (end == std::string::npos) break;
      const std::string_view name(cell.data() + value, end - value);
      if (name.ends_with(cell_suffix + "1")) cell[end - 1] = '2';
    }
    // Hidden until the builder shows it for a skill (the game hides its own cells first,
    // sub_8235BD40).
    const size_t start_tag = s.start_tag_end - s.begin;
    cell.insert(start_tag, "<Property Name=\"Visible\" Value=\"False\" />");
    out.append(cell);
    copied = s.end;
  }
  out.append(xml.substr(copied));
  return out;
}

int GameSlot(int pane, int row, int column) {
  const int slot = 12 * pane + 2 * row + column;
  if (slot < 0) return -1;
  return std::min(slot, 35);
}

Table BuildTable(const std::vector<Skill>& skills) {
  Table table;
  for (const Skill& s : skills) {
    if (s.column == kColumns - 1 && s.pane >= 0 && s.pane < kPanes && s.row >= 0 && s.row < kRows) {
      table.third[{s.pane, s.row}] = s.id;
      continue;
    }
    if (s.column < 0 || s.column >= kGameColumns) continue;
    const int slot = GameSlot(s.pane, s.row, s.column);
    if (slot >= 0) table.slots.push_back({slot, s.id});
  }
  return table;
}

bool UsesThirdColumn(const std::vector<Skill>& skills) {
  return std::any_of(skills.begin(), skills.end(), [](const Skill& s) {
    return s.column == kColumns - 1 && s.pane >= 0 && s.pane < kPanes && s.row >= 0 &&
           s.row < kRows;
  });
}

std::optional<Focus> StepRight(Focus from, int columns, const CellVisible& visible) {
  Focus f = from;
  for (int step = 0; step < kPanes * columns; ++step) {
    if (++f.column >= columns) {
      f.column = 0;
      if (++f.pane >= kPanes) f.pane = 0;
    }
    if (visible(f.pane, f.row, f.column)) return f;
  }
  return std::nullopt;
}

std::optional<Focus> StepLeft(Focus from, int columns, const CellVisible& visible) {
  Focus f = from;
  for (int step = 0; step < kPanes * columns; ++step) {
    if (--f.column < 0) {
      f.column = columns - 1;
      if (--f.pane < 0) f.pane = kPanes - 1;
    }
    if (visible(f.pane, f.row, f.column)) return f;
  }
  return std::nullopt;
}

std::optional<Focus> FirstVisible(int columns, const CellVisible& visible) {
  for (int row = 0; row < kRows; ++row) {
    for (int pane = 0; pane < kPanes; ++pane) {
      for (int column = 0; column < columns; ++column) {
        if (visible(pane, row, column)) return Focus{pane, row, column};
      }
    }
  }
  return std::nullopt;
}

std::optional<Rect> ParseOffsetRect(std::string_view area) {
  double v[8];
  const std::string text(area);
  if (std::sscanf(text.c_str(), " { { %lf , %lf } , { %lf , %lf } , { %lf , %lf } , { %lf , %lf } }",
                  &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 8) {
    return std::nullopt;
  }
  if (v[0] != 0 || v[2] != 0 || v[4] != 0 || v[6] != 0) return std::nullopt;
  return Rect{v[1], v[3], v[5], v[7]};
}

std::string FormatOffsetRect(const Rect& r) {
  char text[128];
  std::snprintf(text, sizeof(text), "{{0,%g},{0,%g},{0,%g},{0,%g}}", r.x0, r.y0, r.x1, r.y1);
  return text;
}

std::array<Rect, 3> ThreeColumnCells(const Rect& first, const Rect& second) {
  const double width = (first.x1 - first.x0) * kThreeColumnScale;
  const double height = (first.y1 - first.y0) * kThreeColumnScale;
  const double gap = (first.x1 - first.x0) * kThreeColumnGap;
  const double left = (first.x0 + second.x1) / 2 - (3 * width + 2 * gap) / 2;
  const double middle = (first.y0 + first.y1) / 2;
  std::array<Rect, 3> cells;
  for (int i = 0; i < 3; ++i) {
    const double x0 = left + i * (width + gap);
    cells[i] = {x0, middle - height / 2, x0 + width, middle + height / 2};
  }
  return cells;
}

Rect ScaleChild(const Rect& child, double scale) {
  return {child.x0 * scale, child.y0 * scale, child.x1 * scale, child.y1 * scale};
}

}  // namespace torchlight::game_menu::skill_tree
