// The save indicator's minimum display time (CGameUI::ShowSaveIndicator, guest_abi/game_ui.h).
//
// Closing the indicator, the game sleeps on its render thread until it has been up for 1 s (or 3 s
// once the save took over 1 s): the Xbox 360's certification minimum. The save is written before
// it closes, and on the host it takes a few milliseconds, so each autosave and zone change froze
// the game for about a second. The PC version has no such wait (its only Sleep call is the C
// runtime's startup). Only that Sleep is skipped; the indicator opens and closes as before.
//
// The full autosave notice (shown once per session) keeps its 5 s minimum: the player reads it.
// Difference with the Xbox 360, see docs/ARCHITECTURE.md.

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/game_ui.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace fn = torchlight::guest_abi::functions;
namespace ui = torchlight::guest_abi::game_ui;

#define FUNCTION_ADDRESS_CHECK(ns, entry, addr) \
  static_assert(ns::entry.address == 0x##addr##u, "guest_abi mismatch")

// Set while ShowSaveIndicator closes the icon (not the full notice), on the thread running it.
thread_local bool skip_minimum_sleep = false;

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(ui, kShowSaveIndicator, 82344EE0);
REX_EXTERN(__imp__sub_82344EE0);
REX_FUNC(sub_82344EE0) {
  const bool open = (ctx.r4.u32 & 0xFF) != 0;
  const uint32_t menu = abi::ReadU32(base, ctx.r3.u32 + ui::game_ui_object::kSaveMenu.offset);
  // Read before the call: closing the menu clears it once the full notice was shown.
  const bool full_notice =
      menu != 0 && abi::ReadBool(base, menu + ui::save_menu::kFullNoticePending.offset);
  skip_minimum_sleep = !open && menu != 0 && !full_notice;
  __imp__sub_82344EE0(ctx, base);
  skip_minimum_sleep = false;
}

FUNCTION_ADDRESS_CHECK(fn, kSleep, 8287D878);
REX_EXTERN(__imp__sub_8287D878);
REX_FUNC(sub_8287D878) {
  if (skip_minimum_sleep && uint32_t(ctx.lr) == ui::kShowSaveIndicatorSleepReturn) {
    REXLOG_INFO("save indicator: skipped the {} ms minimum display time", ctx.r3.u32);
    return;
  }
  __imp__sub_8287D878(ctx, base);
}

}  // extern "C"
