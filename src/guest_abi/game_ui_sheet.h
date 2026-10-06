// The game UI's root window (CEGUI GUI sheet), the full-screen layouts the game hangs on it, and
// the game UI's per-frame update: what the achievement unlock toast (achievements/toast.cpp) needs
// beyond game_ui.h, which it uses unchanged. Same conventions and evidence style as game_ui.h.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::game_ui_sheet {

using functions::GuestFunction;

// [confirmed] CGameUI::Update: r3 = CGameUI, f1 = frame time, r5 = game context. Every frame from
// the game loops (calls @0x821E6954, @0x821F600C, @0x8221B2B0); dispatches on context +5124
// (achievements.h kContextEligibility): below 1 it updates the menus (sub_8233E338 at branch target
// 0x821A4134; that callee reads the settings menu +0x3A8 and the menu list +0x17A8 of game_ui.h on
// the same r3), at 1 the in-game HUD (sub_821A83C8 at branch target 0x821A4124). Runs on the game's
// thread in every state.
inline constexpr GuestFunction kGameUiUpdate{0x821A40E0, Confidence::kConfirmed};

namespace game_ui_object {
// [confirmed] The GUI sheet: kGameUiCreateMenus creates a "DefaultWindow" named "Sheet"
// (createWindow @0x82339D20, stored @0x82339D24) and makes it CEGUI's active sheet
// (System::setGUISheet sub_825A7908 @0x82339D88, on the System at CGameUI +796). setGUISheet
// stores the sheet at System +48 and returns the old one; its only other caller is the System
// constructor (sub_825A6F30 @0x825A75DC, run from kGameUiCreateMenus @0x82339538 before the
// sheet is set), so this window is the root of everything the game UI draws while CGameUI lives.
inline constexpr Field kSheet{0x340, Confidence::kConfirmed};
// [confirmed] Full-screen layouts hung on the sheet only while they show:
// media/ui/loading.uilayout (loaded @0x82339DF8, stored @0x82339DFC; its "TipText" and
// "HOURGLASS" children are looked up next) and media/ui/ratings_splashscreen_xenon.uilayout
// (@0x82339ED0). sub_82338218 attaches them with addChildWindow(sheet, layout) (@0x823383C8 and
// @0x82338314) after testing Window::isChild (sub_825ADEE0) on the sheet, and takes them off with
// removeChildWindow (sub_825AEC60: loading @0x82338470; the splash in sub_82337D58 @0x82337E50).
inline constexpr Field kLoadingScreen{0x344, Confidence::kConfirmed};
inline constexpr Field kRatingsScreen{0x358, Confidence::kConfirmed};
}  // namespace game_ui_object

namespace window {
// [confirmed] CEGUI::Window child list, a std::vector<Window*> {begin +44, end +48}:
// Window::isChild(const Window*) (sub_825ADEE0) walks exactly that range.
inline constexpr Field kChildrenBegin{44, Confidence::kConfirmed};
inline constexpr Field kChildrenEnd{48, Confidence::kConfirmed};
}  // namespace window

// Window::isChild without a guest call: whether `child` is a direct child of `parent`.
inline bool IsChild(const uint8_t* base, uint32_t parent, uint32_t child) {
  if (!parent || !child) return false;
  const uint32_t begin = ReadU32(base, parent, window::kChildrenBegin);
  const uint32_t end = ReadU32(base, parent, window::kChildrenEnd);
  if (!begin || end < begin || end - begin > 4 * 4096) return false;  // corrupt or not a window
  for (uint32_t at = begin; at < end; at += 4) {
    if (ReadU32(base, at) == child) return true;
  }
  return false;
}

// A full-screen game layout (loading screen, ratings splash) currently covers the game UI.
inline bool Covered(const uint8_t* base, uint32_t game_ui) {
  const uint32_t sheet = ReadU32(base, game_ui, game_ui_object::kSheet);
  return IsChild(base, sheet, ReadU32(base, game_ui, game_ui_object::kLoadingScreen)) ||
         IsChild(base, sheet, ReadU32(base, game_ui, game_ui_object::kRatingsScreen));
}

}  // namespace torchlight::guest_abi::game_ui_sheet
