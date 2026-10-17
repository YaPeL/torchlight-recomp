// The guest's controller reads: the XInput wrappers the game calls, and the structures they fill
// (the Xbox SDK's XINPUT_STATE / XINPUT_KEYSTROKE, big-endian in guest memory). Used to swap
// confirm and cancel on Nintendo pads (game_menu/pad_swap.cpp, mods/pad_prompts.h).

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"

namespace torchlight::guest_abi::xinput {

using functions::GuestFunction;

// [confirmed] XInputGetState(user r3, state r4): moves the state pointer to r5, sets the flags r4 =
// 1 and tail-calls XamInputGetState (@0x821FEA58..@0x821FEA60). Its two callers are the game's
// only reads of the pad state.
inline constexpr GuestFunction kGetState{0x821FEA58, Confidence::kConfirmed};
// [confirmed] XInputGetKeystroke(user r3, flags r4, keystroke r5): user 255 (any) becomes 0 with
// flag 0x40000000 (@0x821FEA74..@0x821FEA84), then XamInputGetKeystrokeEx(&user, r4, r5)
// (@0x821FEA90); r5 is passed through unchanged. Its two callers are the game's only keystroke
// reads.
inline constexpr GuestFunction kGetKeystroke{0x821FEA68, Confidence::kConfirmed};

// XINPUT_STATE: dwPacketNumber (u32), then XINPUT_GAMEPAD whose first field is wButtons (u16).
inline constexpr uint32_t kStateButtons = 4;
inline constexpr uint16_t kButtonA = 0x1000;
inline constexpr uint16_t kButtonB = 0x2000;
// XINPUT_KEYSTROKE: VirtualKey (u16) first.
inline constexpr uint32_t kKeystrokeVirtualKey = 0;
inline constexpr uint16_t kVkPadA = 0x5800;
inline constexpr uint16_t kVkPadB = 0x5801;
// The functions' result: ERROR_SUCCESS when the structure was filled.
inline constexpr uint32_t kSuccess = 0;

}  // namespace torchlight::guest_abi::xinput
