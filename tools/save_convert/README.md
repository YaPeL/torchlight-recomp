Converts a Torchlight PC character save (`N.SVT`, v1.15) to the 360 format (`N.TSV`) the recomp uses.

`python3 tools/save_convert/convert.py SOURCE.SVT DESTINATION.TSV --pak PATH/pak.zip --pc-pak PATH/Pak.zip [--force]`

- `--pak`: the `pak.zip` of the user's own 360 game (from the extracted XBLA package). It is used to
  check that every GUID and name in the save exists; if anything is missing the save is not
  converted and the missing references are listed.
- `--pc-pak`: the `Pak.zip` of Torchlight PC (next to `Torchlight.exe`). Required: the save keeps
  the state of each active quest's dialog lines according to the PC quest definitions, and a few
  quests have different dialogs on the 360, so both are compared to adapt the state (a state that
  cannot be placed without guessing stops the conversion). It also resolves, by name, quests whose
  GUID changed.
- The source is only read. The destination is not replaced if it exists, unless `--force` is given.

The shared stash and the settings have their own commands (same rules: the source is only read,
nothing is replaced without `--force`, and nothing is written unless it validates):

- `convert_stash.py SOURCE DESTINATION --pak PATH/pak.zip --pc-pak PATH/Pak.zip [--force]`:
  `sharedstash.bin` (or the hardcore `sharedstashh.bin`, to a destination of the same name). The
  items are checked like a character's, and two items in one slot are refused (the game would
  delete one). Stashes are not merged.
- `convert_settings.py PC_DIR RECOMP_DIR [--force]`: imports from the PC `settings.txt` /
  `local_settings.txt` only the preferences that mean the same on the 360 (volumes, mutes, tips,
  damage numbers, blood, camera shake, automap) and the "game completed once" flag. A recomp file
  that does not exist yet is created with those keys; in an existing one, preferences that differ
  are only replaced with `--force` (the flag only goes up).

Both formats were checked with real PC and 360 files.

`character_save.schema.json` describes the save (fields, sizes, version conditions, and the guest
reader function and address of every block); `savefile.py` reads it into a tree and writes a tree
back (parsing and writing an unchanged save gives the same bytes), and `gamedata.py` reads the paks'
`.adm` files and adapts the quest dialog state. Tests: `python3 -m unittest discover tests/save_convert` (synthetic saves and
paks).

## Finding where the game misreads a save

`align_reads.py LOG N.TSV [SECTION]` compares, read by read, how the game consumed a save with how
the schema parses it, and prints the first game read that does not land on schema field
boundaries. It needs a game log with the reads, which takes a temporary hook (not kept in the
repo) in the game executable:

- Hook `sub_823A2008(stream, dst, bytes, element_size, count)`, the stream read every save reader
  goes through. The stream keeps its position at +16 and its size at +24 (u64, big-endian). For
  each call log `save-trace: read lr=<ctx.lr as 8 hex digits> pos=<position before the read>
  size=<element_size>x<count> got=<returned count>`.
- Log only while a function that opens a save into a stream is running (the callers of
  `sub_823A22A8`: `sub_8221DC20` and `sub_82217DA8` load a character, `sub_82387580` is the
  "continue game" list), and mark it with `save-trace: sub_XXXXXXXX begin` / `... end`.
- Limit the logged short reads (a misread string length yields tens of thousands of one-character
  reads, and the log rotates away the part that matters).

Some readers read fields straight from the stream buffer without `sub_823A2008`; those reads are
not logged, so the first divergence the script reports can come a little after the real cause.
Look back from it to the last structure the game and the schema agreed on. With the
`RANDOMMINERS` quest of an imported save, for instance, the script stopped at the next quest's
name, but the cause was an extra pair of bytes in the quest before.
