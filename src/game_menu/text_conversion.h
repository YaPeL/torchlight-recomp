// Text conversions the game's own get wrong for scripts beyond Latin-1.

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace torchlight::game_menu {

// UTF-16 to Unicode code points (what a CEGUI::String holds): surrogate pairs combined, a lone
// surrogate replaced by U+FFFD. The game's own conversion keeps only the low 8 bits of each unit
// (guest_abi/game_ui.h kWStringToCeguiString).
std::vector<uint32_t> Utf16ToCodePoints(std::u16string_view text);

}  // namespace torchlight::game_menu
