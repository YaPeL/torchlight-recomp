# PC's interface: what the mods change, and PC's UI in the recomp (study, 2026-10-15)

Reading only: the mods we have (Enhanced Edition with its two small mods, the 29 mods of the JCC
pack; TNNR has no UI files), PC's `Pak.zip` and `Torchlight.exe`, the Xbox pak and the decrypted
Xbox image (`tools/xex/xex_dump.py`). PC was not run. Nothing of the game is kept here: window,
class and command names are identifiers, quoted as evidence. Marks as in `docs/mods.md`:
**[read]** from the files, **[inferred]** from them, **[to verify]** needs a run.

## 1. What the mods change in the interface

### 1a. The files

287 files under `media/UI/` in 31 mods. A throwaway script compared each `.layout` with PC's own
window by window (by name: added, removed, and per property: position and size, text, images,
`onClick`, window type) and every other file by path with PC's pak and the Xbox pak. **[read]**

| Kind | Files | Mods | Same path as a game file |
|------|------:|------|--------------------------|
| Item and skill icon imagesets (`itemicons/`, new names) with their images | 110 + 124 | almost all | no |
| Icon imagesets that replace the game's (`itemicons13/14` EE, `itemicons16` JCC - Main) | 3 + 3 | 2 | PC and Xbox |
| Class portraits (`logo.imageset`, EE's `*logo.imageset`) | 4 + 5 | EE, JCC - Main, class mods | `logo.*` on PC only |
| Layouts (`.layout`) | 12 | EE (9), JCC - Main (1), JCC - Diablo 3 HUD (2) | PC only |
| Skin textures of PC's menus (`ui1/ui2.dds`, `models/*/*_top/_bottom.dds`, `dropdown.dds`) | 21 | EE (19), D3 HUD (2) | PC only |
| Loading and intro pictures (`loading.dds`, `vanquisherintro.dds`, `logo.dds`) | 4 | EE, JCC - Main | PC and Xbox (`loading`, `vanquisherintro`) |
| Fonts (`Serif.font`, `FrizQuadrata.font`, `FrizQuadrataBig.font`) | 3 | EE | PC and Xbox |
| Skin scheme and HUD imageset (`GuiLookSkin.scheme`, `hud.imageset`) | 2 | D3 HUD | PC only |
| `Thumbs.db` (Windows thumbnails, junk) | 5 | JCC | no |

No mod adds a new screen or a new kind of window, and no mod's layout uses a command PC's own
layouts do not use: every change is to look, text, images, or removing windows. **[read]**

### 1b. By screen

| Screen (PC file) | Mod | Change | Kind | To see it in the recomp |
|------------------|-----|--------|------|-------------------------|
| Character creation (`charactercreate.layout`) | EE | 19 class and pet buttons added, 9 images, 22 position/size | functional (game's handlers) + icons | **Done**: the selector (classes, pets, own icons; `docs/mods.md` 7l) |
| | JCC - Main | 84 buttons added (28 classes, 55 pets), scroll bar | functional + icons | **Done**, same |
| Character load (`characterload.layout`) | EE | 7 position/size | aesthetic | PC's screen (section 2) |
| Main menu frame (`mainmenuframe.layout`) | EE | 4 position/size | aesthetic | PC's screen |
| Loading (`loading.layout`, `loading.dds`) | EE | 4 position/size, its own picture | aesthetic + image | PC's screen; the picture: the replacements folder (`docs/mods.md` 7k) would show it, as a mod it loses to the game's file, as on PC |
| Cinematics (`cinematicmenu.layout`, `vanquisherintro.dds`) | EE | 4 position/size, intro picture | aesthetic + image | as loading |
| HUD (`bottomhud.layout`) | EE | the eight menu buttons removed (inventory, stats, skills, journal, pet, quests, perks, options) | aesthetic (cleaner bar; the keys still work) | PC's HUD; the Xbox HUD has no such buttons anyway |
| | JCC - Diablo 3 HUD | the health globe's three windows replaced by two big images and a "junk" image, 22 images, 69 position/size, the journal button's command removed, `hud.imageset` and the skin scheme replaced | aesthetic + images (a reskin) | PC's HUD with the mod's scheme and imageset |
| Pet HUD (`pethud.layout`) | EE | feed button removed, 8 position/size | aesthetic | PC's pet HUD |
| Inventory (`inventorymenu.layout`) | EE | 12 position/size | aesthetic | PC's inventory |
| | D3 HUD | 1 position/size; `inventory_top/bottom.dds` | aesthetic + skin | PC's inventory |
| Enchanting (`enchantmenu.layout`) | EE | 3 position/size | aesthetic | PC's enchant menu |
| Every PC menu (skin) | EE | `ui1/ui2.dds`, the 3D frames' textures (pet, journal, merchant, quest, character, skill, inventory, dropdown) | skin | PC's menus |
| Fonts | EE | the same three fonts at a smaller size (9 and 11 instead of 11 and 15) | aesthetic | probably nothing, on PC either: the game loads its fonts first (below) |
| Item and skill icons | almost all | new imagesets | icons | **Works today**: the game loads the mods' `itemicons/` imagesets itself (the selector's log: "already loaded by the game") **[read: run logs]** |
| | EE, JCC - Main | `itemicons13/14/16` replaced | icons | **[to verify]**, see below |

**The replaced icon atlases are a real risk.** The Xbox build's `itemicons13`, `itemicons14` and
`itemicons16` are not PC's: 40, 61 and 38 images against PC's 48, 64 and 8, and of the images both
have, 22, 35 and 8 sit elsewhere in the texture. EE's and JCC's files are PC's plus their own
(26, 29 and 16 images the Xbox file does not have). **[read]** So whichever file the game takes, one
side shows wrong icons: the Xbox file, and the mods' new items that use those atlases show nothing
or throw; the mod's file, and some of the Xbox game's own items show another item's picture. The
game reads these through its own file layer (`GAME:\MEDIA\UI\ITEMICONS\ITEMICONS14.IMAGESET` in
the run logs), which also searches the mods (`kFindModFile`, `guest_abi/mods.h`), so which one wins
is not known from reading. **[to verify]** with an automatic run: EE, an inventory holding one of
the Xbox game's items drawn from `itemicons14` and one of EE's, a capture of each icon. If the mod
wins, a fix is generic: merge at load (the Xbox file's images plus the mod's that the Xbox file
lacks, the mod's texture for those), done on the host before the guest reads it.

Two points of the native renderer's side (render agent, 2026-10-15). **[read: the code]**
- **The XML and the texture are chosen in two places.** The guest picks the `.imageset` file
  through its file layer (above); the host picks the atlas's image file by the texture's name and
  resource group, following OGRE's rule (`mods/texture_files.h`: the replacements, then the game's
  groups before the mods'). A mod's XML laid over the Xbox atlas, or the reverse, draws wrong
  icons even if each choice alone looks right. The check has to establish both: the imageset file
  the game opened (its log) and the texture file the capture holds for that atlas (a capture's
  `TXFL` chunk records a mod's or the replacements' file; no entry means the pak's).
- **A merge needs a new atlas.** An imageset has one image file, so "the Xbox images plus the
  mod's" is a recomposed texture: decode both atlases, place the mod's extra images in free space
  or a larger texture, write the combined image and an imageset with the new coordinates, and
  serve both from one place to the guest and the host (as the generated layouts are, so the host
  resolver finds the same file). The game's item definitions name the imageset and the image, so
  those names must stay. That is 2-3 days, not a few hours, if the check shows it is needed.

**Fonts and pictures at a game path.** PC's rule (`docs/mods.md` 7j): textures are global by name
and the first loaded wins, and the game's groups come before the mods'. The game loads its fonts and
its loading pictures from its own groups at start, so the mods' copies are most likely unused on PC
too. **[inferred]** The replacements folder (7k) is the way to apply them on purpose.

**What the survey means for the recomp:** the gameplay part of the mods' UI (classes, pets,
icons) is covered or nearly so; everything else is the look of PC's own screens, and seeing it
needs PC's screens themselves (section 2). No mod asks for anything a 360 screen could show.

## 2. PC's interface inside the recomp: feasibility

The idea: with the files of the user's own PC copy (never shipped), show PC's screens in the recomp,
with the mouse as well as the keyboard and the pad.

### 2a. What the guest already has

- **CEGUI is complete.** The Xbox image has every widget class and Falagard renderer PC's layouts
  use: PushButton, RadioButton, Checkbox, Slider, Thumb, Scrollbar, Combobox, ComboDropList,
  Editbox, MultiLineEditbox, ProgressBar, TabControl, ScrollablePane, Tooltip, Spinner, PopupMenu,
  ListHeaderSegment, the Falagard renderers for them, `FreeTypeFont` and `MouseCursor` (RTTI in the
  image). **[read]**
- **PC's skin is pure data.** `GuiLookSkin.scheme` maps every `GuiLook/*` type, the custom-looking
  ones too (`CharacterSlot`, `ItemTooltip`, `WeaponSwitch`, `FishingButton`, `PetFeedButton`), onto
  standard CEGUI classes with a look in `GuiLook.looknfeel`; no Runic window class is involved.
  The scheme names its imagesets (`hud`, `loading`, `logo`, `statsmenu_ui`, `ui2`, `uiicons`) and
  the same seven fonts the Xbox scheme names (same TTF files). **[read]**
- **Same formats.** PC layouts are CEGUI XML (`GUILayout`) like the Xbox `.uilayout`; imagesets are
  authored at 1024x768 with `AutoScaled` on both; the Xbox UI textures are ordinary little-endian
  DDS (DXT3), as PC's. **[read]**
- **Every command PC's layouts use is in the Xbox command table.** PC's 35 layouts use 48 `onClick`
  commands, 35 of which no Xbox layout uses (`GUITOGGLEINVENTORY`, `GUIFEEDPET`, `GUIPETSELL`,
  `GUISKILLUP`, `GUISCROLLUP`, ...); all 48 are strings in the Xbox image, the 102-entry table
  `kCommandNames` reads (`guest_abi/game_ui.h`). **[read]** Whether each Xbox menu still acts on
  them is per menu (below).
- **The scheme is loaded by a known call.** The game loads its scheme in `sub_823392E8`: the path
  `media/ui/hud/schemes/ceguiWidgets.scheme` (a big-endian wide string at `0x820CA950`) through
  `kUiResolveLayout` (`0x8239D5F8`), then `sub_825C9500(0x83582AF0's object, file, group)`, which
  by its arguments is `SchemeManager::loadScheme` (the global sits next to the imageset manager's,
  `0x83582AE8`). **[read; the role of `sub_825C9500` inferred]** So the host can load PC's scheme
  with one more call after the game's, from files served like the generated layouts.

### 2b. What is missing: the menus' code

Each screen is a Runic C++ class that loads its layout and then finds its windows by name. PC and
Xbox share some classes and not others (RTTI of both executables): **[read]**

- **Shared (17):** `CMainMenu`, `CNewGameMenu`, `CContinueGameMenu`, `CDifficultyMenu`,
  `COptionsMenu`, `CDieMenu`, `CCinematicMenu`, `CWaypointMenu`, `CModalMenu`, `CTipMenu`,
  `CQuestDialogMenu`, `CFishingMenu`, `CInteractiveMenu`, `CDropdownMenu`, `CSubMenu`,
  `CMenuManager`, `CGameUI`.
- **PC only (12), replaced on Xbox by `...Xenon` classes:** inventory, skills, stats, journal, pet,
  merchant, stash, enchant, quests, settings; PC's `CCombineMenu` and `CDialogMenu` have no Xbox
  class of that name.

How many of the windows an Xbox screen's code looks for are in PC's layout of the same screen (the
Xbox layout's named windows, a proxy for what its code finds): **[read]**

| Screen | Xbox windows in PC's layout | Verdict |
|--------|-----------------------------|---------|
| Pet HUD (`pethud`) | 6 of 6 | PC's layout could replace the Xbox one as is |
| Waypoints | 21 of 22 (only the frame `FrameB`) | close |
| Character load | 24 of 26 | close |
| Modal box, tip | 5 of 6, 2 of 3 (`FrameB`) | close |
| Death screen | 7 of 10 | some work |
| Quest dialog | 18 of 28 (reward sockets) | some work |
| Character creation | 9 of 18 | the selector already covers the mods' part |
| Settings, options, difficulty | 8 of 16, 1 of 6, 1 of 6 | different screens |
| Inventory, stats, quests, journal, pet menu | 5 of 108, 0 of 29, 1 of 48, 0 of 1, 3 of 15 | different classes: PC's layout cannot drive the Xenon code |
| HUD (`bottomhud`), merchant, stash, enchant, skills, perks, combine | no Xbox layout of that name | Xbox's are other screens (`hud`, `containerframe_xenon`, ...) |

So PC's screens split in two: the small shared ones, where PC's layout can stand in for the Xbox
one with a few added windows, and the big gameplay screens (HUD, inventory, skills, stats, pet,
merchant, stash, enchant, journal, quests), whose PC code does not exist on Xbox. For those the
choice is to write the behaviour on the host (hit tests, drag and drop, the item grid, tooltips,
skill points), driving the game's own data and commands, or to keep the Xbox screens.

### 2c. Mouse, keyboard and pad

- **PC:** no gamepad at all (no XInput, `OIS.dll` ships but `Torchlight.exe` does not import it);
  input is Win32 (`GetAsyncKeyState`, `GetCursorPos`, `SetCursorPos`, `ShowCursor`) fed to CEGUI
  through `injectMousePosition`, `injectMouseMove`, `injectMouseButtonDown/Up`, `injectKeyDown/Up`,
  `injectChar` and `injectTimePulse` (imports of `CEGUIBase.dll`), and the game listens to
  `EventMouseEnters/Leaves/Move/ButtonDown/Up/DoubleClick` on its windows. **[read]**
- **Xbox guest:** CEGUI's mouse events and `MouseCursor` are linked (the event names `MouseClick`,
  `MouseButtonDown`, `MouseMove`, `MouseWheel`, `MouseDoubleClick` and the class are in the image),
  but nothing tells whether `System::inject*` survived the linker: no Xbox code needs them. **[read;
  the inject functions not found]**
- **Two ways to give the recomp a mouse:**
  1. Find `System::injectMousePosition`/`injectMouseButtonDown` in the recompiled code (CEGUI 0.6
     source as the map, as `docs/ARCHITECTURE.md` does for OGRE) and feed them from the host's
     mouse, plus a cursor image. Hover, click, drag and the wheel then work as on PC for any
     window whose code listens. Risk: they may be stripped.
  2. Without them: a host hit test over the guest's window tree (window rects through the CEGUI
     calls the menus already use) that moves the game's pad focus to the window under the cursor and
     turns a click into the window's command (`kWindowCommandSlot`) or the pad's A. Works for every
     Xbox menu today (they are built for focus and commands); no drag and drop, no PC behaviour.
  For either way, the host's cursor position has to be mapped to the guest's 1280x720: the native
  renderer knows the presented rectangle (render scale, aspect, letterboxing), so the mapping
  belongs to the backend's present, not to the hooks. CEGUI's own rects are already in the guest's
  pixels, the widened layouts included. The cursor is drawn by the host: in the native mode in the
  backend's UI pass (as the frame counter is); in the Xenos mode only through the SDK's own overlay,
  a separate path.
  Way 2 is a first stage worth having on its own (mouse on the 360 UI); way 1 is what PC's screens
  need.
- **Keyboard:** the recomp already maps keys to the pad; PC's hotkeys (I, S, K, J, P, Q, T, Tab
  for the map) are the PC-only commands above (`GUITOGGLEINVENTORY`, ...) and would go through
  `kCommandNames` like any command.

### 2d. Resolution, scaling and living with the 360 UI

- PC's layouts and imagesets assume 1024x768 and scale with `AutoScaled`; the Xbox scales its
  layouts by an aspect factor (`kGameUiScaleLayout`), and our generated layouts already re-anchor
  bands for wide frames (`wide_layout.h`). PC layouts at 16:9 need the same treatment, per screen,
  or the guest's scaling applied to them. Fonts are FreeType at native resolution in both.
- **Choosing:** a setting per family of screens ("PC menus" on/off), not per screen: mixing a PC
  inventory with an Xbox stash in one session would confuse more than it helps, and the 3D frames
  of PC's menus (`models/*/*_top.dds`) only look right next to each other.
- **Pad on PC's screens:** PC's layouts have no tab order or focus paths; the pad would need one
  per screen (the selector already writes navigation for an Xbox screen). Keyboard and mouse
  could be the only input for PC screens, with the 360 ones kept for the pad.

### 2e. Plan by stages and cost

| Stage | What | Cost | Risk |
|-------|------|------|------|
| 0 | The icon atlas check (1b): one automatic run, checking both the imageset the game opened and the atlas texture the host drew; if they disagree or the mod wins, a recomposed atlas served to both | 0.5 day for the check; 2-3 days with the merge | low for the check; medium for the merge |
| 1 | Mouse on the 360 UI by hit test (2c way 2): cursor drawn by the host, hover moves the focus, click = command/A, wheel = scroll where the menu scrolls | 3-5 days | medium: window rects through guest calls, per-menu quirks |
| 2 | PC's skin in the guest: serve PC's scheme, looknfeel, imagesets and textures from the user's PC copy (like the generated layouts), load the scheme after the game's (`sub_825C9500`); the host frontend must read those textures from the PC copy (as the mods' textures, `mods/texture_files.h`) | 3-4 days | medium: name clashes (`Loading`, the fonts) are skipped by CEGUI 0.6 when present; texture lookup on the host (below) |
| 3 | PC's small shared screens: pet HUD, waypoints, modal, tip, character load, death (2b), PC's layout with the missing Xbox windows added by the layout cache | 1-2 days each | low-medium |
| 4 | Mouse as PC (2c way 1): find CEGUI's inject functions, feed them; hover and click events on PC's windows | 3-5 days if present, much more if stripped | high: unknown until read |
| 5 | PC's gameplay screens (HUD, inventory, stash, merchant, enchant, skills, stats, pet, journal, quests): their behaviour written on the host against the game's data | weeks per screen family; the inventory with drag and drop alone is the biggest | high: a re-implementation, not a port |

What the host's renderer reads for stage 2 (render agent, 2026-10-15; **[read: the code]**): not the
layouts, schemes, looknfeel or imageset XML, which only the guest reads; only the image files of
named textures, by name and resource group (`mods/texture_files.h`), and the font glyphs, which
FreeType renders into textures the guest fills and the host receives as content, with nothing to
add. So the host side of stage 2 is one more location for the resolver, the PC copy's
`Pak.zip` (a reader for its entries and where it ranks against the Xbox pak, the mods and the
replacements), plus the group the guest gives PC's skin. A capture keeps such a file's bytes in its
`TXFL` chunk, so replay needs no PC copy.

Stages 0-3 are a few weeks in all and stand on their own; stage 5 is a project of its own and only
makes sense if the mods' reskins (EE's cleaner HUD, the Diablo 3 HUD) matter enough to the players.

**Prototype:** none in code this night. The smallest clean one (stage 2 plus PC's pet HUD) needs
the scheme load confirmed, the PC copy's files served, and the host frontend reading PC textures:
three changes in three areas (game menu, live frontend, layout cache), too many for a prototype
behind a switch. Stage 1 (mouse by hit test on the Xbox menus) is the better first prototype.
