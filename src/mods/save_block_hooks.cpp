// No saving in a session where the game did not load units that saves hold (save_units_install.h,
// SavingBlocked): the game drops a saved item it does not know when it loads the character, and
// its next save would write the character without it. The character save (guest_abi save_menu.h
// kSaveCharacter) is the only code that writes save data, the stash included, and its callers
// do not read its result, so it is skipped whole: nothing is written, nothing renamed, and the game
// goes on as after a save. The player is told at the character list (save_units_notice.h).

#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/save_menu.h"
#include "mods/save_units_install.h"

static_assert(torchlight::guest_abi::save_menu::kSaveCharacter.address == 0x8221CC70);

REX_EXTERN(__imp__sub_8221CC70);

extern "C" {

REX_FUNC(sub_8221CC70) {
  if (torchlight::mods::SavingBlocked()) {
    REXLOG_WARN("units: character not saved: saving is off for this session (saves hold units the game did not "
                "load)");
    return;
  }
  __imp__sub_8221CC70(ctx, base);
}

}  // extern "C"
