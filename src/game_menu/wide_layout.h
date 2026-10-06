// The game's full-screen frames on screens wider than 16:9 (TLR-006).
//
// Menus and loading screens draw a band along the top or bottom edge from a centred image of fixed
// width plus two end pieces anchored to the screen's edges; at 1280 pixels they overlap into one
// band, on a wider screen they leave gaps. WidenBands anchors those end pieces to the edges of a
// centred 16:9 area instead, keeping the overlap they had at 1280. Structural only: a band end is
// an image of absolute width aligned left or right in a full-width parent, touching the top or
// bottom edge, that overlaps (laid out at 1280x720) a centred image of fixed width touching the
// same edge. Nothing is matched by name. At 16:9 the result is the layout's own geometry.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::game_menu {

struct WidenedLayout {
  std::string xml;
  std::vector<std::string> windows;  // the band ends moved, by window name
};

// The layout with its band ends re-anchored, or nothing when it has none (or cannot be read).
std::optional<WidenedLayout> WidenBands(std::string_view xml);

}  // namespace torchlight::game_menu
