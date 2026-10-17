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
// [confirmed] The menu's tab navigation state, embedded (zeroed by sub_82346A60, called from the
// ctor with this+0x5C, the call returning to 0x82351684). The pad's event handler is a separate
// 44-byte CDropdownMenuTabNavigationHandler, allocated by the ctor, built by sub_82348B60 with r4 =
// this+0x5C (@0x823516B0; it keeps it at +8, `stw r4,8(r3)`) and stored at kNavigationHandlerObject
// (@0x823516D0).
inline constexpr Field kNavigation{0x5C, Confidence::kConfirmed};
inline constexpr Field kNavigationHandlerObject{0xF4, Confidence::kConfirmed};
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

// [confirmed] The tab navigation state (dropdown_menu::kNavigation). The window the pad focus is on
// is at +44: its setter sub_82346D10 (r3 = state, r4 = window) drops the previous window's four
// event connections (+48..+79) and subscribes the new window's, then stores it (`stw r28,44(r31)`
// @0x82346F8C); the tab order collector calls it (sub_82347F18). The base handler's event slot
// (0x82348C38) clicks the event's window only when it is not this one (`lwz r9,44(r11)` on the
// state at handler +8, @0x82348C44..@0x82348C50).
namespace tab_navigation {
inline constexpr Field kFocusWindow{44, Confidence::kConfirmed};
}  // namespace tab_navigation
inline constexpr GuestFunction kTabNavigationSetFocus{0x82346D10, Confidence::kConfirmed};

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
// [confirmed] ResourceGroupManager::initialiseResourceGroup(name): logs "Initialising resource
// group " (@0x820DB170), throws "Cannot find a group named " when the group is missing, then parses
// the group's scripts (sub_8242B730, which logs "Parsing scripts for resource group " @0x820DB6AC)
// and creates its declared resources (sub_8242BED0), as in OgreResourceGroupManager.cpp 1.7,
// lines 102-126. The resources.cfg loader calls initialiseAllResourceGroups (sub_82429D78)
// itself, so a group made after it stays uninitialised until this is called.
inline constexpr GuestFunction kInitialiseResourceGroup{0x82429C98, Confidence::kConfirmed};
// [confirmed] ResourceGroupManager::initialiseAllResourceGroups: parses every uninitialised group's
// scripts (calls sub_8242B730). Called by the resources.cfg loader (@0x8239C8C8) after its
// locations are added (the last @0x8239C6B0) and the data manager's group list (data_manager
// below) is filled (@0x8239C390, @0x8239C48C); otherwise only from two ResourceBackgroundQueue
// methods nothing calls directly.
inline constexpr GuestFunction kInitialiseAllResourceGroups{0x82429D78, Confidence::kConfirmed};

// [confirmed] The data manager's OGRE groups, the ones resources.cfg's archives are in ("0ZIP0",
// "ZIP"): a vector of std::string (stride 0x1C) at +76, count +80, capacity +84, as the data loader
// walks it in sub_8239D0E8 (addi r30,r27,76; lwz 8(r30) bound; addi r28,r28,28).
namespace data_manager {
inline constexpr Field kGroupsData{76, Confidence::kConfirmed};
inline constexpr Field kGroupsCount{80, Confidence::kConfirmed};
inline constexpr uint32_t kGroupStride = 0x1C;
}  // namespace data_manager

// [confirmed] The game's ScriptCompilerListener::handleEvent (Runic's): this r3, compiler r4,
// event r5, retval r6; returns whether it handled the event. Two events, compared by their type
// string (event +4, OgreScriptCompiler.cpp 1.7 lines 1658-1666):
// - "createMaterial" (@0x820D2C3C): the material's name is the script's folder (event
//   kMaterialFile, upper case) with anything before "MEDIA" dropped (@0x8239B5E0), plus the
//   material's name (kMaterialName, @0x8239B644). When a material of that name exists
//   (MaterialManager slot 22, @0x8239B664) it RETURNS THAT ONE, and the script is compiled over it;
//   only otherwise does it call create (kResourceManagerCreate, @0x8239B6C4). PC's listener
//   (Torchlight.exe 0x5C31E0) always calls create, which OGRE leaves unregistered when the name
//   exists (addImpl, no loading listener), so on PC the first definition is kept. Scripts whose
//   compiler +52 starts with 'D' or 'd' are left alone (@0x8239B4FC, @0x8239B514).
// - "processResourceName" (@0x820D2C4C), for a texture (kResourceKind 0): sub_8239A7F8 rebuilds
//   kResourceName from the script's full path, so a script on one of our devices names its textures
//   "TLMOD000:/MEDIA/..." where PC's names start at "MEDIA/".
inline constexpr GuestFunction kScriptListenerHandleEvent{0x8239B490, Confidence::kConfirmed};
namespace script_event {
inline constexpr Field kType{4, Confidence::kConfirmed};             // std::string
inline constexpr Field kMaterialFile{32, Confidence::kInferred};     // std::string, createMaterial
inline constexpr Field kMaterialName{60, Confidence::kConfirmed};    // std::string, createMaterial
inline constexpr Field kMaterialGroup{88, Confidence::kConfirmed};   // std::string, createMaterial
inline constexpr Field kResourceKind{32, Confidence::kConfirmed};    // processResourceName, 0 = texture
inline constexpr Field kResourceName{36, Confidence::kConfirmed};    // std::string, processResourceName
inline constexpr uint32_t kTextureKind = 0;
}  // namespace script_event
// [confirmed] Ogre::MaterialManager singleton pointer (lis r30,-31920; lwz r4,26516(r30) in the
// listener, @0x8239B65C).
inline constexpr uint32_t kMaterialManagerGlobal = 0x83506794;
// [confirmed] ResourceManager::create (sub_824C8FC8), MaterialManager vtable 0x82002DEC slot 4:
// r3 the returned SharedPtr (16 bytes, the pointer at +4), r4 this, then name, group, isManual,
// loader, params. Every call adds one to mNextHandle, a 64-bit value at +112 (ld @0x824C8FE8,
// std @0x824C8FF4), before createImpl; addImpl drops a resource whose name exists, and
// _notifyResourceCreated keeps it alive in its group's list (OgreResourceGroupManager.cpp 1.7,
// lines 1124-1139).
inline constexpr VtableSlot kResourceManagerCreate{4, Confidence::kConfirmed};
inline constexpr Field kResourceManagerNextHandle{112, Confidence::kConfirmed};
// [confirmed] SharedPtr<Material>'s destructor, as the listener releases its own (@0x8239B684,
// @0x8239B6DC).
inline constexpr GuestFunction kMaterialPtrRelease{0x8220D490, Confidence::kConfirmed};

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
// [confirmed] The list's end: "is a menu open" walks begin (+6056) to end (+6060), 4 bytes a menu
// (sub_82191210 @0x82191210..@0x82191220), testing each one's open flag (dropdown_menu::kOpen).
inline constexpr Field kMenusEnd{0x17AC, Confidence::kConfirmed};
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
// CEGUI window lookup and visibility, as the skill tree uses them.
// [confirmed] Window::getChildRecursive(name): r3 = window, r4 = String*; searches the children
// (vector at +0x2C..+0x30) by name (+0x348) and recurses into each (@0x825AE360..); null when
// absent (the skill tree's cell lookup checks it, @0x8235E994).
inline constexpr GuestFunction kFindDescendant{0x825AE360, Confidence::kConfirmed};
// [confirmed] Window::setVisible(bool): r3 = window, r4 = state; stores the flag at +0x161
// (@0x821A56E0) and fires the shown or hidden event. Window::isVisible(local_only) reads the same
// byte (@0x821AFE64) and, unless local_only, the parents' too.
inline constexpr GuestFunction kWindowSetVisible{0x821A56C0, Confidence::kConfirmed};
inline constexpr Field kWindowVisible{0x161, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// What the development UI dump reads of a window (dev/ui_probe in script_input.cpp).
// [confirmed] Window type (CEGUI String, "ceguiWidget/StaticImage" and the like): the ctor
// sub_825AD818 copies its type argument (r4, kept in r27) to +688 (the String copy returning to
// 0x825AD92C), then its name argument to +840 (kWindowName).
inline constexpr Field kWindowType{688, Confidence::kConfirmed};
// [confirmed] Window text (CEGUI String): kWindowSetText assigns it (@0x825AEAB4).
inline constexpr Field kWindowText{0x58, Confidence::kConfirmed};
// [confirmed] Window::getUnclippedPixelRect (r3 = the Rect to fill, r4 = window): the window's
// screen rectangle in pixels, cached at +628..+643 (valid flag +644), computed through vtable slot
// 2 when the cache is stale (the virtual call returning to 0x821FD7F4). getPixelRect (sub_821AFB68), the same
// clipped by the parents, caches at +648 (flag +664); Window::isHit (vtable 0x8210119C slot 3,
// 0x821AFCA8) tests a point against it.
inline constexpr GuestFunction kWindowUnclippedPixelRect{0x821FD7B8, Confidence::kConfirmed};
inline constexpr GuestFunction kWindowPixelRect{0x821AFB68, Confidence::kConfirmed};
// [confirmed] CEGUI Rect (16 bytes, floats): isHit tests x against +8 and +12 and y against +0
// and +4 (after getPixelRect, the call returning to 0x821AFCE0): top, bottom, left, right.
namespace cegui_rect {
inline constexpr Field kTop{0, Confidence::kConfirmed};
inline constexpr Field kBottom{4, Confidence::kConfirmed};
inline constexpr Field kLeft{8, Confidence::kConfirmed};
inline constexpr Field kRight{12, Confidence::kConfirmed};
inline constexpr uint32_t kSize = 16;
}  // namespace cegui_rect
// [confirmed] Whether a window has a property: kGameUiBindCommands looks "onClick" up in the
// window's property map (std::map<String, Property*> at window +4, the find sub_825C9F10 with r3 =
// the iterator to fill, r4 = window + 4, r5 = String*, @0x82338EF8) and compares the iterator with
// the map's end node (`lwz r11,8(r27)`, @0x82338EFC..@0x82338F04) before kWindowGetProperty, which
// throws for a property the window lacks.
inline constexpr Field kWindowPropertyMap{4, Confidence::kConfirmed};
inline constexpr Field kWindowPropertyMapEnd{8, Confidence::kConfirmed};
inline constexpr GuestFunction kPropertyMapFind{0x825C9F10, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// CNewGameMenu: the character creation screen (vtable 0x820D2658; its layout
// media/ui/charactercreate.uilayout, loaded @0x8238D6F0). Its three class buttons are ImageButtons
// named after their class ("Destroyer", "Alchemist", "Vanquisher") with onClick GUISELECT1; the pad
// moves over a fixed 5 x 3 grid of window names (table 0x82421E28, CEGUI Strings of 0x98 bytes,
// filled by sub_82EB3258): the class buttons are its first row, in that order.
inline constexpr uint32_t kNewGameMenuVtable = 0x820D2658;
// [confirmed] The layout's file name (L"media/ui/charactercreate.uilayout" @0x820D24B8, loaded by the
// menu's init sub_8238D6F0). Other menus' layouts use GUISELECT1 too (optionsmenu,
// characterload, difficulty), for other things.
inline constexpr const char* kNewGameMenuLayout = "charactercreate.uilayout";
inline constexpr uint32_t kNewGameClassRow = 0;
// [confirmed] The pets' buttons ("Dog", "Cat", "Chakawary", onClick GUISELECT2) are the grid's third
// row (the table's entries 6-8, sub_82EB3258).
inline constexpr uint32_t kNewGamePetRow = 2;
inline constexpr uint32_t kNewGameClassColumns = 3;
// [confirmed] Slot 8 (0x820D2674), on show: puts the pad focus on the grid's first cell
// (@0x8238DDB0) and selects the class "Destroyer" (@0x8238DDC0..@0x8238DDD4).
inline constexpr GuestFunction kNewGameMenuOnShow{0x8238DCA8, Confidence::kConfirmed};
// [confirmed] Slot 9 (0x820D267C), OnCommand: r3 = menu, r4 = command, r5 = the window's name.
// Command 14 (GUISELECT1, @0x8238E834) selects the class of that name (@0x8238EA08 into
// kSelectClass); 15 selects the pet.
inline constexpr GuestFunction kNewGameMenuOnCommand{0x8238E7F0, Confidence::kConfirmed};
inline constexpr uint32_t kCommandSelectClass = 14;
inline constexpr uint32_t kCommandSelectPet = 15;
// [confirmed] Slot 12 (0x820D2688), the pad: A sends the focused grid cell's command and name to
// OnCommand (@0x8238F408); left/right/up/down move the focus by (r4, r5) = (-1/+1, 0) or
// (0, -1/+1) (@0x8238F48C into kNewGameMenuMoveFocus).
// [confirmed] Move the focus: r3 = menu, r4 = columns, r5 = rows; both wrap (columns 0-2, rows
// 0-4, @0x8238F510..@0x8238F570), a hidden cell is passed over, then the cell is focused
// (@0x8238F5B8).
inline constexpr GuestFunction kNewGameMenuMoveFocus{0x8238F4F8, Confidence::kConfirmed};
namespace new_game_menu {
// [confirmed] The focused grid cell (s32 each; @0x8238F568, @0x8238F570).
inline constexpr Field kFocusColumn{0x20C, Confidence::kConfirmed};
inline constexpr Field kFocusRow{0x210, Confidence::kConfirmed};
}  // namespace new_game_menu
// [confirmed] Select a class: r3 = the player choice (game_ui + 0x1790, @0x8238E9F8), r4 = the
// class name (std::wstring*, @0x8221400C..@0x82214014). Unless it is the class already chosen
// (@0x8221403C), the name is copied in (+0x174) and the player unit made by that name
// (kMakeUnit @0x822140C8, wardrobe.h).
inline constexpr GuestFunction kSelectClass{0x82214000, Confidence::kConfirmed};
// [confirmed] Select a pet, as kSelectClass: r3 = the player choice, r4 = the pet's name
// (std::wstring*, @0x82214194); unless it is the pet already chosen (@0x822141C4), the name is
// copied in (+0x190, @0x822141A4) and the pet made by that name (sub_823DEB40), kept only when made.
// OnCommand(15) calls it with the button's name (@0x8238EA70); the screen's show with "Dog"
// (@0x8238DDF8).
inline constexpr GuestFunction kSelectPet{0x82214188, Confidence::kConfirmed};

// ---------------------------------------------------------------------------------------------
// CCharSkillsMenuXenon: the skill tree page of the character screen (RTTI
// .?AVCCharSkillsMenuXenon@@, complete object locator 0x8211CC68 before its vtable 0x820D0164).
// Its layout, skillsmenu_xenon.uilayout, has the tree's cells "skillbg<pane>_<row>_<column>"
// (3 panes, 6 rows, 2 columns) with the children "skillicon..." and "skilllvl...", and the four
// spell cells "spellbg<n>".
inline constexpr uint32_t kCharSkillsMenuVtable = 0x820D0164;
// [confirmed] Slot 6 (0x820D017C): r3 = menu, r4 = character; stores the character (@0x8235BD50)
// and hides every tree cell, panes 0-2, rows 0-5, columns 0-1 (@0x8235BE58..@0x8235BE78).
inline constexpr GuestFunction kSkillsMenuSetCharacter{0x8235BD40, Confidence::kConfirmed};
// [confirmed] Slot 8 (0x820D0184): builds the tree from the character's skills. For each skill it
// writes the skill's id into table slot min(12 * pane + 2 * row + column, 35) (@0x8235C210,
// @0x8235C244) before looking its cell up by name; a found cell is shown, gets its icon and level,
// and its +0x134 pointed at the slot (@0x8235C350), and the focused cell's skill is described
// (sub_8235D558). A skill in a third column therefore takes the next row's first slot. The
// navigation handlers call it after every move (vtable +0x20, @0x8235E464, @0x8235E550).
inline constexpr GuestFunction kSkillsMenuBuildTree{0x8235BE88, Confidence::kConfirmed};
inline constexpr VtableSlot kSkillsMenuBuildTreeSlot{8, Confidence::kConfirmed};
// [confirmed] Focus one cell right / left (r3 = menu): the column +1 / -1 within 0-1, wrapping into
// the next / previous pane of 0-2 (@0x8235E3F4..@0x8235E410, @0x8235E4DC..@0x8235E504); on the
// spell row the spell index wraps within 0-3. A cell that exists but is hidden is passed by calling
// itself again (@0x8235E444); then the tree is rebuilt.
inline constexpr GuestFunction kSkillsMenuFocusRight{0x8235E3A0, Confidence::kConfirmed};
inline constexpr GuestFunction kSkillsMenuFocusLeft{0x8235E488, Confidence::kConfirmed};
// [confirmed] The focused skill (r3 = menu; returns the skill or null): looks the table slot of the
// focused cell (or spell 36-39) up in the character's skills (@0x8235E8C8..@0x8235E8DC). The
// description, learn and assign code reach the focused skill only through it (sub_82381D18,
// sub_82382720, sub_823827F8).
inline constexpr GuestFunction kSkillsMenuFocusedSkill{0x8235E830, Confidence::kConfirmed};
// [confirmed] Skill lookup by id: r3 = the character's skill list, r4 = id (64-bit); returns the
// skill or null (@0x8235E8DC).
inline constexpr GuestFunction kSkillListFind{0x821F58F8, Confidence::kConfirmed};
// [confirmed] Slot 13 (vtable +0x34): the root window the cells are looked up in (@0x8235E958,
// @0x8235E990 into kFindDescendant).
inline constexpr VtableSlot kSkillsMenuRootSlot{13, Confidence::kConfirmed};
namespace skills_menu {
// [confirmed] The character (kSkillsMenuSetCharacter @0x8235BD50; its skill list at +0x184,
// @0x8235C124, @0x8235E8D4).
inline constexpr Field kCharacter{0xB0, Confidence::kConfirmed};
inline constexpr Field kCharacterSkills{0x184, Confidence::kConfirmed};
// [confirmed] u8: the focus is on the spell row (the left/right handlers branch on it, @0x8235E3A0).
inline constexpr Field kSpellRow{0xB5, Confidence::kConfirmed};
// [confirmed] The skill ids of the tree's 36 cells then the 4 spells, 8 bytes each (the builder
// stores at (slot + 38) * 8, @0x8235C244; the getter reads the same, @0x8235E8D8).
inline constexpr Field kTable{0x130, Confidence::kConfirmed};
inline constexpr uint32_t kTableSlots = 40;
// [confirmed] The focused cell, s16 each: pane, row, column (sub_8235E8F0 builds the cell's name
// from them, @0x8235E914..@0x8235E920; the menu's reset zeroes them, @0x8235ADE4).
inline constexpr Field kFocusPane{0x650, Confidence::kConfirmed};
inline constexpr Field kFocusRow{0x652, Confidence::kConfirmed};
inline constexpr Field kFocusColumn{0x654, Confidence::kConfirmed};
}  // namespace skills_menu
namespace skill_list {
// [confirmed] The builder walks entries 0..count-1 (@0x8235C130); an entry is read from
// data + 4 * index while the index is below the bound, else from data (@0x8235C1BC..@0x8235C1D0).
inline constexpr Field kData{0x2C, Confidence::kConfirmed};
inline constexpr Field kCount{0x30, Confidence::kConfirmed};
inline constexpr Field kBound{0x34, Confidence::kConfirmed};
}  // namespace skill_list
namespace skill {
// [confirmed] The skill's place in the tree, 32-bit fields used as s16 (extsh, @0x8235C1DC..
// @0x8235C1E8), and its id (64-bit, @0x8235C230).
inline constexpr Field kColumn{0xD4, Confidence::kConfirmed};
inline constexpr Field kRow{0xD8, Confidence::kConfirmed};
inline constexpr Field kPane{0xDC, Confidence::kConfirmed};
inline constexpr Field kId{0x180, Confidence::kConfirmed};
}  // namespace skill

// ---------------------------------------------------------------------------------------------
// [confirmed] Ogre::ResourceGroupManager singleton pointer (r3 of every addResourceLocation call
// in kResourcesCfgLoader, @0x8239C56C). OGRE 1.7's openResource searches every group when the
// file is not in the one asked for (OgreResourceGroupManager.h:746, default true), so a location
// in any group is found.
inline constexpr uint32_t kResourceGroupManagerGlobal = 0x83582A4C;

// ---------------------------------------------------------------------------------------------
// CEGUI imagesets (a mod class's own icon at character creation, docs/mods.md 7l).
// [confirmed] CEGUI::ImagesetManager singleton pointer: sub_823392E8 (the call returning to
// 0x8233A5DC) and sub_825E7A08 load it from here (lis -31912, lwz +10984) as r3 of
// kImagesetManagerCreate and kImagesetManagerFind.
inline constexpr uint32_t kImagesetManagerGlobal = 0x83582AE8;
// [confirmed] ImagesetManager::createImageset(filename, resourceGroup): r3 = manager, r4 and r5 =
// String*; logs "Attempting to create an Imageset from the information specified in file '"
// (0x82101940, the log call returning to 0x825BA368), constructs the Imageset from the file
// (sub_825B8A70, which parses the XML and loads its image file through the resource group), looks
// its name up in the map (sub_825C9F10) and, when absent, inserts it (sub_825C9E20, @0x825BA428);
// returns the Imageset*. A name already present deletes the new one (sub_825BA448) and goes to
// sub_8287D560 (@0x825BA41C), CEGUI 0.6's AlreadyExistsException; a file or an image file that
// cannot be read throws inside the constructor. Callers make sure of all three first.
inline constexpr GuestFunction kImagesetManagerCreate{0x825BA338, Confidence::kConfirmed};
// [confirmed] The imageset of a name, or null (a Runic addition; CEGUI 0.6's getImageset throws):
// r3 = manager, r4 = String*; map find (sub_825C9F10, the call returning to 0x825BA73C), the value
// at +164 when found, 0 otherwise (loc_825BA754).
inline constexpr GuestFunction kImagesetManagerFind{0x825BA718, Confidence::kConfirmed};

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
