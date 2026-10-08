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

## 7b. Mounting, registering a mod, writes and saves (2026-10-07)

**Mounting the folder.** The guest reads files through the runtime's virtual file system. The SDK
already offers what is needed, with no patch: `rex::filesystem::HostPathDevice` on a host folder,
`RegisterDevice` and `RegisterSymbolicLink`; the video menu mounts `data/ui/` as `tlhost:` and the
wide layouts as `tlwide:` this way (`src/game_menu/video_menu.cpp`). The mods folder would be a
device of its own on `DataDir()/mods/` (for example `tlmods:`), **writable** (`read_only` false)
because of the compiled files below, and only that folder: never the game's data. **[read]**

**Registering a mod.** `sub_823AA550(manager, const std::wstring& path)` takes the highest
priority among the listed mods, allocates a 200-byte mod object, builds it with
`sub_823A9EA0(mod, path, highest + 1)` and appends it to the manager's list (`+36` data, `+40`
count, `+44` capacity). `sub_823A9EA0` stores the mod's folder at `+164` (adding a trailing `\`
when missing), sets it active (`+192` = 1, `+193` = 0) with that priority (`+196`), checks
`mod.dat` in the folder, reads `NAME`, `AUTHOR` and `DESCRIPTION`, and lists the folder (`*.*`,
`sub_823AA340`) into the mod's file map (`+36`). The manager is the global at `0x83559518`. The
only caller, `sub_82268300(const wchar_t* path)`, takes the path, checks two flags of the global at
`0x8355A9C0` (`+65` clear and `+72` set) and calls it; nothing calls it. **[read]** The order of
registration therefore sets the priorities (each new mod gets the next one); a negative priority
from `mods.dat` can be applied afterwards by writing `+196`, as PC's list does. **[to verify]** that
the two flags are what "the data manager is ready" means, and the exact point at startup.

**Writes.** The data save `sub_82396950` resolves the file through the mod-aware loader, derives
the compiled name (`sub_823A66E0`) and opens it with `"wb"`: a `.DAT` from a mod is compiled into
the mod's own folder, next to its source. **[read]** So the device must be writable, and the
writes stay in the user's mods folder; the game's data is never written (its device stays
read-only). The exact compiled name (`X.DAT.adm`, as in `pak.zip`) is **[to verify]** in a run.

**Saves that reference a removed mod.** **[to verify]** for PC and the guest: what loading a
character does with an item whose unit no longer exists (dropped, kept as unknown, or a failure).
Characters reference their class data; the guest has "Missing datagroup for character " in its
unit spawner (`sub_82287868`, logs and continues) but the save loading path for a missing item was
not read. To check in a run on copies: a synthetic mod that adds an item, a character that holds
it, the mod removed, the character loaded.

**Safety net.** Today the runtime backs a save container up only before deleting it (SDK patch 12,
`<user_data_root>/save-backups/<UTC>-<package>/`, kept by `save_import/backup_retention.h`).
Proposed: at startup, before the guest runs, compare the active mod set (folder names, priorities
and a digest of each `mod.dat`) with the set recorded at the previous start; when it differs, copy
the save containers to `save-backups/<UTC>-mods/` first, so the same retention rules keep it. A
character save does record the names of the mods active when it was saved (section 7d), but only
per character and only for the menu's warning, so "the set of the previous start" stays the
reference for the backup.

**The render front's code.** Adding a mod's folder as an OGRE resource location reuses the video
menu's helper (`AddFileSystemLocation` in `src/game_menu/video_menu.cpp`, the render agent's area).
If it has to be shared (moved to a common place), the commit says so for review at integration.

## 7c. The mod manager is missing on Xbox; its layout (2026-10-07)

**Finding.** The guest's data manager zeroes its mod manager pointer (`+20`) in its constructor
(`sub_8239B998`) and nothing writes it again; the global the dead entry reads (`0x83559518`) has
four readers (`sub_82256AA0`, `sub_8225F590`, `sub_82268180`, `sub_82268300`) and no writer.
PC's class is `CModFileFilter` (RTTI of vtable `0xA90E48`), derived from `CRunicCore`; the Xbox
image has no RTTI, vtable or constructor for it. Its non-virtual methods survive because the data
loader and the MODS check use them. So the guest's mod system has everything except the manager
object itself, which the host must build. **[read]**

**PC layout** (constructor `0x5CE440`, created by `new(0x38)` and stored at the data manager's
`+0xC` at `0x5C1AAA`; base constructor `0x5FC950`):

| Offset | PC | Guest evidence |
| --- | --- | --- |
| `+0x00` | vtable `0xA90E48` (one slot: deleting destructor `0x5CE420`) | needed only by the data manager's destructor (below) |
| `+0x04` | 0 (`CRunicCore` base) | `CRunicCore`'s destructor `sub_823DED38` acts only when it is not 0 |
| `+0x08` | `std::wstring`: the mods folder | not read by any surviving method |
| `+0x24` | list data | `+36` in `sub_823AA550`, `sub_823AAC48`, `sub_823AA988`, `sub_823AA630`, `sub_82256AA0`, `sub_8225F590`, `sub_82268180` |
| `+0x28` | list count | `+40`, same functions |
| `+0x2C` | list capacity | `+44`, same functions |
| `+0x30` | 1 | `+48`: read by the list's growth `sub_8230AB58` (`+12` of the list) |
| `+0x34`, `+0x35` | 1, 0 (save `mods.dat` flags, used by PC's destructor `0x5CDBB0`) | not read by any surviving method |
| global | `0xEBBC7C` = this | `0x83559518` (the four readers above) |

The base constructor also increments the instance counter (PC `0xF36C48`; guest `0x8355A2A0`,
incremented by every `CRunicCore` constructor, e.g. `sub_823A9EA0`, `sub_8239B998`). All guest
offsets match PC's; no field is unexplained for what the guest uses. **[read]**

**Lifecycle.** Allocation with the game's allocator `sub_821CD7F8(0, size)` (as `sub_823AA550`
allocates a `CMod`); the matching free is `sub_821CD9A8`. The data manager's destructor
`sub_8239CFB8` calls slot 0 of `+20`'s vtable when it is set, then zeroes it. Since `CModFileFilter`'s
vtable does not exist on Xbox, the object uses `CRunicCore`'s own vtable `0x820D61FC`, whose slot 0
`sub_823DECE0` runs the base destructor (nothing to do with `+4` = 0) and frees the object with
`sub_821CD9A8`. The list buffer and the `CMod` objects are not freed then (PC's destructor would);
that only happens when the data manager itself is destroyed, at shutdown. **[read]** The data
manager is never rebuilt while the game runs: its constructor is called only by the global data
loader `sub_8231FF28` (`new(136)` @`0x823200BC`), which only `CGame`'s vtable slot 2 `sub_82206000`
calls; that method loads `plugins.cfg` and sets the engine up, once per process. **[read]** So the
host builds the mod manager once, and nothing frees it twice.

## 7d. The mod list in character saves: a writer bug of the Xbox build (2026-10-07)

**Finding.** A character saved with mods active could not be loaded again: the loading screen
never ended (validation run 2, loading it without mods; frames kept being presented). Run 1's hang
on the way back to the menu, with mods, is probably the same and is checked by the next run. The save
records the active mods' names in the player's unit, and the Xbox build writes that list wrong.
Nothing showed it before because without a mod manager the list is always empty. **[read]**

- **Saving**: `sub_822D66A8` asks the data manager for the active mods' names (`sub_8239E718`:
  data manager `+20`, each `CMod` with `+192` set, its name at `+80`) and appends them to the
  unit's save data (`+576`, a vector of `std::wstring`). The unit writer `sub_822A68F0` writes the
  u32 at `+568`, the count, and per name the length and the UTF-16 units.
- **The bug**: the length is loaded as a u32, stored on the stack, and 2 bytes of it are written
  (`@0x822A74FC`). On big-endian those are the high half: 0. The units follow with the right
  size. PC, little-endian, writes the low half, the real length.
- **Loading**: the unit reader `sub_822A7D78` takes a u16 length (`lhz` `@0x822A8A44`) before
  each name, so every name reads as empty and the units as the next fields. In the run's save the
  item list then claimed 4,291,686,325 entries.
- **Use**: `sub_822D6858` copies the list to the player (`+2524`). The main menu (`CMainMenu`,
  vtable slot 3 at `0x820D2450`: `sub_8238C3A8`) compares the current character's list with the
  active mods and calls `setVisible(1)` on `CharacterModsWarning` when they differ
  (`@0x8238C930`..`@0x8238C944`). On Xbox that widget is a `Text` with no text
  (`main_xenon.layout`, bottom right of the main menu) and no code gives it one (`CMainMenu`
  only finds it and hides it, `@0x8238C0A8`..`@0x8238C0C4`), so the warning shows nothing.
  Making it visible would be new UI work (a translated text); not done.
- **Checked on the run's save** (outside the repository): with only the ten lengths written, the
  save parses completely with `tools/save_convert`'s schema; the saves made without mods have an
  empty list.

**Correct format**: a u16 length, then the UTF-16 units, both big-endian: what the reader
takes, what PC writes (in its own byte order) and what the save converter and the in-game import
produce from a PC save (`wstring16` in the schema; tests with a filled list in
`tests/save_convert` and `src/save_import/save_tree_test.cpp`). An imported PC character saved
with mods therefore loads, and the menu warns when those mods are not active. **[read + tested]**

**Repair while writing** (`src/game_menu/mods_install.cpp`, `src/mods/saved_mod_list.h`). The
host wraps `sub_822A68F0`.

- **Units without names** (all of them without mods) only call the original.
- **Otherwise** it keeps the stream's position, calls the original, and searches the bytes that
  call wrote for the list exactly as the bug leaves it: the u32 from `+568`, the exact count, and
  every name with length 0. If the list is found exactly once, it writes each length (big-endian
  u16).
- **Why this is safe**: the save's size does not change, nothing reaches the file during the
  writer, and the hash is computed afterwards (`sub_8221CC70`), so it covers the repaired bytes.
- **Fallback**: in any other case (not found, found twice, a name longer than 65,535 units) it
  rewinds the stream (position `+16` and size `+24`) and writes the unit again with the list
  emptied, as every Xbox save is. The log says it plainly (`mods: FALLBACK: ...`).
- **Writing a unit twice is safe** **[read]**: across the writer, the item writer
  `sub_822C9AB8`, `sub_823A24D8` and `sub_823A20E8`, the only stores outside the stack are the
  stream's fields (`+4` when it grows, `+16`, `+24`) and the guest's write-error flag
  `0x8355A268`, whose 148 stores all sit behind a branch taken on a complete write. The temporary
  strings live on the stack (`r1+96`) and are freed in the same call; the unit and its items are
  only read. Tested with a stand-in writer in `src/game_menu/save_mod_list_test.cpp` (repair,
  moved buffer, fallback, no names).

**Not done: repair at load.** Saves already written with the bug cannot load. Only test saves
exist today. **Condition for any release that ships mods without this repair** (or for builds of
this branch from before it that reached players): add a check at startup, before the guest runs,
that finds this pattern in the character saves with the schema, repairs the lengths in place
after a backup, and logs it.

## 7e. New-item mods in validation: what kept the unit out (2026-10-08)

A synthetic mod (outside the repository) adds a sword, a text `.DAT` under
`media/units/items/`, in the encoding PC mods ship (UTF-16LE with a BOM). Runs on copies of the
saves, with the mods diagnostics (`TORCHLIGHT_MODS_DIAGNOSTICS`), found three causes, one after the
other:

1. **The mod's subfolders were never listed.** The game asks for a mod's subfolders with the
   CMod's folder, kept with `/`, plus `"/*.*"` (`tlmods:/<mod>//*.*`, sub_823A1010). The Xbox
   library's FindFirstFileA splits a path only at `\` and returned 0xC000000D without opening
   anything (guest_abi `xapi_files.h`), so the mod's file map held only `MOD.DAT`. Fixed in our
   hooks for `tlmods:` only (`hooks/find_file_hooks.cpp`); OGRE's recursive searches on `game:`
   ask for `<dir>/*` the same way and are left as the Xbox reads them, since made global the fix
   would start loading `game:\RTShaderLib\materials\RTShaderSystem.material`. The SDK wildcard
   patch (patches/README.md, 20) was not the cause.
2. **An index with the unit left out was cached under its key.** The key covers only the mods'
   files and the base, so the index built while the subfolders were invisible was reused after
   that was fixed. Now such an index serves its start only (`IncompleteUnitIndexName`,
   `kUnitIndexVersion` 3).
3. **The game's text `.DAT` reader reads UTF-16 big-endian.** `sub_82399C00` (and the `FILE*`
   variant `sub_82399B00`) loads the file into 16-bit units as they are in memory and skips a
   first unit of 0xFFFE, which is how the bytes `FF FE` of a little-endian BOM read on the Xbox; it
   swaps nothing after it, so a PC `.DAT` reads as garbage and the unit has no `UNIT_GUID`. The
   game's own data is compiled (`.DAT.adm` only in `pak.zip`), so this reader never saw a PC file.
   A conversion on our side, without touching the files on disk, is **[proposed]**.

**A guest fault never ends the process.** In each of the four runs where our index build had the
game load the mod's unit and it was left out (117, 129, 130 and 161), the game soon read guest
address 0x1AC and then looped on `Unhandled guest access violation`, logging tens of megabytes in
seconds (run 161: 14 rotated log files) until it was killed; the runs that used a cached index
did not. Run 161 rules out the item with an unknown unit as the trigger: the save protection had
already removed it, and the loop came at the title screen, before any character was loaded. What
reads 0x1AC is **[to find]**. The runtime side (an unhandled guest fault hangs the process instead
of ending it with an error; `RtlUnwind` is a stub) is in patches/README.md, known gaps.

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

## 10. Validation mods (candidates, not downloaded)

Development uses synthetic mods made by the tests. For the final validation the user downloads real
mods from a browser to `~/torchlight-mods-test/` (outside the repository and the real user folder);
they are checked to be data folders only (no executables, installers or DLLs). Candidates found on
2026-10-07 (automatic download was not possible: ModDB answers 403, Nexus requires an account,
ModDrop has no direct link, the community site did not answer):

| Mod | Version | Covers | Source |
| --- | --- | --- | --- |
| TET: Torchlight Enhanced Textures | 0.9.5 | textures only | https://www.moddb.com/games/torchlight/addons/tet-torchlight-enhanced-textures-v095 |
| SSS Torchlight Texture Project | 1.31 | textures (large, about 149 MB) | https://www.moddrop.com/torchlight/mods/660498-sss-torchlight-texture-project-v131-updated |
| Grimm Overall Improved Torchlight | — | data changes | https://www.nexusmods.com/games/torchlight/mods |
| Grimm Reworked Spells | — | data changes (spells) | https://www.nexusmods.com/games/torchlight/mods |

## 11. Implementation status (2026-10-07, branch feature/pc-mods)

- `src/guest_abi/mods.h`: the data manager hook points, the mod manager layout (from PC's
  constructor, checked against every surviving guest reader), `InitModManager`,
  `PublishModManager`, `ActiveModCount`; tests on synthetic memory.
- `src/mods/`: the text data format, the folder scan, `mods.dat`, the registration plan
  (`ModManagerNeeded`: a manager only with at least one mod) and the save safety net (backup when
  the set changes, including the first time mods appear); tests on synthetic mods.
- `src/game_menu/mods_install.cpp`: `InstallMods` before the guest runs (scan, `mods.dat`, backup,
  `tlmods:` writable mount) and `RegisterMods` from the data manager constructor's hook (build and
  publish the manager, register each mod with `sub_823AA550`, disabled priorities, resource
  locations after the game's). The hook is the video menu's (`video_menu.cpp`): one override per
  guest function.
- Achievements: the PC set accepts the guest's MODS_1/5/10 completions; the list shows all 66 as
  earnable.
- Save mod list (section 7d): the unit writer's bug repaired as the save is written; tests of the
  repair on synthetic stream bytes and of the PC import with a filled list.
- Validation run 1 (synthetic mods): mount, backup, manager, MODS toasts, texture replacement and
  the achievement list confirmed; it also found the save bug of section 7d.
- Validation of the repair (2026-10-07, saves restored from before run 1, default settings):
  with 10 mods, a character loaded, saved on the way back to the menu and loaded again, with no
  hang. Both saves logged `repaired (10 names)` and no fallback, and the save parses completely
  with the 10 names. With one mod removed (9), the same steps: the backup ran, both saves logged
  `repaired (9 names)`, and the save holds the 9 names. The menu's mod warning cannot be seen on
  Xbox (no text, above).
- Next: a save holding an item of a mod that is then removed (needs new-item mods, blocked by the
  prebuilt `UNITDATA.RAW`).
