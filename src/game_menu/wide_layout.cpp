#include "game_menu/wide_layout.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>

namespace torchlight::game_menu {

namespace {

// The layouts' base screen: absolute offsets are pixels of a 1280x720 screen (the game scales them
// by its X_RATIO/Y_RATIO when it loads a layout).
constexpr double kBaseWidth = 1280, kBaseHeight = 720;
constexpr double kEpsilon = 0.5;

// A Value="..." attribute: where its text is in the layout.
struct Attribute {
  std::string value;
  size_t begin = 0, end = 0;
};

struct Window {
  std::string type, name;
  Attribute area;  // UnifiedAreaRect
  std::optional<Attribute> horizontal, vertical, image;
  bool has_area = false;
  std::vector<std::unique_ptr<Window>> children;
  // Laid out at 1280x720.
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool absolute_x = false;  // left and right edges are pixel offsets only
};

struct UDim {
  double scale = 0, offset = 0;
};
// {{xs,xo},{ys,yo},{xs2,xo2},{ys2,yo2}}
bool ParseArea(const std::string& text, UDim out[4]) {
  double v[8];
  if (std::sscanf(text.c_str(), " { { %lf , %lf } , { %lf , %lf } , { %lf , %lf } , { %lf , %lf } }",
                  &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) != 8) {
    return false;
  }
  for (int i = 0; i < 4; ++i) out[i] = {v[2 * i], v[2 * i + 1]};
  return true;
}

// The value of attribute `name` inside [begin, end), or nothing.
std::optional<Attribute> FindAttribute(std::string_view xml, size_t begin, size_t end,
                                       std::string_view name) {
  const std::string key = std::string(name) + "=\"";
  const size_t at = xml.substr(0, end).find(key, begin);
  if (at == std::string_view::npos) return std::nullopt;
  const size_t value_begin = at + key.size();
  const size_t value_end = xml.find('"', value_begin);
  if (value_end == std::string_view::npos || value_end > end) return std::nullopt;
  return Attribute{std::string(xml.substr(value_begin, value_end - value_begin)), value_begin,
                   value_end};
}

// Reads the Window tree (the subset CEGUI layouts use: Window elements with Property children).
bool Parse(std::string_view xml, std::vector<std::unique_ptr<Window>>& roots) {
  std::vector<Window*> stack;
  size_t pos = 0;
  while ((pos = xml.find('<', pos)) != std::string_view::npos) {
    const size_t close = xml.find('>', pos);
    if (close == std::string_view::npos) return false;
    const std::string_view tag = xml.substr(pos, close - pos + 1);
    if (tag.starts_with("<Window ") || tag.starts_with("<Window>")) {
      auto w = std::make_unique<Window>();
      if (auto t = FindAttribute(xml, pos, close, "Type")) w->type = t->value;
      if (auto n = FindAttribute(xml, pos, close, "Name")) w->name = n->value;
      Window* raw = w.get();
      (stack.empty() ? roots : stack.back()->children).push_back(std::move(w));
      if (!tag.ends_with("/>")) stack.push_back(raw);
    } else if (tag.starts_with("</Window")) {
      if (stack.empty()) return false;
      stack.pop_back();
    } else if (tag.starts_with("<Property ") && !stack.empty()) {
      auto name = FindAttribute(xml, pos, close, "Name");
      auto value = FindAttribute(xml, pos, close, "Value");
      Window& w = *stack.back();
      if (name && value) {
        if (name->value == "UnifiedAreaRect") {
          w.area = *value;
          w.has_area = true;
        } else if (name->value == "HorizontalAlignment") {
          w.horizontal = value;
        } else if (name->value == "VerticalAlignment") {
          w.vertical = value;
        } else if (name->value == "Image") {
          w.image = value;
        }
      }
    }
    pos = close + 1;
  }
  return stack.empty();
}

// CEGUI 0.6 placement: the position is relative to the alignment's anchor.
void Layout(Window& w, double px0, double py0, double px1, double py1) {
  const double pw = px1 - px0, ph = py1 - py0;
  UDim a[4] = {{0, 0}, {0, 0}, {1, 0}, {1, 0}};
  if (w.has_area && !ParseArea(w.area.value, a)) a[0] = a[1] = UDim{}, a[2] = a[3] = UDim{1, 0};
  const double x = a[0].scale * pw + a[0].offset, y = a[1].scale * ph + a[1].offset;
  const double width = (a[2].scale - a[0].scale) * pw + (a[2].offset - a[0].offset);
  const double height = (a[3].scale - a[1].scale) * ph + (a[3].offset - a[1].offset);
  const std::string h = w.horizontal ? w.horizontal->value : "Left";
  const std::string v = w.vertical ? w.vertical->value : "Top";
  w.x0 = px0 + (h == "Centre" ? (pw - width) / 2 + x : h == "Right" ? pw - width + x : x);
  w.y0 = py0 + (v == "Centre" ? (ph - height) / 2 + y : v == "Bottom" ? ph - height + y : y);
  w.x1 = w.x0 + width;
  w.y1 = w.y0 + height;
  w.absolute_x = a[0].scale == 0 && a[2].scale == 0;
  for (auto& c : w.children) Layout(*c, w.x0, w.y0, w.x1, w.y1);
}

bool IsImage(const Window& w) {
  return w.image && w.type.size() >= 11 && w.type.ends_with("StaticImage");
}
bool AtTop(const Window& w) { return std::fabs(w.y0) <= kEpsilon; }
bool AtBottom(const Window& w) { return std::fabs(w.y1 - kBaseHeight) <= kEpsilon; }

void Collect(Window& w, std::vector<Window*>& all) {
  all.push_back(&w);
  for (auto& c : w.children) Collect(*c, all);
}

// Band ends among `parent`'s children.
void FindEnds(Window& parent, const std::vector<Window*>& all,
              std::vector<std::pair<Window*, Window*>>& ends) {
  const bool full_width =
      std::fabs(parent.x0) <= kEpsilon && std::fabs(parent.x1 - kBaseWidth) <= kEpsilon;
  for (auto& child : parent.children) {
    Window& e = *child;
    const std::string h = e.horizontal ? e.horizontal->value : "Left";
    if (full_width && IsImage(e) && e.absolute_x && (h == "Left" || h == "Right") &&
        (AtTop(e) || AtBottom(e))) {
      for (Window* c : all) {
        if (c == &e || !IsImage(*c)) continue;
        const double width = c->x1 - c->x0;
        const bool centred = std::fabs((c->x0 + c->x1) / 2 - kBaseWidth / 2) <= kEpsilon + 0.5;
        const bool same_edge = (AtTop(e) && AtTop(*c)) || (AtBottom(e) && AtBottom(*c));
        const bool overlaps = e.x0 < c->x1 && c->x0 < e.x1;
        if (centred && width < kBaseWidth - kEpsilon && same_edge && overlaps) {
          ends.push_back({&parent, &e});
          break;
        }
      }
    }
    FindEnds(e, all, ends);
  }
}

std::string Number(double v) {
  char text[32];
  std::snprintf(text, sizeof(text), "%g", v);
  return text;
}

}  // namespace

std::optional<WidenedLayout> WidenBands(std::string_view xml) {
  std::vector<std::unique_ptr<Window>> roots;
  if (!Parse(xml, roots)) return std::nullopt;
  std::vector<Window*> all;
  for (auto& r : roots) {
    Layout(*r, 0, 0, kBaseWidth, kBaseHeight);
    Collect(*r, all);
  }
  std::vector<std::pair<Window*, Window*>> ends;
  for (auto& r : roots) FindEnds(*r, all, ends);
  if (ends.empty()) return std::nullopt;

  // Edits from the end of the text backwards, so earlier positions stay valid.
  struct Edit {
    size_t begin, end;
    std::string text;
  };
  std::vector<Edit> edits;
  WidenedLayout out;
  for (auto& [parent, e] : ends) {
    UDim a[4];
    if (!e->has_area || !ParseArea(e->area.value, a)) continue;
    // Relative to a full-width parent at x 0: offsets from its centre.
    const double left = e->x0 - parent->x0 - kBaseWidth / 2;
    const double right = e->x1 - parent->x0 - kBaseWidth / 2;
    edits.push_back({e->area.begin, e->area.end,
                     "{{0.5," + Number(left) + "},{" + Number(a[1].scale) + "," +
                         Number(a[1].offset) + "},{0.5," + Number(right) + "},{" +
                         Number(a[3].scale) + "," + Number(a[3].offset) + "}}"});
    if (e->horizontal && e->horizontal->value != "Left") {
      edits.push_back({e->horizontal->begin, e->horizontal->end, "Left"});
    }
    out.windows.push_back(e->name);
  }
  std::sort(edits.begin(), edits.end(), [](const Edit& a, const Edit& b) { return a.begin > b.begin; });
  out.xml = std::string(xml);
  for (const Edit& edit : edits) out.xml.replace(edit.begin, edit.end - edit.begin, edit.text);
  return out;
}

}  // namespace torchlight::game_menu
