// The hero's hold-position flag is released while the game locks the player's input (a cutscene, a
// dialog). The game's client update sets the flag from the held input on every unlocked frame
// (guest_abi player_input::kClientUpdate), but on a locked frame it returns before that, after
// releasing only its own held-input fields; so a flag set by the attack held when a cutscene starts
// stays set for the whole cutscene. The game's moveTo refuses to move a unit with the flag set, so a
// layout path that walks the hero (the town's first cutscene: to Syl) is dropped at once and the
// hero stands still. Bug of the original 360 game (docs/regressions.md); PC Torchlight has the same
// flag and the same moveTo test, but only its stand-in-place key sets it.
//
// After each client update the hook asks the game's own input lock, and while it is locked clears
// the flag, as the game's own input release does (sub_8221AAB8). The first unlocked frame sets the
// flag again from the input still held, as on any other frame.

#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/hero_motion.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace input = torchlight::guest_abi::functions::player_input;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(input::entry.address == 0x##addr##u, "guest_functions mismatch")

}  // namespace

extern "C" {

FUNCTION_ADDRESS_CHECK(kClientUpdate, 821EBA48);
REX_EXTERN(__imp__sub_821EBA48);
REX_FUNC(sub_821EBA48) {
  const uint32_t client = ctx.r3.u32;
  __imp__sub_821EBA48(ctx, base);
  const uint32_t hero = abi::ReadU32(base, client + abi::hero_motion::kClientHero.offset);
  if (!hero || !abi::ReadU8(base, hero + input::kUnitHoldPosition.offset)) return;
  const uint32_t game = abi::ReadU32(base, client + input::kClientGame.offset);
  if (!game) return;
  torchlight::game_menu::GuestCall call(ctx, base);
  if ((call.Call(input::kInputLocked.address, {game}) & 0xFF) == 0) return;
  call.WriteU8(hero + input::kUnitHoldPosition.offset, 0);
  REXLOG_INFO("input: the hero's hold position released while the input is locked");
}

}  // extern "C"
