# PC and Xbox 360 saves (TLR-009, research)

Question: where and in what format the PC/Steam version saves, how it differs from the 360 format
the recomp writes today, whether they are compatible and what migrating or reusing them would take.

Sources: a copy of `~/.local/share/torchlight/` made on 2026-10-04 into a temporary directory (the
original was not touched); `reference/pc` (Torchlight PC v1.15, `Torchlight.exe` SHA-256
`a783a607…`, read only); the XEX image decrypted with `tools/xex/xex_dump.py`; recompiled code in
`~/torchlight-rewrite/generated/default`; SDK in `~/rexglue-sdk` (`0c7b01a`); a real PC save provided
by the user (`~/0.SVT`, Vanquisher, 98 600 bytes; copied to a scratch directory, unmodified); the
Torchtools specification and code (see section 3).

## Summary

- **Same logical format, different encoding.** Both versions save one file per character (`N.SVT`
  on PC, `N.TSV` on 360), `backup.tmp`, `sharedstash.bin`, `settings.txt` and `local_settings.txt`.
  The header of a PC Vanquisher and of a 360 one parse with the same code and give the same fields at
  the same offsets; content GUIDs are the same values with their bytes reversed.
- **Three measured differences:**

  | | PC v1.15 (`0.SVT`) | 360 / recomp (`0.TSV`) |
  |---|---|---|
  | Byte order | little-endian, UTF-16LE text | big-endian, UTF-16BE text |
  | Version (first `u32`) | **23** | **25** |
  | End of file | `u32` with the total file length | SHA-256 (32 bytes) of the rest |

- **PC → recomp is possible**: the 360 loader (`sub_8221DC20`) does not reject old versions; it reads
  each field only if the file's version has it ("version ≥ N" comparisons with N = 2, 10, 12, 13, 14,
  16, 17, 18, 19, 23). A v23 `.SVT` turned big-endian, with the SHA-256 at the end instead of the
  length, loads; the game saves it again as v25. One more difference is in the game data, not the
  format: the state of each active quest's dialog lines follows the quest definitions, and two
  quests have different dialogs on the 360 (section 4b). The converter adapts them.
- **Recomp → PC is possible too** (corrected 2026-10-07): the PC reader knows up to version 23, but
  versions 24 and 25 add no field. The 360 reader's highest comparison is "≥ 23", so what the 360
  writes as v25 is exactly what it reads, the v23 layout. Written little-endian as version 23 with
  the length trailer, and with the quest dialog states fitted to the PC definitions, a recomp save
  is a PC one (`tools/save_convert/convert_to_pc.py`).
- **The converter needs the size of every field** to reverse bytes, and the 360 reader itself gives
  it: it reads with primitives that take the element size (`sub_823A2450`: 1, 2, 4 or 8 bytes in 161
  of 178 calls). The exceptions are few and localized (12 and 64 bytes, the hash and some
  variable-size reads).
- **Risk with the dialogs in only mode** (from TLR-010): today in only mode the save dialogs are
  answered "No" automatically. No save is deleted or overwritten, but the game may go on with saving
  disabled without warning. See section 5. A converted `.TSV` with a wrongly computed hash would
  trigger "Corrupt/Damaged Save".

## 1. Where each version saves

### PC (Steam, v1.15)

Strings in `Torchlight.exe` (UTF-16LE unless noted):

- Windows Games manifest: `<SavedGames baseKnownFolderID="{3eb685db-65f9-4df6-a03a-e3ef65729f3d}"
  path="Runic Games\Torchlight\save" />`. That GUID is `FOLDERID_RoamingAppData`, so saves go to
  `%APPDATA%\Runic Games\Torchlight\save\`. The GOG forum confirms it
  (<https://www.gog.com/forum/torchlight_series/torchlight_1_save_not_up_to_date>): `0.SVT` is the
  first character, `backup.tmp` the recovery copy and `sharedstash.bin` the shared stash;
  characters are `0.SVT`, `1.SVT`, … and can be moved between installations by renaming them.
- Files: `.svt` / `*.svt`, `backup.tmp`, `save.tmp`, `sharedstash.bin`, `local_settings.txt`,
  `saveBackup/backup/`, `saveBackup/tmpBackup/`. The save routine that builds `save.tmp` and
  `backup.tmp` is at `0x417110` (references at `0x417207` and `0x41725e`).
- Steam Cloud: `save/saves.cmp`, `save/sharedstash.cmp`, `SavesBeforeCloud/` and messages such as
  "Your local saved files are newer than the ones downloaded from the SteamCloud". Saves travel to
  the cloud packed (`.cmp`) through `SteamRemoteStorage` (`steam_api.dll`). The `.cmp` format was not
  analyzed.
- Mods: `Runic Games\Torchlight\Mods`.
- On Linux with Proton: the same tree inside the Wine prefix
  (`steamapps/compatdata/41500/pfx/drive_c/users/steamuser/AppData/Roaming/Runic Games/Torchlight/`;
  41500 is the app id according to `reference/pc/41500_install.vdf`). Not verified on a real
  installation.

### Recomp (360 format)

The SDK mounts the game's `SAVE` container (title ID `0x58410A7E`) at:

```
<user_data_root>/<XUID>/58410A7E/00000001/torchlight.sav/   contents
<user_data_root>/<XUID>/58410A7E/Headers/00000001/torchlight.sav.header   XCONTENT header
```

- Default `user_data_root`: `GetUserFolder() / "torchlight"` (`src/ui/rex_app.cpp:114-121`).
  - Linux / Steam Deck: `$XDG_DATA_HOME/torchlight`, or `~/.local/share/torchlight`
    (`src/core/filesystem_posix.cpp:106-125`).
  - Windows: `Documents\torchlight` (`FOLDERID_Documents`, `src/core/filesystem_win.cpp:55-63`).
  - macOS: uses the POSIX implementation, i.e. `~/.local/share/torchlight`, which is not the usual
    macOS location (`~/Library/Application Support`).
  - It can be changed with the existing `user_data_root` cvar (`src/system/runtime.cpp:32`): it is
    config, no new environment variable is needed.
- XUID: `B13EBABEBABEBABE`, the SDK's default profile (`src/system/xam/user_profile.cpp:28`).
- `torchlight.sav.header` (328 bytes): content header with the display name "Torchlight" in
  UTF-16BE.
- `B13EBABEBABEBABE.zip` at the root is not created by the SDK (there are no references to `.zip` in
  its code). It only holds settings and an empty stash from September; it looks like a manual
  backup.

Current contents (copy):

| File | Size | What it is |
|---|---|---|
| `0.TSV` | 287 171 | "Vanquisher" character |
| `1.TSV` | 204 632 | "Alchemist" character |
| `1_TSV` | 126 046 | Older copy of a Vanquisher (09/13); probably the save temporary (`save.tmp` on PC) left behind |
| `backup.tmp` | 203 981 | Recovery copy (Alchemist) |
| `sharedstash.bin` | 8 | Empty shared stash: `00000019 00000000` (version 25, 0 items), no hash |
| `settings.txt` | 1 716 | Options (keys, automap, blood…): bytes `FF FE`, then UTF-16BE text |
| `local_settings.txt` | 3 402 | Local options (resolution, shadows, FPS…): bytes `FF FE`, then UTF-16BE text |

## 2. 360 format

Measured on the four character files:

- Byte 0: big-endian `u32` = **25** (`0x19`), the format version. The stash starts the same way.
- Byte 4: a BE `u16` with the length followed by the UTF-16BE text: the class ("Vanquisher",
  "Alchemist"). Further on, with the same scheme, "Player" and the item names ("Health Potion",
  "HPBONUS", "HEALTH_POTION", "Town Portal Scroll"…).
- There are big-endian floats (e.g. `3F800000` = 1.0 at byte 0x53 of `0.TSV`) and 64-bit
  identifiers (byte 0x24 of `0.TSV`: `00AA472CC2629611`).
- No compression (entropy ~3.4-3.9 bits per byte; names are readable at a glance).
- **The last 32 bytes are the SHA-256 of the rest of the file**: it matches in `0.TSV`, `1.TSV`,
  `1_TSV` and `backup.tmp`. The guest imports `XeCryptSha256Init/Update/Final`. It is what triggers
  "Corrupt/Damaged Save" on load (see section 3, "How the 360 reads").

The `.txt` files start with the bytes `FF FE` and then hold UTF-16 **big-endian** text on the 360
(an earlier reading of them as little-endian was wrong), one key per line (`KEY :value`). The keys
are the PC ones (`OPENGL`, `RES_WIDTH`, `MAX_FPS`,
`SHADOWS_DETAIL`, `STEAMCLOUD`, …). `local_settings.txt` also has `ZIP : game:\pak.zip`, a
console-specific path.

## 3. PC format and differences

### Existing documentation

- **Torchtools** (Java, LGPL, 2010; the `torchtools` Google Code archive, code at
  <https://storage.googleapis.com/google-code-archive-source/v2/code.google.com/torchtools/source-archive.zip>):
  includes `wiki/TorchlightFileSpec.wiki`, "an attempt to detail and organize the file format
  Torchlight uses for player saves (.svt) and shared stashes (.bin)". It is a partial quick
  reference: relative offsets of individual fields (class, difficulty, hardcore, retired, "cheater",
  name, level, XP, fame, HP, MP, unspent points, gold, pet) and the general rules: little-endian
  integers, UTF-16LE texts with a `short` length in characters, 8-byte GUIDs, and "the last 4 bytes of
  the file is an int which describes the file length". It does not describe the full structure
  (items, quests, maps). The code (`torchtools/binary/TLPlayerSave.java`, `TLSave.java`) follows the
  same offset approach.
- **SVTInfo**: a Python API derived from Torchtools, with a command line and a minimal GUI (mentioned
  in the Runic forums, <http://forums.runicgames.com/discussion/9899/>). The code could not be found.
- **C# viewer** by Reelix (<https://gist.github.com/Reelix/7b1a837386dba0f9fe9a58c79b2c8591>, 2019):
  implements the same quick reference and cites the Torchtools wiki.

None of them gives the full schema; the guest code does (section 4).

### Measured on the PC save

- Byte 0: LE `u32` = 23. Byte 4: LE `u16` = 10 and "Vanquisher" in UTF-16LE.
- Following the first reads of `sub_8221DC20` (version `u32`, `u16` length, class, `u32`, `u8`, and
  if version ≥ 13 another `u32` and another `u8`), both files give `1, 0, 0, 0` and end at byte 36.
  The unit (`sub_822A7D78`) starts there with the same GUID: `00 DE1196 62C22C47AA` on PC and
  `00 AA472CC2629611DE` on 360 (`0xAA472CC2629611DE` in both).
- Last 4 bytes: `0x00018128` = 98 600 = the file length. No hash (and the PC executable has no
  SHA-256, SHA-1 or MD5 constants, and imports no CryptoAPI).
- The configuration `.txt` files: same format and same keys (see section 2); they can be reused
  almost as they are, except for platform keys (`ZIP : game:\pak.zip`, `OPENGL`, `STEAMCLOUD`).

### How the 360 reads

- An in-memory file stream (`sub_823A2008` reads like `fread(dst, size, count)`, `sub_823A20E8`
  writes). It copies raw bytes: **it reverses nothing**, so the 360 reads native big-endian. A PC
  `.SVT` as is would be misread from the first field.
- Primitives: `sub_823A2450(dst, size, count, stream)` for values and `sub_823A1A98(stream, count)`
  for UTF-16 text (it reads one `u16` at a time).
- Character load (`sub_8221DC20`): seeks to `length - 32`, reads 32 bytes and compares them with the
  SHA-256 of the rest (`sub_823A2558` → `XeCryptSha256*`). If they do not match, it exits through
  `loc_8221EDE4` (damaged save). Then it goes back to the start, reads the version into `r15` and
  passes it to the readers of each part (`sub_822A7D78` unit, `sub_822CA9A0` items, etc.), which
  compare it field by field. There is no minimum or maximum version check.
- Save (`sub_8221CC70`): writes everything with version 25 and appends the SHA-256 with
  `sub_823A2610`. It does not write the length at the end.
- No reader compares the version with 24 or 25 (the highest comparison is "≥ 23", in the item
  reader): on the 360 a v23 and a v25 save are read with the same layout.
- Every read that comes back short (fewer bytes than asked, past the end of the stream) raises the
  global `0x8355A268`; the storage state machine `sub_82195AA8` then shows "Corrupt/Damaged Save",
  whose "Yes" deletes the whole save container (`XamContentDelete`). The character may load anyway.
- Active quests (`sub_823D0A08`, `sub_823CB2B0`): for each quest the game knows, it reads four lists
  of two-byte states, one per section of the quest's `DIALOG` block (`INTRO`, `RETURN`, `COMPLETE`,
  `PASSIVE`; quest `+204`, `+220`, `+236`, `+252`, built by `sub_823C7090`). It reads only as many
  states as the definition has lines and does not skip the extra ones, so a save written with a
  different definition is misread from there on. A quest it does not know is skipped through its
  end offset.

Are they compatible? **Not directly** (byte order and end of file), but **convertible from PC to
360** without inventing data: keeping version 23 is enough.

## 4. PC → recomp migration

### Offline converter (done: `tools/save_convert/`)

`convert.py SOURCE.SVT DESTINATION.TSV --pak pak.zip --pc-pak Pak.zip [--force]`:

1. Checks the `.SVT` length trailer and the version (only 23, the PC v1.15 one).
2. Parses the body with `character_save.schema.json`, which has to consume every byte.
3. Checks against the user's 360 `pak.zip` every `UNIT_GUID`, `UNIQUE_GUID` and `QUEST_GUID`, the
   effect names and their source (skill, affix or unit effect) and the quest names. If anything is
   missing, it does not convert and lists what is missing (for example, items from mods). `-1` and
   `0` mean "no GUID"; in items without a `unit_guid` (gold piles) the rest of the item is
   uninitialized memory the game saves as is, and it is not checked.
4. Adapts the dialog state of every active quest whose dialog differs between the PC and the 360
   definitions (both paks are needed, so `--pc-pak` is required). Each 360 line takes the state of
   the PC line with the same speaker (`UNITNAME`) and text; a 360 line with no such line starts as
   new (`0, 0`, the value of a new line). If a state cannot be placed without guessing (a PC line
   with state that has no counterpart, or an ambiguous match), it does not convert and says which
   quest and section.
5. Writes the tree back big-endian (`savefile.write_body`, which recomputes the end offsets of the
   quest blocks), **keeps version 23** and replaces the length trailer with the SHA-256.
6. Reads the result back as a 360 save, checks that every active quest has as many dialog states as
   the 360 definition, and only then writes it. It never touches the source and never overwrites the
   destination without `--force`.

The schema was derived from the guest reader and verified with real data (next section). What did
not come out of the visible reads:

- Sizes that depend on the version inside a branch (`unit` `0x822A7EE0`: `u64` from version 8,
  `u32` before; `item` `0x822CAAA0`: `u32` in version 16, `u64` from 17).
- A `u32` that the reader **skips** without reading, inside every effect (`0x822CB7F0` in items,
  `0x822A94DC` in units); the writer `sub_822C9AB8` does write it.
- The 12- and 64-byte reads are floats (position and matrix); the grid of each level too (explored
  map: 1.0, 0.6, 0).
- The first variable-size read of `sub_822FC2F8` uses the dimensions of the freshly created object
  (0): it takes no bytes.

The shared stash and the settings have their own tools (section 4c).

### Validation

- With real data (local, not committed): the schema consumes exactly the 98 596 body bytes of the PC
  `0.SVT` (v23) and the four recomp `.TSV` files (v25: 287 139, 204 600, 126 014 and 203 949); the
  round trip `.SVT` → `.TSV` → `.SVT` gives the original file byte for byte; all five saves pass the
  reference check against the 360 `pak.zip` (the 360's own saves serve as a control that the check
  gives no false errors).
- In the repo: `python3 -m unittest discover tests/save_convert`, with synthetic saves and paks
  generated from the same schema (every struct, round trip, errors, quest GUID replacement, existing
  destination, untouched source).
- The writer gives back every save byte for byte: 34 native v25 saves and the PC `0.SVT` (also
  big-endian through the old byte swap, which it replaces).
- The assumption that the game loads every dialog line of the definition was checked with the
  native saves: in all 69 active quests they contain, the number of states the 360 wrote equals the
  number of lines in the 360 `pak.zip`.
- In the game: the first conversion (without the dialog adaptation) loaded, but raised "Corrupt/Damaged
  Save" right after loading. A temporary trace of the reads (`tools/save_convert/align_reads.py`)
  showed that the game read the quest `RANDOMMINERS` two bytes shorter than the file (4 `PASSIVE`
  lines on PC, 3 on the 360).
  With the adaptation the character loads without the dialog; the game saves it again as v25
  (read exactly by the schema), and it loads again after that.

### Alternative: convert while reading

Instead of a converter, hook `sub_823A2450`/`sub_823A1A98` to reverse by size when the source file is
a PC one, and skip the hash check. It avoids writing the schema, but it puts format logic in the
game's read path and the exceptions in the table still have to be solved. Not recommended.

### Recomp → PC

Not without loss: it would mean going down to v23 dropping what 24 and 25 add (not identified), and
the PC reader has no way to skip it. Out of scope unless asked.

### Default location (part of TLR-009, not researched in depth)

- Today the SDK already provides a per-platform path and the `user_data_root` override. It remains to
  decide whether the recomp uses the SDK path or its own (e.g. on Windows
  `%APPDATA%\Runic Games\Torchlight Recomp` instead of Documents, and on macOS
  `~/Library/Application Support`). Changing the macOS or Windows default means touching the SDK or
  resolving the path from the app with `OnConfigurePaths` (`include/rex/rex_app.h:115`), a public
  hook.
- Detecting PC saves: look in `%APPDATA%\Runic Games\Torchlight\save\` (Windows) and in the Proton
  prefix of app 41500 (Linux, Steam Deck). Read only; the converter writes to a new file and never
  replaces an existing `N.TSV` without confirmation (TLR-009 criterion).

## 4b. Game data: PC versus 360

The compiled content (`.adm`) of `Pak.zip` (PC v1.15) and `pak.zip` (360) was compared. The `.adm`
files have the same format in both (little-endian on the 360 too): a `u32` version, a string table
(`u32` id, `u32` length, UTF-16LE) and a node tree (`u32` name, properties `u32` key + `u32` type +
value, children). Types and sizes: 1 int32, 2 float, 3 double, 4 uint32, 5 string (id), 6 bool (4
bytes), 7 int64, 8 translatable string (id). With that, the 6181 PC `.adm` files and the 6203 360
ones are read completely, with no leftover bytes (`tools/save_convert/gamedata.py`).

| Identifier | PC | 360 | PC only | 360 only |
|---|---|---|---|---|
| `UNIT_GUID` (items, monsters, players, pets, props) | 3466 | 3489 | 0 | 23 (20 items, 3 monsters) |
| `UNIQUE_GUID` | 558 | 558 | 0 | 0 |
| `PERK_GUID` | 5 | 5 | 0 | 0 |
| `GUID` (unit themes, etc.) | 35 | 36 | 0 | 1 (`unitthemes/poisonmonster`) |
| `QUEST_GUID` | 111 | 112 | **1** | 2 |
| Skills (by `NAME`) | 407 | 407 | 0 | 0 |
| Affixes (by `NAME`) | 1079 | 1081 | 0 | 2 |

- The PC skills in `media/skills/warrior/` exist on the 360 with the same names (in another
  folder). The save stores skills and affixes by name (`HPBONUS`, `HEALTH_POTION`…).
- **The only PC reference that does not exist on the 360**: the quest `IntroToGamePT1` ("Torchlight
  Under Attack", `media/quests/storyquest/intro/introtogamept1.dat`) changed its GUID:
  `-3544491302002617890` on PC, `-3967305250345119265` on 360. The rest of the file is the same.
- How the save stores quests (confirmed while writing the schema): active quests and progress flags
  go **by name** (`sub_823D0A08`), and the reader skips a quest it does not know. The `QUEST_GUID`
  only appears in **quest items** (`item/q4`) and in `unit/q7`: an item stores the GUID of the quest
  it belongs to. A PC save with an item of the first quest (used in the first minutes) would have the
  old GUID.
- **What would happen**: the item would stay tied to a quest the 360 does not know. The effect in the
  game was not analyzed. To avoid it, the converter replaces the GUID with the one of the quest with
  the same name on the 360. There is no table in the repo: the new GUID comes from the user's
  `pak.zip` and the old one from the PC `Pak.zip` (`--pc-pak`, required for the dialog adaptation
  as well).
- **Quest dialogs**: of the 111 quests in both games, 2 have a `DIALOG` block with a different
  number of lines; in every other one all four sections have the same lines (speaker and text) in the
  same order. `RANDOMMINERS` (`PASSIVE`: 4 on PC, 3 on 360; the 360 dropped one line and gave one
  speaker another line's text, so which line was dropped depends on whether lines are matched by
  speaker or by text) and `RANDOMPEOPLE` (`PASSIVE`: 9 on PC, 10 on 360, one line added in the second
  place). This is what the converter adapts (section 4, step 4).
- **Same GUID, different values**: 266 definitions (206 items, 48 monsters, 9 props, 3 players).
  Almost all of it is text or UI (`DESCRIPTION` 204, `COLUMN`/`ROW` 60, `DISPLAYNAME` 4) and
  `DESTROY_ON_DEATH` 36. There are few balance changes: `LEVEL_REQUIRED` in 7 units, damage in 1
  (`axe_dwarfgeneral`: 50 ice + 50 physical on PC, 100 physical on 360), `RARITY`, `VALUE`,
  `RUNNINGSPEED`, `MAXIMUM_PET_INSTANCES` in 1-2 each. A converted save uses the 360 definitions:
  those items look and behave as in the console version. The values of each item instance (rolled
  affixes, enchantments) travel in the save and do not change.

## 4c. Shared stash and settings

### Shared stash (`convert_stash.py`)

- Same layout on both platforms, each in its own byte order, with **no trailer** (no length, no
  hash): `u32` version, `u32` item count, the items. 360: read by `sub_82326360`, written by
  `sub_82326650`. PC (`Torchlight.exe` v1.15): read at `0x52A5D0` and written at `0x52A8C0` with
  `_wfopen_s` (`rb` / `wb`), `fwrite` of the version (a global), the count and the items, then
  `fclose`. Verified with a real PC `SHAREDSTASH.BIN` (version 23, 8 items, slots 19 to 26): the
  schema reads it exactly and writes it back byte for byte, and it converts with every reference
  found in the 360 `pak.zip`.
- The items use the character save's item reader and writer (`sub_822CA9A0` / `sub_822C9AB8`)
  with the stash's version, so the converter's `item` schema applies as is (and version 23 items
  are already proven in the game with a converted character). Every list in an item carries its
  own count; no loop is bounded by the game data, unlike the quest dialogs.
- Two stashes on both platforms: `sharedstash.bin` and `sharedstashh.bin` (hardcore characters).
  Both games create the stash container with 42 slots. An item goes back to the slot in its `w2`
  field (`+136`); `-1` and `999` mean "first free slot", and an item that does not fit is **deleted
  without notice** (`sub_822E29F8` fails and the loader destroys it). The converter refuses two
  items in one slot.
- `save/sharedstash.cmp`, `settings.cmp` and `save/saves.cmp` on PC belong to Steam Cloud; nothing
  to import.
- The converter checks every reference as for a character, keeps version 23, never merges two
  stashes, never replaces an existing stash without `--force`, and requires the destination to
  have the source's file name.
- Checked as well with a real 360 stash full of items (42 items, weapons, armour, a ring, scrolls, a
  potion): the schema reads it exactly and writes it back byte for byte, in both byte orders. The
  game used slots 19 to 60 (42 consecutive slots). Turned into a PC-style file (little-endian,
  version 23) and converted with the real paks, every reference passes and the result equals the
  original except for the version word.
- In the game, with only files converted from a real PC install (two characters, the shared stash
  and settings created from scratch with the imported keys only): the stash shows its 8 items, the
  character loads where it was saved (town) with the item left on its pet on PC, the automap
  starts off as on PC, and no "Corrupt/Damaged Save" appears. The game saves the character and the
  stash again as v25 (the stash items get new instance numbers in `q0`, and two weapons a different
  `w5`, presumably recomputed from the 360 data); it reads the partial settings files with its
  defaults for the missing keys.

### Settings (`convert_settings.py`)

- `settings.txt` and `local_settings.txt`: one `KEY :value` per line. The 360 writes `FF FE`, then
  the text big-endian with `\n` line ends. The PC writes `FF FE`, then the text little-endian with
  `\r\n` line ends, and names the first file `SETTINGS.TXT` (verified with a real PC install; both
  PC files are read and written back byte for byte). The tool accepts either byte order only when
  the text decodes cleanly, and finds the files whatever their case.
- The two games do not keep every key in the same file: the PC has `SHOW TIPS`,
  `GAME_COMPLETED_ONCE` and `RETIREE_QUEST` in `settings.txt`, the 360 in `local_settings.txt`
  (which also has 7 keys the PC lacks: `BRIGHTNESS`, `CONTRAST` and Live / promotion flags). A key
  is looked up in both PC files and written to the 360 file that holds it.
- The 360 reader (`sub_823DEF18`) registers each key with a default (`sub_82398428`): a key
  missing from the file takes the game's default. `RETIREE_QUEST :COMPLETEDONCE` is one of those
  defaults, not progress.
- Imported: `SOUND VOLUME`, `MUSIC VOLUME`, `SOUND MUTE`, `MUSIC MUTE`, `SHOW TIPS`,
  `FLOATY_NUMBERS`, `AUTOMAP`, `SHOW BLOOD`, `NO CAMERA SHAKE`, `AUTOMAP ZOOM`, and the progress
  flag `GAME_COMPLETED_ONCE` (only upwards). Not imported: video and renderer options (the
  recomp's `settings.toml` and the 360 game handle them), `BRIGHTNESS` / `CONTRAST` (the 360
  game's gamma ramp has its own options), measurements, debug and console switches, Steam options,
  paths (`ZIP`, `RESOURCES PATH`…) and keyboard bindings.
- Nothing set in the recomp is overwritten: a missing recomp file is created with the imported
  keys only; in an existing one the preferences that differ are listed and only changed with
  `--force`, and the progress flag is raised if the PC one is higher. Untouched lines are written
  back byte for byte (checked with the recomp's own files).

## 4d. Import inside the game

The import runs in the recomp itself, from the "load character" menu, with the converter ported to
C++. The save, settings and cache locations do not change; the only new location is an import
folder.

### Import folder

The `import/` folder of the game's files, the ones the first start installs from the user's package
(`platform/user_folders.h` `kGameData`): `~/.local/share/TorchlightRecomp/game/import/` on Linux,
`%LOCALAPPDATA%\TorchlightRecomp\game\import\` on Windows. It is the user's own writable copy of
the game (the executable's folder may not be: Program Files, a macOS `.app`, the Steam Deck's
read-only system); a previous installation moved aside on the first start takes its `import/`
along. The saves are checked against the `pak.zip` of the game data in use. The user copies there, from the PC installation, the
`N.SVT` characters, `sharedstash.bin` / `sharedstashh.bin`, `settings.txt` / `local_settings.txt`
and the PC `Pak.zip` (next to `Torchlight.exe`). The folder is the recomp's: imported files are
renamed there (`.bkp`), never in the PC installation.

### When: characters from the menu, stash and settings at the next start

The game keeps the stash and the settings in memory and writes them back, so they cannot be
replaced while it runs:

- **Stash**: read inside the character load (`sub_8221DC20` → `sub_82326360`, also through
  `sub_82326228` when the player changes) and written inside **every** character save
  (`sub_8221CC70` → `sub_82326650`). The loader does not read the file again if the stash already
  holds items (`sub_82326360` returns early when its container exists and is not empty), so after
  a first character load the session keeps its stash, and the next save writes it over an
  imported one.
- **Settings**: created and read once, in the game's initialisation (`sub_823DD248` →
  `sub_823DD110` → `sub_823DEF18`); written back with all their keys by the registry's virtual save
  (`sub_82397C28`, slot 1 of `0x820D6FE0`, both files through `sub_823976E0`).
- **Characters**: each `N.TSV` is only read when the list is built (`sub_82387580`) or the
  character is loaded, so a new file written while the menu is open is safe.

So the import is in two steps:

1. **In the "load character" menu**. `CContinueGameMenu` (vtable `0x820D21D8`): its `SetOpen`
   (slot 7, `0x823874D8`) builds the list (`sub_82387580`) and refreshes it (`sub_82389D30`); its
   `Update(dt)` (slot 3, `0x8238A138`) runs every frame (slots of the `CDropdownMenu` base,
   `guest_abi/game_ui.h`). On opening, if `import/` holds anything new, a host thread reads the
   paks and converts in memory (about a second), and then, from `Update`, a XAM message box
   (`XamShowMessageBoxUI`, import thunk `0x8302675C`, drawn by the native backend in only mode and
   driven with the gamepad) shows what is imported now, what at the next start, and what cannot be
   (with the reason). It does not block the guest thread: its texts, buttons, result and overlapped
   live on the game's heap (`sub_821CD7F8` / `sub_821CD9A8`) and the overlapped is polled from
   `Update` (a synchronous wait would stop the frames that draw the box). Three buttons, "Not now"
   active: **Yes**, **Not now** (asked again in the next session) and **Do not ask again** (the
   files become `.skipped`, never deleted). Texts in the game's language, from
   `data/ui/tl_import_strings.txt`; if the list does not fit, counts in the box and the detail in
   `import/import.log`.

   On **Yes**: the characters are written through the game's own mount of the container
   (`sub_823AC618` / `sub_823AC7B8`, files `SAVE:\N.TSV` through the VFS), numbered like a new
   character (`sub_8238EA88`: the lowest N ≥ 0 not taken by an `N.tsv`, N read like `wcstol`), the
   list is refreshed as `SetOpen` does, and each `.SVT` becomes `.svt.bkp` (or `.svt.rejected`).
   The converted stash and the settings decision go to `import/pending/`, with the container's host
   path (resolved while it is mounted, so the next start applies them where they were confirmed)
   and the number of items the recomp stash had when confirmed.
2. **At the next start, before the guest runs** (`OnPreLaunchModule`): `ApplyPending` replaces the
   stash only if the recomp one still has the confirmed number of items (keeping the old one as
   `import/sharedstash.bin.recomp-bkp`), writes the settings (same rules as the tool, never
   overwriting a preference set in the recomp) and renames the PC files to `.bkp`. Whatever cannot
   be applied leaves a notice (the stash changed, the container is gone, a file is unreadable): the
   menu explains it and asks again.

### Converter in C++

A module without OGRE, platform or guest types (`src/save_import/`), a port of
`tools/save_convert/`:

- **Reused as is**: `character_save.schema.json`, embedded in the executable at build time, so the
  Python tool and the game read the same file; the logic already validated in the game (generic
  reader/writer with end offsets, `.adm` parsing, the reference checks, the quest dialog
  adaptation, stash, settings).
- **Written**: the schema interpreter, a small reader for the JSON subset the schema uses, the zip
  reader (central directory + inflate with **miniz**, at a fixed version with the download's hash),
  **SHA-256** (copied from the one the SDK uses, Stephan Brumme's, zlib licence, with its
  attribution), and the file operations (exclusive create, atomic replace, rename).
- **No index of the paks**: reading the GUIDs, names and quest dialogs takes about 0.2 s per pak
  in C++ (0.6 s in Python) (only some 4000 small `.adm` are inflated; the PC `Pak.zip`'s 318 MB are mostly other
  assets), so a cache would add invalidation for under a second.
- **Tests**: C++ unit tests, plus synthetic fixtures generated with `synthetic.py` (no game data)
  that the C++ port must convert to the same bytes as the Python tool.

### Conflicts

| Case | Behaviour |
|---|---|
| Character number taken | Never overwritten: the character gets the number the game would give a new one, shown in the box |
| Character that cannot be converted (missing GUID, e.g. a mod; ambiguous dialog state) | Not imported; the box says why, the file becomes `.svt.rejected` and the detail goes to `import/import.log` |
| "Do not ask again" | Every file of the plan becomes `.skipped` (kept, never deleted) |
| PC stash, recomp stash empty | Imported (at the next start) |
| PC stash, recomp stash with items | Never merged: a second box asks whether to replace the recomp stash (N items) with the PC one, "No" by default; on "Yes" the old one is kept as `import/sharedstash.bin.recomp-bkp`. The same for the hardcore stash |
| Settings | As the tool without `--force`: created if missing, `GAME_COMPLETED_ONCE` raised, preferences set in the recomp never overwritten |
| Without the PC `Pak.zip` | Nothing is imported or renamed; the box says what it is for and where it is |

Every file is written atomically and renamed to `.bkp` only after the result reads back, so an
error leaves nothing half done.

### Validation

- Offline: the C++ port gives the same bytes as the Python tool on synthetic fixtures (checked in
  `src/save_import/*_test.cpp`) and on the real saves, stashes, settings and paks (not committed);
  the import flow (numbers, rejections, the stash question, the pending part, a stash or container
  that changed before the start) has its own tests with temporary folders.
- In the game (2026-10-05), with a test `import/` (two PC characters, a damaged `.SVT`, the PC stash
  and settings, `Pak.zip`) and a copy of the saves with a 42-item stash and no settings: the box
  listed the two characters (slots 13 and 14), the stash and settings for the next start and the
  damaged file; "Yes" and then "Replace" wrote `13.TSV` and `14.TSV` (the list showed them; butita
  loaded and was saved again as v25), renamed the files (`.bkp`, `.rejected`) and left the stash and
  settings in `pending/`. At the next start the stash was replaced (the old one kept as
  `sharedstash.bin.recomp-bkp`; the game showed the 8 PC items and saved them again), the settings
  were created with the imported keys (the automap started off, as on PC), and the menu did not ask
  again.

## 5. Risk: save dialogs in only mode

Detailed in `docs/text-input-research.md` (section 3). In short:

- In only mode there is no `ImGuiDrawer`, and the SDK answers every `XamShowMessageBoxUI` with the
  active button without showing anything.
- "Existing Save" (overwrite), "Corrupt/Damaged Save" (delete) and "Storage Device" (continue without
  saving) have the buttons `["No", "Yes"]` with the active one at 0: the automatic answer is "No".
  `XamContentDelete` is only called with "Yes". **There is no risk of deleting or overwriting a
  save.**
- **Medium risk**: with "No", the game stays with saving disabled or without a device, and the user
  sees no warning. They may play and lose the session's progress. With the SDK's headless device
  selector (always device 1), "Existing Save" should not show up in normal use; "Corrupt/Damaged
  Save" could show up if a `.TSV` fails the check (for example, one edited or badly converted:
  relevant for the converter above).
- The TLR-010 design (detached ImGui in only mode) solves it: the dialogs are shown and answered.
  Until then, conversions should be validated and important saves tested with Xenos.

## Still open

1. Which fields versions 24 and 25 add (only matters for recomp → PC).
2. Pending validation in the game: the hardcore stash (`sharedstashh.bin`) and the "Do not ask
   again" button (files renamed `.skipped`) are covered only by the tests
   (`src/save_import/import_plan_test.cpp`, `import_message_test.cpp`). To check with a test
   `import/` folder and `--user_data_root` on a copy of the saves.
