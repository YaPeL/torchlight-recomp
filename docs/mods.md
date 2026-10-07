# PC mods on the Xbox 360 build: feasibility (2026-10-07)

Feasibility study only: no code. Question: can the recomp load Torchlight PC mods, and with them
earn MODS_1, MODS_5 and MODS_10, which are out of scope today (achievement-coverage.md)?

Sources: static reading of the PC executable (`reference/pc/Torchlight.exe`, version 1.15, OGRE 1.6)
and of the recompiled guest, plus the file lists of both `pak.zip`s. Nothing from a mod or from the
game is in the repository; addresses and names below are evidence pointers. Each statement is marked
**[read]** (seen in the code or the data) or **[to verify]** (inferred; needs reading or a run).

## 1. How Torchlight PC loads mods

**Where.** `0x5C68C0` builds the user folder with `SHGetSpecialFolderPathW` + `"/Runic Games/Torchlight/"`;
the mod manager appends `"mods/"` (`0x5CE4D6`). So mods live in
`%APPDATA%\Runic Games\Torchlight\mods\<mod>\`, one folder per mod, on the player's machine.
**[read]**

**The manager.** The global data loader `0x5258F0` (the same function that loads the FAMEGATE
graph and checks MAX_FAME's data) creates it: `new(0x90)` + constructor `0x5C4E20`, stored at
`+0x5C` (`0x525AFD`). Its initialisation `0x5CE440` scans `mods/`, reads and rewrites the list file
`mods.dat` (keys `DIRECTORY`, `PRIORITY`, `COMPRESSED`, `CHECKFORNEW`) and calls `SteamApps`.
**[read]**

**One mod.** `0x5CD150` loads a mod's descriptor `mod.dat`: a `MOD` block with `NAME`, `AUTHOR` and
`DESCRIPTION`. **[read]**

**Applying a mod.** `0x5CCE30` unloads a resource group, lists the mod folder's files (`"*.*"`,
through `0x5C73E0`), reloads the game data (`0x524EC0`, `0x526630`) and calls OGRE's
`ResourceGroupManager::setResourceGroupAllowedToAccessFileSystemOnSearch`, a Runic addition to OGRE
1.6, so the files of the mod folder are found by path. **[read]** That a mod's files override the
base files with the same path under `media/`, and that new files are picked up because the data
loader indexes whole folders, is how PC mods are known to work. **[to verify]**

**Order.** Each mod has a `PRIORITY` in `mods.dat`. A mod counts as active when its flag at
`+0xD0` is set and its priority (`+0xD4`) is not negative (`0x5CDAC0`). **[read]** That a higher
priority wins when two mods replace the same file is **[to verify]**.

**Formats.** A mod folder mirrors `media/`: data files (`.DAT`, which the game compiles to its
binary `.ADM`; the compiled form can be saved, "Unable to save file:" with `.ADM`), meshes
(`.MESH`, `.SKELETON`), materials, textures (`.DDS`), UI layouts and sounds. **[read]** for the file
kinds the loaders accept. Whether mods ship text `.DAT`, compiled `.ADM`, or both, **[to verify]**
with a real mod.

The developer console has a `MODS` command (`0x5683BC`) that lists them. **[read]**

## 2. What the Xbox 360 build keeps

| PC piece | Guest | State |
| --- | --- | --- |
| Folder scan and list, `0x5CE440` (`mods/`, `mods.dat`, `PRIORITY`, `CHECKFORNEW`) | none | **Removed**: none of those strings is in the image, and `tools/guest_re/pc_match.py` finds no counterpart |
| One mod's descriptor, `0x5CD150` | `sub_823A9EA0` (same strings: `mod.dat`, `MOD`, `NAME`, `AUTHOR`, `DESCRIPTION`) | **Dead**: its only caller `sub_823AA550` is called only by `sub_82268300`, which nothing calls or references |
| Manager object | created by the global data loader `sub_8231FF28` (`new(136)`, `sub_8239B998`, stored at `+92`) | **Kept, always empty** |
| MODS check | `sub_8231FF28` @`0x82320A38`..`0x82320AC4`: count `sub_823AAC48` ≥ 1 / 5 / 10 → events 26 / 27 / 28 → completion `0x823D9930` | **Kept, never true**: nothing adds mods |
| File loader | `sub_8239D5F8` (`.ADM`, `.CMP`, `DDS`), `.ADM` save `sub_82396950`, "DataGroups can't load compressed files" | **Kept, mod-aware** (section 7) |
| File lookup in mods | `sub_8239D0E8` → `sub_823AA988`: each active mod's file map (mod `+36`) | **Kept** (section 7) |
| Text `.DAT` parser | `sub_82396BF0` (virtual, vtable entry `0x820D2A64`) → `sub_82396D60`, type names `sub_82393EA0` | **Kept** (section 7) |

So, as with GUIFEEDPET, the Xbox build keeps almost the whole chain (the manager, a descriptor
reader, the mod-aware file lookup, the text parser and the MODS check) and lost only its entry: the
folder scan that fills the list. **[read]**

The Xbox `resources.cfg` (the user's install) adds `Zip=game:\pak.zip`, music, programs and shader
folders, and `localization_zip` entries described as "the game will look in here first for assets"
(only the current language's). The loose-file root `FileSystem=game:\` is commented out. **[read]**
How the guest orders a `localization_zip` ahead of `pak.zip`, and whether more `Zip` or `FileSystem`
lines would be searched, **[to verify]** in the guest's `resources.cfg` reader.

## 3. PC data against Xbox data

| | PC 1.15 `Pak.zip` | Xbox `pak.zip` |
| --- | --- | --- |
| Files | 22,426 | 21,403 |
| Game data | 6,165 `.ADM` | 6,187 `.ADM` |
| Meshes | 3,311 `.MESH` | 3,314 `.MESH` |
| Textures | 2,771 `.DDS` | 2,832 `.DDS` |
| UI | 35 `.LAYOUT` | 90 `.LAYOUT` + 48 `.UILAYOUT` (the Xbox UI) |

- **`.ADM`**: the same binary format (version 1, a string table at the top of each file, then the
  tree), read by the same parser in a test of both. The string ids differ between the builds, but
  they are local to each file. **[read]**
- **`.MESH`**: the same OGRE serializer (`[MeshSerializer_v1.40]`, little-endian); the headers of a
  sample mesh are byte-identical. **[read]**
- **`.DDS`**: standard DDS in both; the Xbox copies of the sampled textures are at lower resolution
  (256 against 512). **[read]**
- **Content**: the Xbox data has its own items (for example the `360_` belts) and its own UI
  (`*_xenon` layouts); the PC data has PC menus. **[read]**

Consequences:
- **Assets** (meshes, textures, materials, sounds): the same formats. A PC mod's assets load as they
  are; larger textures cost memory (the guest has the Xbox's 512 MB). **[read]** for the formats.
- **Game data** (`.ADM`, or `.DAT` compiled to it): the same format. A data mod (an item, a skill, a
  monster, a balance change) loads as it is if what it references exists in the Xbox data. If mods
  ship text `.DAT`, either the guest compiles it (whether its text parser is kept is **[to verify]**)
  or a host tool converts it to `.ADM` once.
- **UI mods** (PC `.LAYOUT`, menus): not usable; the Xbox UI is different.
- **Mods that need PC code** (new unit types, PC-only systems): not usable.

## 4. The MODS achievements

PC (`0x52606D`..`0x5260C6`, in the global data loader, at startup): after loading the mods, the
number of **active** mods (`0x5CDAC0`: flag `+0xD0` set and priority `+0xD4` ≥ 0) is compared with 1,
5 and 10; each threshold completes its achievement (events 26, 27, 28). Nothing else: a mod's
content does not matter, only that it is in the list and active. **[read]**

The guest keeps the same check with the same rule (count `sub_823AAC48`: flag `+192`, priority
`+196` ≥ 0). **[read]** Today the PC set ignores the guest's events 26..28 (`QualifiedGuestCompletion`),
and they never fire anyway.

To earn them faithfully: a real list of active mods at the moment the global data loader runs, and
the unlock at that point, as with MAX_FAME. The achievements would then follow from loading mods,
not from a separate count.

## 5. Options

| Option | What | Cost | Risks |
| --- | --- | --- | --- |
| **A. Asset and data override** (recommended first) | The host scans a mods folder in the user's data folder (same layout as PC's: `<mod>/mod.dat` and files under `media/`), orders mods by a list like `mods.dat`, and makes the guest find those files before `pak.zip`. Where the guest looks first is the open point: its own resource search (the `localization_zip` mechanism or extra resource locations added the guest's way) or the runtime's file layer. | Medium: reading the guest's resource setup, a host scanner, tests with synthetic mods | Whether new files (not only replacements) get indexed; memory; saves that reference a mod's items without the mod |
| **B. Restore PC's manager** | Rebuild the folder scan on the host and feed the guest's kept pieces (the dead descriptor reader `sub_823A9EA0` and the manager object) so the guest's own MODS check runs | Higher: the kept code may be incomplete; more guest calls (the hang of 2026-10-06 came from calling guest code from a hook) | Partial code paths; harder to test |
| **C. Conversion tool** | A host tool that turns a PC mod's text `.DAT` into `.ADM`, if the guest cannot compile it | Medium; only if needed | Format details of text `.DAT` |
| **D. Achievements only** | Count active mods in the folder and unlock MODS_* without loading them | Low | Not faithful: an achievement for something that does not happen. Not recommended |

**Which mods first:** texture and mesh replacements (pure assets, same formats), then data changes
that only modify existing records (balance, drop tables), then mods that add new items or
monsters (need the data indexing to see new files). UI mods and mods that rely on PC code are out.

## 6. Constraints

- Mods are installed by the player in their own data folder; no mod, no file from a mod and no
  game file goes into the repository, docs or tests.
- Tests use synthetic mods made by us: small `.ADM` and asset files generated by the tests, with
  invented names, exercising discovery, order, activation and the MODS thresholds.
- Development runs use copies (`--user_data_root`), as always.

## 7. Code readings (2026-10-07)

**1. Resource locations and their order.** The Xbox `resources.cfg` is read by the data manager's
constructor `sub_8239B998` (guest_abi/game_ui.h `kResourcesCfgLoader`): OGRE's config file with
`\t:=` separators; `Zip`/`ZIP` entries go to `addResourceLocation` (`kAddResourceLocation`,
`0x8242AE30`, calls @`0x8239C574`, @`0x8239C5FC`, @`0x8239C6B0`), the language zips are picked by
name (`pak_fr`, `pak_de`, `pak_es`), and a data group `MAINDATA` is set up. The string
`localization_zip` is not in the image: the keys the reader compares are `Zip`/`ZIP`. **[read]**
In OGRE 1.7, `addResourceLocation` indexes the archive's files right away and
`ResourceGroup::addToIndex` overwrites the index entry of a file already there
(`OgreResourceGroupManager.cpp`: `resourceIndexCaseSensitive[filename] = arch`); `openResource`
uses that index first. So, within a group, **the location added last wins** for a file present in
several; that is how a language zip added after `pak.zip` replaces its files. **[read]** (OGRE 1.7
source, the guest's base) The project already adds a resource location from the host at a safe
point (`src/game_menu/video_menu.cpp`, `kAddResourceLocation` through `GuestCall`).

**2. How the game finds data files, new ones included.** The data loader `sub_8239D5F8` is
mod-aware: when the data manager's flag `+56` is set, or its mod manager (`+20`) has active mods
(`sub_823AAC48` ≠ 0), it checks the file's freshness (`sub_8239D4C8`) and looks the file up in the
mods first (`sub_8239D0E8` → `sub_823AA988`: over the active mods, flag `+192` and priority `+196` ≥
0, a lookup in each mod's own file map at mod `+36`, `sub_82328620`). **[read]** A mod's file map is
what the (dead) descriptor reader fills when it lists the mod's files (`"*.*"` on PC, `0x5CCEC6`).
So files a mod adds, not only the ones it replaces, are found through the mod, as on PC. Whether
the game's lists of units and other records are built from folder listings that include mod files
is **[to verify]** during implementation (it decides "new items" mods; replacements do not need it).

**3. Text `.DAT`.** The guest keeps the text data parser: `sub_82396BF0`, a virtual method (vtable
entry at `0x820D2A64`), resolves the file through the mod-aware loader and parses it with
`sub_82396D60`, which reads the value types by name through `sub_82393EA0` (`INTEGER`, `INTEGER64`,
`FLOAT`, `DOUBLE`, `UNSIGNED INT`, `STRING`, `TRANSLATE`, `BOOL`; `sub_82394190` the reverse).
Together with the freshness check and the `.ADM` save (`sub_82396950`), the Xbox build can read a
mod's text `.DAT` like PC. **[read]** A host converter (option C) is not needed.

**4. A real PC mod** (folder layout, `.DAT` or `.ADM`): pending; the user will point at one on
their machine, nothing of it enters the repository.

**Consequence for the options.** The guest keeps its own mod system except the entry that fills
the list. Option A can therefore use the guest's own mechanism instead of extra OGRE locations: the
host finds the mods and adds them to the guest's list the way PC's scan does, and the guest's
loader, priorities, text parser and MODS check do the rest. Assets (meshes, textures) that OGRE
loads by name additionally need the mod folder as a resource location added after `pak.zip` (rule
of reading 1), which is the mechanism the project already uses.

## 8. Where mods go on our side

- **Folder:** `mods/` inside the TorchlightRecomp user data folder (`platform::DataDir()`:
  `~/.local/share/TorchlightRecomp/mods/` on Linux, `%USERPROFILE%\Saved Games\TorchlightRecomp\mods\`
  on Windows), with PC's layout: one folder per mod with its `mod.dat` and its files under `media/`.
  A PC mod is copied in as it is.
- **Priority:** PC keeps it in `mods/mods.dat` (`DIRECTORY`, `PRIORITY` per mod; a negative priority
  disables a mod). The same file, in the same format, decides ours: if the player copies PC's
  `mods.dat` with the mods, their order is kept; a mod with no entry is added after the others, in
  folder-name order, and the file is written back, as PC's `CHECKFORNEW` does. A later menu could
  edit it; not in the first version.
- **Import from PC:** PC's mods are plain folders in the same layout, so the first version only
  documents "copy the mod folders (and `mods.dat`) from `%APPDATA%\Runic Games\Torchlight\mods\`".
  An in-game import like the saves' (which reads `<game_data_root>/import/`) is cheap to add later:
  copy, never move, and keep PC's `mods.dat` priorities. Not needed to start.

## 9. Future work: UI and input crossover

Two ideas, outside the mods task, recorded for later:
- **PC UI and mouse in the recomp** (this project): the Xbox `pak.zip` carries PC's `.LAYOUT` files,
  but each is driven by a PC menu class; a survey (like section 2) of which PC menu classes and
  which mouse/picking code the guest keeps comes first. Menus are CEGUI windows, so mouse input
  there may be the cheaper part; click-to-move in gameplay is the expensive one.
- **Xbox UI and a gamepad in Torchlight PC** (a separate product, not this repository): injected code
  for the gamepad (PC reads input through OIS, which supports joysticks; wrapping `OIS.dll` avoids
  touching the executable) plus a mod with the Xbox `.UILAYOUT` files, whose imagesets and fonts can
  only come from the player's own Xbox copy; PC's menu classes look windows up by other names, so a
  bridge or adapted layouts are needed.
