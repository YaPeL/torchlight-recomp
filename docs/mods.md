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
   PC's reader is the same code (it skips 0xFEFF, `0x5C1499` and `0x5C173A`) and converts nothing:
   PC text files are UTF-16LE with or without a mark, and UTF-8 or ANSI fail there too. The Xbox
   game writes its own text files as `FF FE` followed by big-endian units (`settings.txt`), the
   mark its reader skips. `mods/text_reader_hooks.cpp` turns a little-endian file's units around in
   the guest's buffer after the reader loads it; the files on disk are not touched.
   `mods/text_encoding.h` tells the orders apart on up to 64 characters after the mark (which byte
   of each is zero), and leaves a file whose order is unclear as it is, with a line in the log.

**Compiled `.ADM`.** The game looks for `<file>.adm` before the text file. PC's and the Xbox's
`.ADM` are the same format, both little-endian (version `01 00 00 00`, UTF-16LE strings; e.g.
`media/units/items/BASE.DAT.adm` in both paks), so the game has nothing to tell apart and a mod's
`.ADM` compiled on PC reads as it is. Freshness is by modification time (`sub_8239D4C8`: each
file's time from its pak, or `_stat64` `sub_8285FB40`, -1 when missing): the `.ADM` is used when
it is as new as the text file or newer, otherwise the text is read and compiled again, written
next to it (section 7b), as on PC. So the game can rewrite a `.ADM` inside a mod's folder (one a
mod ships older than its text file, or one it compiled itself), exactly as PC does; the project
does not redirect those writes.

**A guest fault never ends the process.** In the runs where our index was loaded (117, 129, 130
and 161, then every run since), the game soon read guest address 0x1AC and then looped on
`Unhandled guest access violation`, logging tens of megabytes in seconds (run 161: 14 rotated log
files) until it was killed. Run 161 rules out the item with an unknown unit as the trigger: the
save protection had already removed it, and the loop came at the title screen, before any
character was loaded. Cause 4 below is why. The runtime side (an unhandled guest fault hangs the
process instead of ending it with an error; `RtlUnwind` is a stub) is in patches/README.md, known
gaps.

4. **The game never read our index file.** Under gdb (runs capped by `tools/run_capped/run_capped.py`), the
   read of 0x1AC is in the game's state update `sub_82212950`: it looks up the default class,
   "Destroyer", in a `CResourceManager` (`sub_823DE8F0`, returning to 0x822131C4), and when that
   fails, again with a fallback name (returning to 0x82213210), whose result it does not check
   (`lwz r11,428(r3)` with r3 = 0). With our index built from the base alone
   (`--mods_unit_index_base_only`, diagnostics), the file we write is byte for byte the Xbox
   `media/UNITDATA.RAW`, and the loop still came. The loader's reader then held 0 bytes and the
   index's GUID map 0 entries: the game looked for `<key>.RAW` in the mod (both misses are in the
   log) and nowhere else, so the loader skipped the whole file with no error (guest_abi
   `unit_index.h`, `index`). The loader uses the path for nothing else: its caller passes a
   temporary string and keeps nothing, the reader splits it into folder and name only to open it,
   and nothing reads it again or registers it in a group. The data loader resolves a name in the
   mods first, then (only when the data manager's flag `+16` is set) with OGRE's `resourceExists`
   over its groups and then "General" (`0x8349D568`, the group our `tlunits:` location is in), then
   with `_stat64` on a folder of its own (`sub_8239D0E8`). Read under gdb: `+16` is set, the groups
   are `0ZIP0` and `ZIP`, and the game asks for `57930ADE12DF44C0.RAW`, **in upper case**, while
   the file was `57930ade12df44c0.RAW`: not found (3). With an upper-case copy in the cache the
   game read the whole file (676106 bytes, 3377 units with the mod's sword) and found "Destroyer"
   at once. That copy was read because it was in the folder when `tlunits:` was mounted, not
   because of its case (see "folders we mount" below: a guided run with the upper-case name built
   in the same start loaded 0 again). The cache's file names are upper case anyway
   (`UnitIndexFileName`, `kUnitIndexVersion` 4), as the game asks for them.

**Rule: files we create for the game.** The data loader asks for every name in upper case, and a
resource location on a host folder is matched by case on Linux (Windows and macOS ignore case by
default, so the same mistake would not show there). A file we create for the game to find through
a resource location is named exactly as the game asks for it: in upper case when the game reaches
it through the data loader (the unit index), with the game's own spelling when it asks by its own
name through CEGUI or OGRE (our video layout on `tlhost:`, which we load by our own name; the
widened layouts on `tlwide:`, which keep the pak's names). guest_abi `game_ui.h` has the lookup
and the rule. **[to verify]** with a mod whose files are named in lower case, as PC mods often
are (Windows ignores case): the mods' file maps, which the game fills from the folder listing and
looks up in upper case, and the mods' assets that OGRE finds through the mod folders' locations.
Those are the player's files, so the fix, if needed, cannot be renaming them.

**Rule: folders we mount, and files created in them later.** A host folder mounted as a device
(`HostPathDevice`) is read into memory when it is mounted. Opening a file by its path still finds
one created later (the device falls back to the disk, ignoring case), but listing the folder
returns what was there at the mount, plus what the guest created through the device itself
(`Entry::CreateEntry` adds it). OGRE indexes a resource location by listing it when the location
is added, and the game fills a mod's file map by listing it when the mod is registered: both are
snapshots. So a file the host writes into a mounted folder behind the device's back is never
listed, and any file created after those snapshots is missing from them until the next start.
That is what kept our unit index out: written by the host after `tlunits:` was mounted, it was
never listed (case did not matter: with the file there at the mount, its lower-case name was read
too). `tlunits:` is now mounted in the index loader's hook, once the file exists
(`MountIndexFolder`). The other mounts: `tlhost:` holds files shipped with the executable,
`tlwide:` is written before it is mounted, and the language pack is on the game's own device, so
none of them is affected. On `tlmods:` the game itself writes the `.ADM` files it compiles, through
the device, so the device lists them; but a `.ADM` compiled during a session is in neither the
mod's file map nor OGRE's index until the next start. In that session the game reads and compiles
the text file again, as it did the first time; PC fills its file maps once at start too. Harmless
today; noted so that nothing of ours relies on a file created in a mounted folder being found in
the same session.

**What guards against it now.** After the game loads our index, `mods/unit_index_install.cpp`
reads the size of its GUID map and compares it with what the game keeps of the file we wrote
(`CheckLoadedIndex`): the distinct GUIDs of the entries with a NAME (`LoadedUnitCount`; the loader
drops the 113 Xbox entries without one, so the Xbox file loads as 3376). Nothing loaded: a clear error in the log, and the game's own index is
loaded instead with its own path (nothing was inserted, so that is the load the game would have
done), without the mods' units. Part of it loaded: an error, and the index is left as it is, since
going back after a partial load is not safe (a replaced entry is freed but stays filed under its
other names); see "a partial load" below for why the index is not reset. The saves are checked
before the guest runs, when nothing can be reading or writing them, against the index the game is
expected to load (the cached one, or the base plus the GUIDs the mods' definitions set). After the
load only a comparison runs: units the check counted on that the game does not hold are logged
and told on screen, and the saves are not changed again.

**A partial load: no reset (decided 2026-10-09).** Going back to the Xbox index after a partial
load would mean destroying the index object in place and building it again. The object is a
`CUnitResourceList` (vtable `0x820C7A08`; slot 0, `sub_82329448`, the deleting destructor). Its
destructor `sub_82329588` destroys every entry in a list at `+76` through the entry's own deleting
destructor, frees the objects at `+116` and `+92`, clears the singleton `0x835594EC`
(@0x8232966C) and destroys the maps at `+60` (by GUID), `+44`, `+28` and `+12`. After that,
`sub_82329310` would build it in place and load `MEDIA/UNITDATA.RAW`. The risk: in a partial load
the loader has freed each entry it replaced, and if such an entry is still in the `+76` list the
destructor frees it a second time, corrupting the guest's heap. The loader does not touch `+76`
in the code read, so what fills that list is not known. Not reset, because a partial load is
practically impossible (the loader reads the whole file at once, our writer gives the Xbox file
byte for byte, and the one failure seen gave 0, not part), and because the protection already keeps
items from being lost: a partial load is logged as an error, the comparison then counts every
mod unit as missing, and if a save holds one, the saves are copied and nothing is saved for the
session.

**Saves holding units the game did not load.** In the guided run of 2026-10-08 (the index not
read, the Xbox one loaded instead) the game loaded the character that carried the mod's sword,
dropped the sword it did not know, and its save on the way back to the menu wrote the character
without it. A notice is not enough for that. So when the comparison finds units missing, before
any character is loaded: the saves holding them are found (read only, `SavesHoldingUnits`), every
save is copied (`save-backups/<UTC>-units-not-loaded`), and **nothing is saved for the rest of the
session** (`SavingBlocked`, `mods/save_block_hooks.cpp`). The block skips the character save
(`kSaveCharacter`, guest_abi `save_menu.h`) whole: it is the only code that writes save data (the
stash is written inside it), its callers do not read its result, and skipping it leaves the files
as they were (no `save.tmp`, no renames). The rest of the save code was read for this: the other
users of the save container read or list, except deleting a character (on the player's request)
and the storage state machine's container delete (on "Yes" to "Corrupt/Damaged Save").
Refusing writes from the host (a read-only save device) was not taken: it needs an SDK change,
and the game's storage error paths end in dialogs, one of which deletes the container. The notice
at the character list says in capitals that nothing is saved in the session, which saves are
affected and where the copy is. Validated in a guided run of the failing case (2026-10-08): the
notice showed, the game's save on the way back to the menu was skipped ("character not saved"),
the character file kept the sword, and the copy was made.

**Each mod its own device (2026-10-09).** The kernel refuses in a path the characters the Xbox
does not allow in a name (`"`, `+`, `,`, `<`, `>`, `|`; the SDK's `IsValidPath`, status
0xC0000033). PC mods are named on Windows, which allows them: 5 of the Ultimate Torchlight
Mod-Pack's 29 folders have a comma ("JCC - Class Skills - Airbender, Paladin and Sorceress"), and
with every mod under one device (`tlmods:\<folder>\`) the game could open nothing in them (empty
file maps, the mods inactive, their classes left out of the index). Each mod's folder is now
mounted as its own device, `tlmod<NNN>:` (three digits: the file system matches devices by prefix, so `tlmod1:` would take `tlmod10:`'s paths) with NNN its place in the plan (`hooks/guest_path.h`
`ModDeviceLink`), so the folder's name never reaches a guest path; folders whose names are not
ASCII work the same way. Checked before the change, nothing else depends on the folder's path: a
mod's name (in the list and in saves) is its `mod.dat` `NAME` (CMod +80, read in `sub_823A9EA0`,
"MOD" without one), the priorities and the MODS count follow the plan's order as before, the
devices are writable for the `.ADM` the game compiles, and OGRE's locations and the separators fix
(`ModsSearchPath`) take the new names.

**Test runs with real mod packs: the log (2026-10-09).** With dozens of mods the game looks every
data file up in each mod's folder, and the SDK logged each failed open as a warning: about 124,000
lines (14.5 MB) in the first 45 s with the Ultimate Torchlight Mod-Pack's 29 mods, before the unit
index loads. For a while, runs with real mod packs had a 150 MB log cap. SDK patch 28 logs files
that are not found at debug level: the same run then wrote 1.13 MB in its first 45 s and 2.6 MB in
all, so every run is back to the 20 MB cap.

**A run stuck on one bad pointer, and the capped runner (2026-10-09).** The first Mod-Pack run with
fixed-width device names froze its window before the unit index loaded: one guest thread repeated
"Unhandled guest access violation: read of guest 0x000000B4" thousands of times a second (the
audio went on). The game's log rotates (5 MB parts), so the flood pushed the start of the run, the
mods and the first fault included, out of the log; the cap counted only the files left, which the
rotation keeps near 100 MB, so it could not stop the run, and the stop pattern was looked for at a
position the rotated file no longer had. The runner is now the one the macOS work wrote,
`tools/run_capped/run_capped.py` (Linux, macOS, Windows; ctest `run_capped_test`), with these
additions:
- rotation followed: files are known by device and inode, so a renamed part keeps its read position
  and the bytes of parts rotated away still count against `--max-log-mb`;
- `--repeat-pattern`, `--max-repeats`: the same fault line (by default the SDK's guest access
  violation) seen more than 100 times stops the run (exit 123);
- `--head`, `--head-mb`: a copy of the first 2 MB of the logs' lines, so the start survives the
  rotation;
- `--stop-on`, `--stop-delay` from the old Linux script, read across rotations (exit 0);
- processes that leave the group (gdb runs the program in a group of its own) followed by parent
  and killed too; on Linux, the count of `/dev/shm/xenia_memory_*` files left.
The old `tools/run_capped.py` is gone. Its own pass of the fault is next: the same run under gdb,
stopped at the first access violation, for the stack.

The macOS work's branch (`feature/macos-game`, `7c59663`) is not in `develop` yet. To keep a single
script, that branch takes this branch's commits "tools/run_capped: follow rotation, stop at a
repeated fault, keep the start of the logs" and "tools/run_capped: an interrupted runner stops the
command first" (they apply on top of `7c59663`, which this branch cherry-picked unchanged), or merges `develop` after this branch lands; it should not change its
copy on its own meanwhile.

**A new-item mod, validated (2026-10-09).** With `tlunits:` mounted once the index exists, the game
loaded the merged index in the same start it was built (3377 units, the mod's sword among them)
and with the cached one. In guided runs: the save's item of the mod's unit loaded with the mod's
damage (180 DPS shown, `DAMAGE_PHYSICAL:777`; it keeps the name stored with it in the save, since
the game saves each item's name), the character saved with it, and two new swords made with the
game's developer commands (`ITEMGUID <guid>` and `ITEM <name>`, `--dev_guest_command`, development
builds only) showed the mod's name, "TL Test Sword", and the same damage.

## 7f. A body model without an entity: the wardrobe guard (2026-10-09)

With the Ultimate Torchlight Mod-Pack, the game froze at the title screen: one thread repeated
"Unhandled guest access violation: read of guest 0x000000B4" (the audio went on). Halving the pack
left one mod, `JCC - Main`, which faults on its own.

What faults (guest_abi/wardrobe.h). The title screen makes the Destroyer (`sub_823DE8F0`, the unit
made by name). Its load sets up the body model (`sub_822883F0`, `sub_822CFDE0`, `sub_8228B4B8`):
the mesh is `<RESOURCEDIRECTORY>/<MESHFILE>.mesh`, each read from the unit's definition with an
empty default. With the mod both came back empty, so the model asked for "/.mesh"; CGenericModel's
load (`sub_822BFF48`) could not make an entity and left +92 null. The wardrobe's first build
(`sub_822DC6B0`) then reads `*(*(wardrobe+12)+92)+180` without testing either pointer.

What is known about the cause. Not the mod's files against Xbox-only units: `JCC - Main` replaces
the 15 `base_{boots,gloves,helm,shoulders,chest}_{a,d,v}_unique.dat` without their `[WARDROBE]`
blocks, which leaves the Xbox-only `360_*` items (the title screen's outfit) inheriting the trimmed
files, but the mod without those 15 files still faulted, and no item is made between the Destroyer
and its empty mesh. Not a thread race, as far as the code shows: a unit's definition is loaded on
first use on the thread that asks (`sub_82196960`, +64). Not freed data from our index builder: the
nodes it loads share the data manager's cached groups, but are marked shared (+53, set by
`sub_82392C18`) and the group's destructor (`sub_82391950`) then leaves the properties alone. Not
the mods' compiled `.ADM`: no run wrote any. And the fault is not deterministic: the same 1366 of the
mod's files faulted in one run and not in the next, which also makes halving the files
meaningless. With the whole mod it faulted in every run (nine: batchA4 to A8, two halving steps,
two gdb runs). The cause was found later: section 7g.

The guard (src/mods/wardrobe_hooks.cpp, wardrobe_guard.h). When the build would take its first-build
path (no rebuild flag at +68, no wardrobe entity at +384, a node at +16) and the model or its entity
is null, the build returns without calling the game, as the game does itself when there is no node,
and the log says once per model: `mods: wardrobe not built: model ... has no entity (mesh "...")
while making unit "..."`. A later rebuild (+68) makes the wardrobe entity from +372, without the
model. The game's other reads of the wardrobe's entities (its release, `sub_822DC0F8`) test them
first; `sub_822D9778`, which reads +376/+384 without a test, is a unit's method (slot 61 of
CTriggerUnit's vtable), not the wardrobe's. It covers any model without an entity, from a mod or
not. A character without a body model may still fail elsewhere (an animation, for one); the
validation run with `JCC - Main` shows how far it goes.

## 7g. Our index build released names other definitions use (2026-10-09)

The empty mesh of 7f was ours. Property names are keys into one global table (guest_abi
unit_index.h, names). The title screen's Destroyer had the same 72 properties with and without
`JCC - Main`, but with it 44 of them had keys the table no longer held (40583..41050,
RESOURCEDIRECTORY and MESHFILE among them), so the game's lookups by name returned the empty
default. Measured with gdb at three moments: while our builder read the 1652 definitions of the
mod, the table kept about 17,300 names but its largest key went from 17,830 to 90,576 (names made
and released again); with the index from the cache, so no build in that session, the same mod gave
a Destroyer with every name. The builder read each definition with the game's own loading into a
node list and emptied it with kClearNodeList, as the game's build tool does. While it runs the
game's unit index is still empty, so the definitions are read from their files into nodes of their
own, and their destruction releases their names (kReleaseName). Which object went on using a
released key was not traced. It also explains why halving the mod's files gave no answer: what
breaks depends on which definitions the build reads and which names end up released.

The fix: the builder keeps the nodes. Nothing it reads is released, so every name it made stays.
The memory they hold stays with the game for the session; a diagnostics build
(`TORCHLIGHT_MODS_DIAGNOSTICS`) logs it after each build (`mods: build memory: ...`: the used
physical pages and virtual heaps, the name table's size, and the nodes, properties and subgroups
kept). The wardrobe guard of 7f stays as a net; the player notice planned for a class without a
model is dropped if the fix holds.

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
