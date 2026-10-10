# Playing with PC mods

Torchlight Recomp loads mods made for Torchlight on PC. This page tells you how to install and order
them and what to expect. The technical notes are in [mods.md](mods.md).

## Where mods go

Put each mod in its own folder inside the `mods` folder, next to your saves:

| | Linux | Windows |
|---|---|---|
| Mods | `~/.local/share/TorchlightRecomp/mods/` | `%USERPROFILE%\Saved Games\TorchlightRecomp\mods\` |

Copy each mod folder as it is on PC (under `Runic Games/Torchlight/mods/`), with its `media`
folder inside. A mod's `mod.dat` (its name and author) is optional, as on PC. Mods must be unpacked
folders; unpack any `.zip`, `.rar` or `.7z` first.

## Order and turning mods off

The order lives in `mods/mods.dat`, the same file PC writes, and you can copy yours from PC. Each mod
has a `PRIORITY`: when two mods change the same file, the one with the lower number wins. A negative
`PRIORITY` turns a mod off without removing it. New folders are added at the end the next time the
game starts, and entries whose folder is gone are dropped.

Changes take effect the next time the game starts.

## What works

- Changed game data: items, monsters, skills, spawn tables and other `.dat` files, including files
  saved as UTF-16 by PC tools.
- New items and monsters: the game's unit table is rebuilt with the mods' units. The first start
  with a new set of mods takes longer, up to about a minute with a large pack; later starts use a
  saved copy. New classes are added to the table too, but picking one when creating a character has
  not been tested yet.
- Replaced textures. Replaced models load the same way, but have been checked less.
- The PC achievements for mods (Played with a mod, 5 mods, 10 mods), in the PC achievement set.
- Large packs: the Ultimate Torchlight Mod-Pack (29 mods) loads.

## What happens to your saves

- **At every start, with or without mods**, the game checks your saves before it loads them. It
  looks for items and creatures whose unit the game does not know: a removed mod's, or a mod's
  items in a PC save you copied in by hand. Without that check, the game would drop them silently
  at the next save. Saves with nothing unknown are not touched, copied or rewritten, so without
  mods the check normally changes nothing. When it does find unknown units, it handles them as
  described below. It also leaves a save as it is when it cannot read the game's unit table, or
  when more than half of a save's units are unknown, because that does not look like a removed
  mod.
- **Every time the set of mods changes**, your saves are copied first, to `save-backups` in the
  saves folder.
- **Items from a mod you removed**: the game cannot keep an item it no longer knows, so those items
  are taken out of the saves, after the copy, and a notice at the character list tells you which
  characters lost what and where the copy is. Put the mod back and restore the copy to get them back.
- **When the game could not load some mods' items** that your saves carry, it saves nothing for the
  whole session, so those items are not lost, and the notice says so in capitals. Quit, fix the
  mods, and start again.

## Not supported yet

- New affixes (the extra properties of magic items) and new level pieces from mods: the game ships
  these tables prebuilt and they are not rebuilt yet.
- Mods that are compressed archives instead of folders.
- Choosing which mods to use from inside the game: edit `mods.dat` for now.

If something goes wrong, the log of the run tells what the game did with each mod
([Where files are kept](../README.md#where-files-are-kept)). Please include it when you report a
problem.
