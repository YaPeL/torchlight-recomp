// Whether CEGUI draws a window: it and every ancestor visible (guest_abi game_ui kWindowVisible)
// and its top ancestor the active sheet (kSystemActiveSheet), which is all System::renderGUI
// renders. Pure over two guest readers, tested in gui_window_drawn_test.

#pragma once

#include <cstdint>

#include "guest_abi/game_ui.h"

namespace torchlight::hooks {

template <typename Read8, typename Read32>
bool WindowDrawn(uint32_t window, uint32_t active_sheet, Read8 read8, Read32 read32) {
  namespace ui = guest_abi::game_ui;
  // A parent chain deeper than any of the game's layouts (or a cycle) counts as not drawn.
  for (int depth = 0; window != 0 && depth < 64; ++depth) {
    if (read8(window + ui::kWindowVisible.offset) == 0) return false;
    if (window == active_sheet) return true;
    window = read32(window + ui::kWindowParent.offset);
  }
  return false;
}

}  // namespace torchlight::hooks
