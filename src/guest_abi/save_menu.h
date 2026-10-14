// The guest's "load character" menu and save storage, for the in-game import of PC saves
// (game_menu/save_import_menu.cpp, docs/saves-research.md section 4d), with their evidence.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"

namespace torchlight::guest_abi::save_menu {

using functions::GuestFunction;

// CContinueGameMenu, the "load character" menu: vtable 0x820D21D8 (MSVC RTTI); its slots follow the
// CDropdownMenu base (game_ui.h).
// [confirmed] Slot 3, Update(dt in f1): vtable 0x820D21D8 + 0x0C = 0x8238A138.
inline constexpr GuestFunction kUpdate{0x8238A138, Confidence::kConfirmed};
// [confirmed] Slot 7, SetOpen(menu in r3, bool open in r4): vtable + 0x1C = 0x823874D8. Opening,
// it builds the list (kBuildList with r4 = r5 = 0, @0x82387500), opens the base menu
// (0x82351FF0), and then calls kAfterList(menu) (@0x8238752C) and kRefresh(menu, 0, 1)
// (@0x8238753C).
inline constexpr GuestFunction kSetOpen{0x823874D8, Confidence::kConfirmed};
// [confirmed] Builds the character list: enumerates SAVE:\*.tsv (pattern L"*" + the global
// L".tsv" at 0x83412AA8), reads and checks each file. Called by kSetOpen and, after deleting a
// character, by sub_82389FF0 (same arguments: menu, 0, 0).
inline constexpr GuestFunction kBuildList{0x82387580, Confidence::kConfirmed};
// [confirmed] Called with the menu right after the list is built, by kSetOpen and by
// sub_82389FF0 (@0x8238A0F4 there). Its own purpose was not traced; the import repeats the
// game's sequence as is.
inline constexpr GuestFunction kAfterList{0x82388910, Confidence::kConfirmed};
// [confirmed] Refreshes the list widgets: (menu, selected index, 1), from kSetOpen with index 0 and
// from sub_82389FF0 with the current one.
inline constexpr GuestFunction kRefresh{0x82389D30, Confidence::kConfirmed};

// [confirmed] Mounts the save container as SAVE: (XamContentCreate, flags 0x14 OPEN_ALWAYS, name
// "torchlight.sav"); returns 0 without mounting while 0x8355A264 is set (the "Corrupt/Damaged
// Save" box was answered "No"). Every save operation brackets itself with it and kUnmount.
inline constexpr GuestFunction kMountSaves{0x823AC618, Confidence::kConfirmed};
// [confirmed] Unmounts what kMountSaves mounted.
inline constexpr GuestFunction kUnmountSaves{0x823AC7B8, Confidence::kConfirmed};

// [confirmed] Saves the current character (r3 = the game, r4 and r5 flags), the only code that
// writes save data. It writes the shared stash first (kWriteStash @0x8221CCA4, its only caller),
// mounts the container (@0x8221CD54) and writes the character to "save.tmp" (opened "wb" through
// sub_823A22A8 @0x8221CE70); when that open fails it builds "Unable to save character to :" with
// the system error and unmounts (@0x8221CE7C..@0x8221CFC8), with no dialog. On success it deletes
// "backup.tmp" (sub_8285D068 @0x8221DA64), renames N.TSV to "backup.tmp" and "save.tmp" to N.TSV
// (sub_8285D0B8 @0x8221DA90, @0x8221DABC). Its seven callers (sub_821E5910, sub_821F5CA0,
// sub_8220AAD8, sub_82212950, sub_82214818 twice, sub_822D31F8, sub_8234BE20) do not read its
// result. Of the other kMountSaves callers only the character delete writes (sub_82389FF0, a file
// delete through sub_8287DE58, on the player's request) and the storage state machine
// sub_82195AA8 (XamContentDelete, on "Yes" to "Corrupt/Damaged Save"); the rest read or list.
inline constexpr GuestFunction kSaveCharacter{0x8221CC70, Confidence::kConfirmed};
// [confirmed] Writes the shared stash (sharedstash.bin); called only by kSaveCharacter.
inline constexpr GuestFunction kWriteStash{0x82326650, Confidence::kConfirmed};

// [confirmed] Import thunk of XamShowMessageBoxUI (registered at 0x8302675C). Nine arguments: the
// ninth (the XOVERLAPPED) goes on the stack at r1 + 0x54, as the game's wrapper sub_8287D8E0
// passes it (lwz r11,180(r1); stw r11,84(r1) before bl 0x8302675C), and the runtime reads
// arguments past the eighth from r1 + 0x54 + 8 * (n - 8).
inline constexpr GuestFunction kXamShowMessageBoxUI{0x8302675C, Confidence::kConfirmed};
inline constexpr uint32_t kStackArgumentOffset = 0x54;

}  // namespace torchlight::guest_abi::save_menu
