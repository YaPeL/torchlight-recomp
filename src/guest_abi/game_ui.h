// The game's own UI layer (Runic, on the guest's CEGUI 0.6): menus, their layouts, commands and
// gamepad navigation, plus the few CEGUI and OGRE resource entry points a host-added menu needs.
//
// Class names come from MSVC RTTI (tools/guest_re/rtti_vtable.py). There are no reference
// headers for Runic's classes, so every entry cites the recompiled code that shows it. Calling
// conventions as in ogre_layout.h (this in r3, by-value class returns in r3 with this in r4).

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::game_ui {

using functions::GuestFunction;

// ---------------------------------------------------------------------------------------------
// Layout files. The game's menus load ".uilayout" files, which are CEGUI XML (<GUILayout>, read
// by the guest CEGUI's Expat parser; GUILayout_xmlHandler::elementStart is sub_825DA550). The
// ".layout" files beside them in the pak are Runic's editor format ("[UI] VERSION:3") and are
// not loaded by these menus.
//
// Per-menu load sequence, as in CSettingsMenuXenon's init (sub_823802F8) and COptionsMenu's
// (sub_82353960):
//   1. kUiResolveLayout(ui_manager, path, out, 0, 1, 0) with the UI manager global (@0x82380338);
//   2. kLoadWindowLayout(window_manager, name built from `out`) returns the root window
//      (@0x82380384);
//   3. kGameUiScaleLayout(game_ui, root, 0), kGameUiBindCommands(game_ui, root) and
//      kMenuBindEvents(menu, root) (@0x823803A0..@0x823803B8); the root is stored at menu+0x0C.
// [confirmed] Runic UI manager global (loaded @0x82380338 and @0x823539A0).
inline constexpr uint32_t kUiManagerGlobal = 0x83559514;
// [confirmed] CEGUI::WindowManager singleton pointer (@0x82380380, and in
// GUILayout_xmlHandler::elementWindowStart @0x825DA9D4).
inline constexpr uint32_t kWindowManagerGlobal = 0x8350678C;
inline constexpr GuestFunction kUiResolveLayout{0x8239D5F8, Confidence::kConfirmed};
inline constexpr GuestFunction kLoadWindowLayout{0x825B81C8, Confidence::kConfirmed};
// [confirmed] kGameUiScaleLayout(game_ui, window, axis_flag): recursive over the children
// (@0x82338828..@0x82338844); scales each window's area by an aspect factor from the game's float
// settings (index 0x83558F54 with flag 0, 0x83558F58 otherwise, @0x823388A0..@0x823388D4, then
// @0x82338924) and passes texts through the translator (global 0x835594E8, @0x82338C08). A
// window added to a loaded layout needs it too, or it keeps its unscaled size and place.
inline constexpr GuestFunction kGameUiScaleLayout{0x823387F0, Confidence::kConfirmed};
inline constexpr GuestFunction kGameUiBindCommands{0x82338E88, Confidence::kConfirmed};
inline constexpr GuestFunction kMenuBindEvents{0x82352278, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// CEGUI entry points.
// [confirmed] Unnamed windows get a generated name (sub_825B8248, "__cewin_uid_" + counter at
// window manager +0x20) that this build turns into clashing names: loading a layout of 20 unnamed
// windows hit createWindow's "name already registered" assert (@0x825B7BE4) with the name "27_".
// Our layouts name every window.
// [confirmed] WindowManager::createWindow(type, name, prefix): r3 = window manager, r4/r5 =
// String*, r6 = prefix; returns the Window* (GUILayout_xmlHandler::elementWindowStart
// sub_825DA940 @0x825DA9D8).
inline constexpr GuestFunction kCreateWindow{0x825B7B48, Confidence::kConfirmed};
// [confirmed] Window::addChildWindow(Window*): r3 = parent (elementWindowStart @0x825DAA00, on the
// window at the top of the handler's stack).
inline constexpr GuestFunction kAddChildWindow{0x825AEBC0, Confidence::kConfirmed};
// [confirmed] Child lookup by name: r3 = window, r4 = String*; returns Window* (the menus look up
// "Title", "ShowTips", ... with it, @0x823803E0, @0x82380408). Null when absent: the result goes
// straight into a dynamic_cast and a null check (@0x8238042C..@0x82380448).
inline constexpr GuestFunction kFindChildWindow{0x825AE128, Confidence::kConfirmed};
// [confirmed] MSVC __RTDynamicCast(ptr, vfdelta, src type, dst type, is_reference) (@0x8238042C).
inline constexpr GuestFunction kDynamicCast{0x821E1828, Confidence::kConfirmed};
// [confirmed] RTTI type descriptors (names read at +8: ".?AVWindow@CEGUI@@",
// ".?AVCheckbox@CEGUI@@").
inline constexpr uint32_t kTypeDescriptorWindow = 0x834C570C;
inline constexpr uint32_t kTypeDescriptorCheckbox = 0x834C5728;
// [confirmed] Checkbox::setSelected(bool): r3 = checkbox, r4 = state (@0x82380490). The selected
// state is the byte at +0x47C: the settings menu toggles with setSelected(!byte) (@0x8237FE60).
inline constexpr GuestFunction kCheckboxSetSelected{0x825CEED0, Confidence::kConfirmed};
inline constexpr Field kCheckboxSelected{0x47C, Confidence::kConfirmed};
// [confirmed] Parent window: Window::addChild_impl (CEGUI::Window vtable 0x8210119C slot 63,
// 0x825AF3B8) removes the child from its old parent (+0x50, @0x825AF3D0..@0x825AF3E4) and stores
// itself there (@0x825AF418).
inline constexpr Field kWindowParent{0x50, Confidence::kConfirmed};
// [confirmed] Window name (CEGUI String; the menus take c_str of it to report clicks,
// @0x823525B8).
inline constexpr Field kWindowName{0x348, Confidence::kConfirmed};
// [confirmed] Runic per-window command: pointer to the int the window's "onClick" command maps to
// (set by kGameUiBindCommands @0x823391A8; the default when there is none @0x823391E8). The menu
// reads *(*(window+0x134)) as the command of a click (@0x82352620..@0x82352630).
inline constexpr Field kWindowCommandSlot{0x134, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// Commands ("onClick" property values). kGameUiBindCommands upper-cases the property and searches
// a table of 102 std::string entries (0x1C bytes each) at kCommandNames (@0x8233916C..@0x823391BC).
// Each translation unit's static initializer fills it in this order (e.g. sub_829D1270). The
// command a menu receives is the entry's index: COptionsMenu::OnCommand switches on 0, 6, 14,
// 0x5E and 0x60, matching optionsmenu.uilayout's GUIEXITGAME, GUICLOSEMENU, GUISELECT1,
// GUISETTINGSMENU and GUILEADERBOARDMENU; CSettingsMenuXenon handles 8 and 9.
inline constexpr uint32_t kCommandNames = 0x832E9AB8;
inline constexpr uint32_t kCommandNameStride = 0x1C;
inline constexpr uint32_t kCommandCount = 102;
enum class Command : uint32_t {
  kExitGame = 0,
  kCloseMenu = 6,
  kBack = 7,
  kDecline = 8,
  kAccept = 9,
  kOk = 10,
  kSettingsMenu = 94,
  kNone = 101,
};

// ---------------------------------------------------------------------------------------------
// CDropdownMenu: base class of every menu (vtable 0x820CF0B4).
// [confirmed] Constructor: r3 = this, r4 = CGameUI*, r5..r8 = values of CGameUI members the
// derived ctors pass on (CSettingsMenuXenon: game_ui+0x58, +0x28, +0x340, +0x1148;
// @0x8233C93C..@0x8233C950). Stores r4 at +0x20, r5 at +0x1C, r6 at +0x24, r7 at +0x08 (kParent),
// r8 at +0x3C (@0x82351604..@0x82351654).
inline constexpr GuestFunction kDropdownMenuCtor{0x823515F8, Confidence::kConfirmed};
inline constexpr uint32_t kDropdownMenuVtable = 0x820CF0B4;
namespace dropdown_menu {
// [confirmed] Window the root is attached to on open (addChildWindow(parent, root) @0x8235216C).
inline constexpr Field kParent{0x08, Confidence::kConfirmed};
// [confirmed] Root window of the loaded layout (@0x823803BC).
inline constexpr Field kRoot{0x0C, Confidence::kConfirmed};
// [confirmed] Open flag (u8): SetOpen returns early when it already matches (@0x82352010,
// @0x823521C0, @0x823521CC) and sets it after opening (@0x823521AC); CGameUI::ToggleSettingsMenu
// passes !open (@0x8233D68C).
inline constexpr Field kOpen{0x18, Confidence::kConfirmed};
// [confirmed] CGameUI* (@0x82351620; menus call CGameUI methods on it, @0x82353CF4).
inline constexpr Field kGameUi{0x20, Confidence::kConfirmed};
// [confirmed] "Title" window (@0x823803E4); kSetTitle writes its text (@0x82351FC8).
inline constexpr Field kTitle{0x28, Confidence::kConfirmed};
// [confirmed] Embedded gamepad navigation handler (CDropdownMenuTabNavigationHandler, ctor
// sub_82348B60 @0x823516B4 with this+0x5C).
inline constexpr Field kNavigation{0x5C, Confidence::kConfirmed};
// [confirmed] Tab order dirty flag (u8, set by the ctor @0x82351684): on the next open the
// navigation handler collects tabOrder / tabOrderRow / tabOrderColumn / tabOrderGroupMatrix from
// the root again (sub_82352528, from SetOpen @0x82352178; collector sub_82347F18), then clears it.
// Windows added to the layout before the first open are therefore navigable.
inline constexpr Field kTabOrderDirty{0xF0, Confidence::kConfirmed};
// [confirmed] Slot 3, Update(dt in f1): CGameUI calls it on the settings menu (@0x8233E3A0).
inline constexpr VtableSlot kUpdate{3, Confidence::kConfirmed};
// [confirmed] Slot 7, SetOpen(bool): base 0x82351FF0. Opening attaches the root to kParent,
// rebuilds the tab order if dirty and shows the root (@0x82352164..@0x823521A8).
// CGameUI::ToggleSettingsMenu closes the pause menu and two others with SetOpen(false) before
// toggling settings (@0x8233D634..@0x8233D6A4).
inline constexpr VtableSlot kSetOpen{7, Confidence::kConfirmed};
// [confirmed] Slot 8, the click event subscriber's target (0x82352590): reads the window from the
// event args (+8), its command (kWindowCommandSlot) and name, and calls slot 9.
inline constexpr VtableSlot kOnClicked{8, Confidence::kConfirmed};
// [confirmed] Slot 9, OnCommand(command, String name by value): r4 = command, r5 = String* the
// callee destroys (base 0x823515D0 only destroys it and returns 1).
inline constexpr VtableSlot kOnCommand{9, Confidence::kConfirmed};
// [confirmed] Slot 11: returns the navigation handler the tab order is collected into
// (@0x8235254C; CSettingsMenuXenon returns this+0x148, 0x82380CA0).
inline constexpr VtableSlot kNavigationHandler{11, Confidence::kConfirmed};
}  // namespace dropdown_menu
inline constexpr GuestFunction kSetTitle{0x82351F78, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// Gamepad navigation (TabNavigationHandler and subclasses). Slot 6 receives the navigation event:
// +0x08 = focused window, +0x0C = action.
// [confirmed] Actions, from CSettingsXenonTabNavigationHandler slot 6 (0x8237FD30): 3 moves a
// slider down and 4 up (@0x8237FE1C, @0x8237FE74), 5 toggles a checkbox or presses a button
// (@0x8237FE58, @0x82380004).
namespace navigation {
inline constexpr VtableSlot kOnEvent{6, Confidence::kConfirmed};
inline constexpr Field kEventWindow{0x08, Confidence::kConfirmed};
inline constexpr Field kEventAction{0x0C, Confidence::kConfirmed};
inline constexpr uint32_t kActionLeft = 3;
inline constexpr uint32_t kActionRight = 4;
inline constexpr uint32_t kActionAccept = 5;
}  // namespace navigation
inline constexpr uint32_t kDropdownMenuNavigationVtable = 0x820CF0E8;
namespace navigation_handler {
// [confirmed] The menu the handler belongs to (CSettingsXenonTabNavigationHandler::OnEvent reads
// the settings menu's widget pointers through it, @0x8237FD5C..@0x8237FD78).
inline constexpr Field kMenu{0x28, Confidence::kConfirmed};
}  // namespace navigation_handler
// [confirmed] CSettingsXenonTabNavigationHandler::OnEvent (its vtable 0x820D1CD0 slot 6): r3 =
// handler, r4 = event; handles the settings menu's own sliders, checkboxes and buttons by pointer
// and returns 1 (@0x82380130). Windows it does not know get nothing, the host's included.
inline constexpr GuestFunction kSettingsNavigationOnEvent{0x8237FD30, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// CGameUI and the settings menu.
namespace game_ui_object {
// [confirmed] The pause menu (COptionsMenu, stored @0x8233C910) and the settings menu
// (CSettingsMenuXenon, stored @0x8233C95C).
inline constexpr Field kOptionsMenu{0x3A4, Confidence::kConfirmed};
inline constexpr Field kSettingsMenu{0x3A8, Confidence::kConfirmed};
}  // namespace game_ui_object
// [confirmed] CGameUI::ToggleSettingsMenu (GUISETTINGSMENU in COptionsMenu::OnCommand, @0x82353CF8).
inline constexpr GuestFunction kToggleSettingsMenu{0x8233D620, Confidence::kConfirmed};
// [confirmed] CSettingsMenuXenon (vtable 0x82001088): ctor (only caller is the CGameUI ctor,
// @0x8233C950) and init, which loads "media/ui/settingsmenu_xenon.uilayout" (@0x82380318) and
// looks up its widgets.
inline constexpr GuestFunction kSettingsMenuCtor{0x82380140, Confidence::kConfirmed};
inline constexpr GuestFunction kSettingsMenuInit{0x823802F8, Confidence::kConfirmed};
// [confirmed] CSettingsMenuXenon::ResetDefaults: r3 = menu; sets the sliders and checkboxes to the
// game's defaults right away; the navigation handler calls it on A over "Reset Defaults" (the
// widget at menu+0x144, @0x8237FE00..@0x82380014).
inline constexpr GuestFunction kSettingsMenuResetDefaults{0x82380F88, Confidence::kConfirmed};
// [confirmed] CSettingsMenuXenon::OnCommand (slot 9): only acts while open; 8 and 9 set flags and
// close the menu (@0x82380C48..@0x82380C7C).
inline constexpr GuestFunction kSettingsMenuOnCommand{0x82380C28, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// OGRE resources.
// [confirmed] ResourceGroupManager::addResourceLocation(name, locType, resGroup, recursive): logs
// "Added resource location '" (@0x8242AF48) and indexes the archive right away
// (OgreResourceGroupManager.cpp, 1.7: find("*") then addToIndex, @0x8242AEF4..@0x8242AF34).
// Called by the resources.cfg loader sub_8239B998 (@0x8239C574, @0x8239C5FC, @0x8239C6B0).
inline constexpr GuestFunction kAddResourceLocation{0x8242AE30, Confidence::kConfirmed};
inline constexpr GuestFunction kResourcesCfgLoader{0x8239B998, Confidence::kConfirmed};

// [confirmed] How the game's data loader (sub_8239D5F8) finds a file, in sub_8239D0E8: the mods'
// file maps first; then, when the data manager's +16 is set (it is), OGRE's resourceExists over the
// groups in its list (+76, count +80: "0ZIP0", "ZIP") and then "General" (the global 0x83424810,
// copied from OGRE's default group name 0x8349D568); then _stat64 in a folder of its own. It asks
// with the name in upper case: a name we passed as "57930ade12df44c0.RAW" reached sub_8239D0E8 as
// "57930ADE12DF44C0.RAW" (gdb, 2026-10-08), and a location on a host folder is matched by case on
// Linux, so that file was never found (not on Windows or macOS, whose file systems ignore case by
// default). Rule: a file we create for the game to find through a resource location is named
// exactly as the game asks for it: in upper case when it goes through this loader (the unit index,
// mods/unit_cache.h UnitIndexFileName), with the game's own spelling when the game asks by its own
// name through CEGUI or OGRE (our layout, the widened layouts, which keep the pak's names).

// ---------------------------------------------------------------------------------------------
// Calling into the guest from a hook (on the game's thread: CEGUI is not thread-safe).
//
// [confirmed] Guest heap: kAlloc(heap, size) returns the block (heap 0 = the default heap
// 0x83558F90, @0x821CD828; the menus are allocated with it, e.g. li r4,0x14C; li r3,0
// @0x8233C928); kFree(block) (@0x821CD9A8, what every string destructor calls).
inline constexpr GuestFunction kAlloc{0x821CD7F8, Confidence::kConfirmed};
inline constexpr GuestFunction kFree{0x821CD9A8, Confidence::kConfirmed};
// [confirmed] std::wstring (UTF-16, the type of the UI's paths): ctor from a wchar_t* (r3 = this,
// r4 = text; capacity 7 and a 16-bit terminator, @0x821AC4A8..@0x821AC4C0) and dtor (frees when
// capacity >= 8, @0x821B3998). Same 0x1C layout as stl_string in ogre_layout.h, in wchar_t units.
// kSettingsMenuInit builds its layout path with it (@0x8238031C).
inline constexpr GuestFunction kWStringFromText{0x821AC490, Confidence::kConfirmed};
inline constexpr GuestFunction kWStringDtor{0x821B3988, Confidence::kConfirmed};
// [confirmed] std::string::assign(const char*, length): the resources.cfg loader fills an empty
// string (buffer[0] = 0, length 0, capacity 15) with it (@0x8239C540..@0x8239C554). Freed inline
// when capacity >= 16 (@0x8239C514..@0x8239C52C).
inline constexpr GuestFunction kStringAssign{0x821BF438, Confidence::kConfirmed};
// [confirmed] CEGUI::String (0x98 bytes: UTF-32 with a 32-character inline buffer at +0x14 and the
// heap buffer at +0x94): ctor from UTF-8 text (r3 = this, r4 = text; reserve 0x20 @0x821EA050)
// and dtor (frees the heap buffers, @0x821AEDF0..@0x821AEE28). kSetTitle uses both
// (@0x82351FBC, @0x82351FD0).
inline constexpr GuestFunction kCeguiStringFromUtf8{0x821EA030, Confidence::kConfirmed};
inline constexpr GuestFunction kCeguiStringDtor{0x821AEDE0, Confidence::kConfirmed};
inline constexpr uint32_t kCeguiStringSize = 0x98;

// [confirmed] Window::setText(const String&): assigns the text (window+0x58) and fires
// onTextChanged (vtable +0x34) (@0x825AEAB4..@0x825AEAF8); kSetTitle calls it (@0x82351FC8).
inline constexpr GuestFunction kWindowSetText{0x825AEAA0, Confidence::kConfirmed};
// [confirmed] Window::setProperty(name, value) (both String*): GUILayout_xmlHandler::
// elementPropertyStart (sub_825DAB50) calls it for every <Property> (@0x825DACB4). Covers "Text",
// "Disabled", "Selected", "Visible" and the rest of the layout's properties.
inline constexpr GuestFunction kWindowSetProperty{0x825AB6F8, Confidence::kConfirmed};
// [confirmed] Window::getProperty(name) returning String by value (r3 = result, r4 = window,
// r5 = name; @0x82338F28 reads "onClick" with it).
inline constexpr GuestFunction kWindowGetProperty{0x825AB678, Confidence::kConfirmed};

// [confirmed] std::vector<T*>::push_back(const T*&): r3 = vector, r4 = pointer to the element
// (the CGameUI ctor adds every menu to its list with it, e.g. @0x8233C96C).
inline constexpr GuestFunction kPointerVectorPushBack{0x822FB020, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// Lifecycle.
// [confirmed] CGameUI (vtable 0x820CB398): ctor sub_823369F8, which builds every menu
// (kGameUiCreateMenus); created only by sub_82213960 when its owner's slot (+0x3C) is still
// empty (@0x82213974..@0x82213984), from the game's scene setup sub_82210148 and from
// sub_8221AD90. The dtor (sub_82337050, via the deleting dtor 0x82336FF8) deletes every menu in its
// list through their vtable slot 0 with flag 1 (@0x82337274..@0x823372C8). A menu added to the list
// therefore dies with CGameUI, like the game's own.
inline constexpr GuestFunction kGameUiCreateMenus{0x823392E8, Confidence::kConfirmed};
inline constexpr GuestFunction kGameUiDtor{0x82337050, Confidence::kConfirmed};
// [confirmed] CDropdownMenu deleting dtor (slot 0 of kDropdownMenuVtable; also COptionsMenu's).
inline constexpr GuestFunction kDropdownMenuDeletingDtor{0x82386008, Confidence::kConfirmed};
// [confirmed] CSettingsMenuXenon deleting dtor (slot 0 of its vtable; installs the vtable again
// @0x8238029C).
inline constexpr GuestFunction kSettingsMenuDeletingDtor{0x82380270, Confidence::kConfirmed};
namespace game_ui_object {
// [confirmed] The menus, in creation order (push_back @0x8233C8D8.. per menu). CGameUI hands
// every one of them the frame's input (vtable slot 2, sub_8233EFF8 @0x8233F0B8) and updates
// them (slot 1 then slot 3, sub_8233E338 @0x8233E3F0, @0x8233E40C); "is a menu open" scans the
// same list (sub_82191210, open flag @0x82191240).
inline constexpr Field kMenus{0x17A8, Confidence::kConfirmed};
}  // namespace game_ui_object
// [confirmed] The CGameUI instance pointer (read before calling its methods, e.g. @0x82380110 ->
// kCloseAllMenus; its +0x18A0 member is deleted by kGameUiDtor @0x823372FC).
inline constexpr uint32_t kGameUiGlobal = 0x835594F8;
// [confirmed] The save indicator, CGameSaveMenuXenon (RTTI .?AVCGameSaveMenuXenon@@, vtable
// 0x820D0690, slot 7 SetOpen = sub_82363E40): built by kGameUiCreateMenus (vtable @0x8233CD48)
// and kept in CGameUI (@0x8233CD58).
namespace game_ui_object {
inline constexpr Field kSaveMenu{0x3D4, Confidence::kConfirmed};
// [confirmed] When kShowSaveIndicator opened the save indicator (stored @0x82344F10).
inline constexpr Field kSaveIndicatorOpenedAt{0x18BC, Confidence::kConfirmed};
}  // namespace game_ui_object
namespace save_menu {
// [confirmed] The full autosave notice is still to be shown: 1 at creation (@0x8233CD40); while
// it is set, SetOpen(true) shows the notice window (+0x10C, @0x82363E6C..@0x82363E70) instead of
// the icon (+0x110, @0x82363EA4), and then clears it (@0x82363F64): once per session.
inline constexpr Field kFullNoticePending{0x114, Confidence::kConfirmed};
}  // namespace save_menu
// [confirmed] CGameUI::ShowSaveIndicator(CGameUI* r3, bool open r4), with a minimum display
// time. Opening stores the time (sub_821F8EE0 @0x82344F0C) in kSaveIndicatorOpenedAt; closing
// reads kFullNoticePending (@0x82344F28) before closing the menu through SetOpen (@0x82344F78),
// then calls kSleep (@0x82344F88) for what is missing to 5000 ms while the full notice is
// pending (@0x82344F34), else to 1000 ms when the indicator was up for at most 1000 ms
// (@0x82344F44) or to 3000 ms when for at most 3000 ms (@0x82344F54). The time is the console's
// certification minimum, not the save: the write is over before it closes.
inline constexpr GuestFunction kShowSaveIndicator{0x82344EE0, Confidence::kConfirmed};
// [confirmed] The return address of that kSleep call.
inline constexpr uint32_t kShowSaveIndicatorSleepReturn = 0x82344F8C;
// [confirmed] CGameUI::CloseAllMenus: SetOpen(false) on the pause, settings and two other menus
// (@0x8233D588..@0x8233D5E0). The settings menu's navigation calls it on action 8
// (@0x82380114).
inline constexpr GuestFunction kCloseAllMenus{0x8233D570, Confidence::kConfirmed};

// [confirmed] Navigation actions as the base TabNavigationHandler (sub_82348BB8, reached through
// CDropdownMenuTabNavigationHandler slot 6 0x82358510) turns them into CEGUI mouse buttons on
// the focused window (System::injectMouseButtonDown sub_825A7BE0 on the System singleton
// 0x835067CC): 5 left (A), 6 right, 8 middle, 7 X1 (@0x82348BDC..@0x82348C20). The settings menu
// handles 6 as accept-and-close (OnCommand(9) @0x823800A4) and 8 as that plus CloseAllMenus.
namespace navigation {
inline constexpr uint32_t kActionBack = 6;
inline constexpr uint32_t kActionCloseAll = 8;
}  // namespace navigation

// ---------------------------------------------------------------------------------------------
// [confirmed] Ogre::ResourceGroupManager singleton pointer (r3 of every addResourceLocation call
// in kResourcesCfgLoader, @0x8239C56C). OGRE 1.7's openResource searches every group when the
// file is not in the one asked for (OgreResourceGroupManager.h:746, default true), so a location
// in any group is found.
inline constexpr uint32_t kResourceGroupManagerGlobal = 0x83582A4C;

// ---------------------------------------------------------------------------------------------
// Translations.
// [confirmed] Resource config reader (resourceconfig.dat, looked for in SAVE:\ and absent, so
// every key keeps its default): registers "Translate File" with a default chosen by the console
// language (sub_8287D5B0): 3 -> "translations\de\translation.dat", 4 -> fr, 5 -> es, anything
// else "translations\translation.dat", which does not exist (@0x82323A3C..@0x82323AC0). The game
// loads that file (as "<path>.adm", Runic's compiled DAT) into its translator (sub_8231FF28,
// global 0x835594E8). Other languages are reached by changing that default.
inline constexpr GuestFunction kResourceConfigReader{0x823236E8, Confidence::kConfirmed};
// [confirmed] Config key registration: r3 = config, r4 = key (std::wstring*), r5 = default value
// (std::wstring*, copied, @0x823985F4); returns the key's index (@0x8239862C).
inline constexpr GuestFunction kConfigRegisterString{0x82398588, Confidence::kConfirmed};
inline constexpr const char16_t* kTranslateFileKey = u"Translate File";
// [confirmed] std::wstring -> CEGUI::String: r3 = the String to build, r4 = the wstring. Sets the
// String empty (reserve 0x20, @0x823458A4..@0x823458D0), grows it with kCeguiStringGrow, then copies
// each UTF-16 unit keeping its low 8 bits (lhzx + clrlwi 24, @0x82345950..@0x82345958): text past
// Latin-1 (Cyrillic, CJK) comes out as unrelated characters. Its only caller is the layout
// translation in kGameUiScaleLayout (@0x82338D74).
inline constexpr GuestFunction kWStringToCeguiString{0x82345898, Confidence::kConfirmed};
// [confirmed] CEGUI::String grow(n): room for n code points plus the terminator; past 32 it moves to
// a heap buffer at +0x94 (@0x821AF01C..@0x821AF0A8). Returns 1.
inline constexpr GuestFunction kCeguiStringGrow{0x821AF010, Confidence::kConfirmed};
namespace cegui_string {
// [confirmed] Fields (kWStringToCeguiString, kCeguiStringGrow, kCeguiStringDtor): code points (UTF-32)
// inline at +0x14 while the reserve is at most 0x20, else in the heap buffer at +0x94.
inline constexpr Field kLength{0x00, Confidence::kConfirmed};
inline constexpr Field kReserve{0x04, Confidence::kConfirmed};
inline constexpr Field kEncodedBufferLength{0x08, Confidence::kConfirmed};
inline constexpr Field kEncodedDataLength{0x0C, Confidence::kConfirmed};
inline constexpr Field kEncodedBuffer{0x10, Confidence::kConfirmed};
inline constexpr Field kInlineBuffer{0x14, Confidence::kConfirmed};
inline constexpr Field kHeapBuffer{0x94, Confidence::kConfirmed};
inline constexpr uint32_t kInlineCapacity = 0x20;
}  // namespace cegui_string

// The console languages whose translation the game picks by itself.
inline constexpr uint32_t kNativeTranslationLanguages[] = {3, 4, 5};

}  // namespace torchlight::guest_abi::game_ui
