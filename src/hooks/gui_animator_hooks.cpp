// The midasm hook GuiAnimatorSetPosition before the bouncing-arrow animator's Window::setPosition
// @0x821A4234 (guest_abi gui_animator), declared by the generated sub_821A41A0 as
// `if (GuiAnimatorSetPosition(ctx.r3)) goto loc_821A4238;`: true skips the call. Skipped only for
// a window CEGUI does not draw (hooks/gui_window_drawn.h): the arrows of panels that are not open,
// which otherwise ask for a redraw of the whole GUI every frame. The entry's phase still steps (it
// is written before the call). What the skip leaves out for such a window: its new area and rect
// caches, onMoved (the System's redraw flag, the children's notification, EventMoved, to which no
// game code subscribes) and the System's update of the window under the mouse. When the panel is
// shown again through its own show (sub_82376A20), the game resets every entry's phase and
// position first; shown any other way (an ancestor made visible, the active sheet back), the arrow
// can be drawn for one frame where it last moved, at most 10 px from where it would be, until the
// next step moves it.

#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_abi/game_ui.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/gui_window_drawn.h"

REXCVAR_DEFINE_BOOL(native_gui_hidden_arrows, true, "Torchlight",
                    "Skip moving the bouncing arrows of GUI panels that are not drawn, which made "
                    "the whole GUI redraw every frame (false: as the game does)");
REXCVAR_DEFINE_BOOL(diag_gui_animator, false, "Torchlight",
                    "Diagnostics: count the bouncing-arrow animator's moves and those of windows "
                    "that are not drawn, and log them every 10 s");

namespace {

namespace abi = torchlight::guest_abi;

struct Counts {
  uint64_t moves = 0, not_drawn = 0;
  std::chrono::steady_clock::time_point last_log{};
};
Counts g_counts;  // the guest's render thread only (the HUD update)

}  // namespace

bool GuiAnimatorSetPosition(PPCRegister& r3) {
  const bool skip_enabled = REXCVAR_GET(native_gui_hidden_arrows);
  const bool diag = REXCVAR_GET(diag_gui_animator);
  if (!skip_enabled && !diag) return false;
  const uint8_t* base = REX_KERNEL_MEMORY()->virtual_membase();
  const uint32_t system = abi::ReadU32(base, abi::game_ui::kCeguiSystemGlobal);
  const uint32_t sheet =
      system ? abi::ReadU32(base, system + abi::game_ui::kSystemActiveSheet.offset) : 0;
  const bool drawn = torchlight::hooks::WindowDrawn(
      r3.u32, sheet, [base](uint32_t a) { return abi::ReadU8(base, a); },
      [base](uint32_t a) { return abi::ReadU32(base, a); });
  if (diag) {
    ++g_counts.moves;
    g_counts.not_drawn += !drawn;
    const auto now = std::chrono::steady_clock::now();
    if (now - g_counts.last_log >= std::chrono::seconds(10)) {
      REXLOG_INFO("diag gui animator: {} arrow moves, {} of windows not drawn ({})",
                  g_counts.moves, g_counts.not_drawn, skip_enabled ? "skipped" : "made");
      g_counts = {0, 0, now};
    }
  }
  return skip_enabled && !drawn;
}
