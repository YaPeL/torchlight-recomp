#!/usr/bin/env python3
"""Convert a Torchlight PC shared stash (sharedstash.bin) into the Xbox 360 one for the recomp.

Usage:
  convert_stash.py SOURCE DESTINATION --pak PATH/pak.zip --pc-pak PATH/Pak.zip [--force]

The stash has the same layout on both platforms (version, item count, items; no trailer), so the
conversion is the character converter's item handling: every GUID and name is checked against the
360 pak.zip (quest GUIDs that changed are resolved by name through the PC Pak.zip) and the bytes are
written big-endian. sharedstash.bin and sharedstashh.bin (hardcore) are separate stashes: the
destination must have the same file name as the source. The destination is never replaced without
--force; the stashes are not merged (two stashes can want the same slot, and the game deletes an
item that does not fit).
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from convert import PC_PAK_MISSING, _write_new  # noqa: E402
from gamedata import NONE_GUIDS, GameData, check_references  # noqa: E402
from savefile import (SaveError, load_schema, pc_stash_to_360, read_360_stash,  # noqa: E402
                      read_pc_stash, signed64)

STASH_NAMES = ('sharedstash.bin', 'sharedstashh.bin')
AUTO_SLOTS = (-1, 999)  # the game places these in the first free slot (sub_822E29F8)


def slot_problems(parsed):
    """Items that would compete for one slot: the game keeps one and deletes the others."""
    by_slot = {}
    for index, item in enumerate(parsed.tree['items']):
        if signed64(item['unit_guid']) in NONE_GUIDS:
            continue  # the game discards it on load, on PC as on the 360
        slot = item['w2'] - (1 << 32) if item['w2'] >= 1 << 31 else item['w2']
        if slot in AUTO_SLOTS:
            continue
        by_slot.setdefault(slot, []).append(index)
    return ['slot %d holds %d items (%s)' % (slot, len(items), ', '.join('#%d' % i for i in items))
            for slot, items in sorted(by_slot.items()) if len(items) > 1]


def convert_stash(source_path, destination_path, pak_path, pc_pak_path, force=False, log=print):
    """Convert one stash file. Raises SaveError with a user message."""
    if not pc_pak_path:
        raise SaveError(PC_PAK_MISSING)
    source_name = os.path.basename(source_path).lower()
    destination_name = os.path.basename(destination_path).lower()
    if source_name in STASH_NAMES and destination_name != source_name:
        raise SaveError('%s and %s are different stashes (sharedstashh.bin is the hardcore one); '
                        'the destination must have the same name as the source'
                        % (os.path.basename(source_path), os.path.basename(destination_path)))
    if os.path.exists(destination_path) and os.path.samefile(source_path, destination_path):
        raise SaveError('the destination is the same file as the source')

    schema = load_schema()
    with open(source_path, 'rb') as f:
        data = f.read()
    parsed = read_pc_stash(schema, data)
    log('PC stash: version %d, %d items' % (parsed.version, len(parsed.tree['items'])))

    if os.path.exists(destination_path) and not force:
        detail = ''
        try:
            with open(destination_path, 'rb') as f:
                detail = ' (it holds %d items)' % len(read_360_stash(schema, f.read()).tree['items'])
        except (OSError, SaveError):
            pass
        raise SaveError('%s already exists%s; it is not replaced without --force, and stashes are '
                        'not merged' % (destination_path, detail))

    target = GameData.from_pak(pak_path)
    source = GameData.from_pak(pc_pak_path)
    replacements, problems = check_references(parsed, target, source)
    problems = [str(p) for p in problems] + slot_problems(parsed)
    if problems:
        lines = '\n'.join('  - %s' % p for p in problems)
        raise SaveError('the stash cannot be converted:\n%s' % lines)

    output = pc_stash_to_360(schema, data, replacements)
    check = read_360_stash(schema, output)  # must read back before it is written
    if len(check.tree['items']) != len(parsed.tree['items']):
        raise SaveError('internal error: the converted stash cannot be read back')

    _write_new(destination_path, output, force)
    for offset, guid in sorted(replacements.items()):
        log('quest GUID at offset %d replaced with %d' % (offset, guid))
    log('Wrote %s (%d bytes, %d items). The source was not modified.'
        % (destination_path, len(output), len(check.tree['items'])))
    return replacements


def main(argv=None):
    parser = argparse.ArgumentParser(description='Converts a Torchlight PC shared stash '
                                     '(sharedstash.bin) to the 360 format the recomp uses.')
    parser.add_argument('source', help='PC sharedstash.bin or sharedstashh.bin; only read')
    parser.add_argument('destination', help='stash to create, with the same file name')
    parser.add_argument('--pak', required=True, help='pak.zip of the 360 game (the recomp data)')
    parser.add_argument('--pc-pak', help='Pak.zip of the PC game (required; next to Torchlight.exe)')
    parser.add_argument('--force', action='store_true', help='replace the destination if it exists')
    args = parser.parse_args(argv)
    try:
        convert_stash(args.source, args.destination, args.pak, args.pc_pak, args.force)
    except (SaveError, OSError) as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
