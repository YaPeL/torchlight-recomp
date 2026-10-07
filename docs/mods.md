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
| File loader | `sub_8239D5F8` (`.ADM`, `.CMP`, `DDS`), `.ADM` save `sub_82396950`, "DataGroups can't load compressed files" | **Kept** |

So, as with GUIFEEDPET, the Xbox build keeps the end of the chain (the check, one descriptor reader,
the file loaders) and lost its entry (the folder scan). **[read]**

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

## 7. Before any code

1. Read the guest's `resources.cfg` reader: how `localization_zip` is searched first, and whether
   more locations can be added and in which order. This decides option A's mechanism.
2. Read how the guest indexes data folders (whether a new `.ADM` in a searched location is picked
   up, or only replacements).
3. Check whether the guest keeps a text `.DAT` parser (decides whether option C is needed).
4. Look at one real PC mod, on the user's machine only, to confirm the folder layout and whether it
   ships `.DAT` or `.ADM`.
