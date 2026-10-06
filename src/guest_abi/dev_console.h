// The guest's developer command executor (the "console" commands such as FAME, DESCEND, LEVELUP),
// for the development-only --dev_guest_command (dev/guest_command.h). Same conventions and evidence
// style as game_ui.h.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::dev_console {

using functions::GuestFunction;

// [confirmed] The console object: kGameUiCreateMenus allocates it (1112 bytes), builds it with
// 0x8234A5B8 and stores it at CGameUI +5312 (@0x8233CFD4), unconditionally.
inline constexpr Field kConsole{5312, Confidence::kConfirmed};
// [confirmed] Command executor: r3 = console, r4 = std::wstring* taken by value (the callee
// destroys it; the HUD builds it with kWStringCopy from the typed text and only destroys its own
// source, @0x821AB8B4..@0x821AB8C4). It compares the first word with HELP, FAME, DESCEND, LEVELUP...;
// FAME adds to the player's fame points (+948 of *(*(console+48)+44)) and marks the character with
// 0x8234A438 (+1721 = 214 and a suffix on the name); ranks follow at the next real fame gain.
inline constexpr GuestFunction kExecuteCommand{0x8234BE20, Confidence::kConfirmed};
// [confirmed] std::wstring copy constructor: r3 = destination, r4 = source (empty init, then
// assign 0x821EDB78).
inline constexpr GuestFunction kWStringCopy{0x8220ED78, Confidence::kConfirmed};

}  // namespace torchlight::guest_abi::dev_console
