#include "game_menu/class_selector_model.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace torchlight::game_menu::class_selector {

namespace {

constexpr std::string_view kLabelPrefix = "tl_class_label_";

std::string Upper(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = char(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

std::string Attribute(std::string_view tag, std::string_view name) {
  const std::string key = std::string(name) + "=\"";
  const size_t at = tag.find(key);
  if (at == std::string_view::npos) return {};
  const size_t begin = at + key.size();
  const size_t end = tag.find('"', begin);
  return end == std::string_view::npos ? std::string() : std::string(tag.substr(begin, end - begin));
}

// Where the Window element starting at `start` closes (the position of its "</Window"); npos when
// it does not, or when it closes itself.
size_t ClosingTag(std::string_view xml, size_t start) {
  int depth = 0;
  for (size_t pos = start; (pos = xml.find('<', pos)) != std::string_view::npos; ++pos) {
    const size_t close = xml.find('>', pos);
    if (close == std::string_view::npos) return std::string_view::npos;
    const std::string_view tag = xml.substr(pos, close - pos + 1);
    if (tag.starts_with("<Window")) {
      if (tag.ends_with("/>")) {
        if (depth == 0) return std::string_view::npos;
      } else {
        ++depth;
      }
    } else if (tag.starts_with("</Window")) {
      if (--depth == 0) return pos;
    }
  }
  return std::string_view::npos;
}

// A class button's label: the game's Serif font in the frame's lower part, wrapped, as wide as the
// button but for its frame's border (4 px each side: a long single word such as "Enchantress" was cut
// at 80% of the width). A pet button is
// 48 px (charactercreate.uilayout): SerifSmall, centred over the button's whole height and over the
// frame around it (9 px each side), not clipped by the button. The formatting properties and their
// values are ceguiWidget/StaticText's (ceguiWidgets.looknfeel: HorzTextFormatting, VertFormatting,
// Falagard's CentreAligned family); an unknown value leaves the text on one line.
std::string Label(std::string_view button, bool pet) {
  return "<Window Type=\"ceguiWidget/StaticText\" Name=\"" + LabelName(button) + "\" >"
         "<Property Name=\"Font\" Value=\"" + (pet ? "SerifSmall" : "Serif") + "\" />"
         "<Property Name=\"Text\" Value=\"\" />"
         "<Property Name=\"Visible\" Value=\"False\" />"
         "<Property Name=\"AlwaysOnTop\" Value=\"True\" />"
         "<Property Name=\"HorzTextFormatting\" Value=\"WordWrapCentreAligned\" />" +
         (pet ? "<Property Name=\"VertFormatting\" Value=\"CentreAligned\" />"
                "<Property Name=\"ClippedByParent\" Value=\"False\" />"
                "<Property Name=\"UnifiedAreaRect\" Value=\"{{0,-9},{0,0},{1,9},{1,0}}\" />"
              : "<Property Name=\"UnifiedAreaRect\" Value=\"{{0,4},{0.3,0},{1,-4},{0.9,0}}\" />") +
         "<Property Name=\"MousePassThroughEnabled\" Value=\"True\" />"
         "</Window>";
}

}  // namespace

std::string LabelName(std::string_view button) { return std::string(kLabelPrefix) + std::string(button); }

std::optional<std::string> AddClassLabels(std::string_view xml) {
  if (xml.find(kLabelPrefix) != std::string_view::npos) return std::nullopt;
  struct Insert {
    size_t at;
    std::string text;
  };
  std::vector<Insert> inserts;
  for (size_t pos = 0; (pos = xml.find("<Window", pos)) != std::string_view::npos; ++pos) {
    const size_t tag_end = xml.find('>', pos);
    if (tag_end == std::string_view::npos) return std::nullopt;
    const std::string name = Attribute(xml.substr(pos, tag_end - pos), "Name");
    if (name.empty()) continue;
    const size_t closing = ClosingTag(xml, pos);
    if (closing == std::string_view::npos) continue;
    // The window's own properties: before its first child window.
    const size_t own_end = std::min(xml.find("<Window", tag_end), closing);
    std::string command;
    for (size_t p = tag_end; (p = xml.find("<Property", p)) != std::string_view::npos && p < own_end; ++p) {
      const std::string_view tag = xml.substr(p, xml.find('>', p) - p);
      if (Upper(Attribute(tag, "Name")) == "ONCLICK") {
        command = Upper(Attribute(tag, "Value"));
        break;
      }
    }
    if (command == "GUISELECT1" || command == "GUISELECT2") inserts.push_back({closing, Label(name, command == "GUISELECT2")});
  }
  if (inserts.empty()) return std::nullopt;
  std::string out(xml);
  std::sort(inserts.begin(), inserts.end(), [](const Insert& a, const Insert& b) { return a.at > b.at; });
  for (const Insert& insert : inserts) out.insert(insert.at, insert.text);
  return out;
}

}  // namespace torchlight::game_menu::class_selector
