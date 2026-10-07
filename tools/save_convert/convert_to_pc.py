#!/usr/bin/env python3
"""Convert an Xbox 360 / recomp character save (N.TSV) into a Torchlight PC v1.15 one (N.SVT).

Usage:
  convert_to_pc.py SOURCE.TSV DESTINATION.SVT --pak PATH/pak.zip --pc-pak PATH/Pak.zip [--force]

The way back of convert.py, for a PC copy of a recomp character (for example to compare both
games on the same saved floor). The 360 reader has no field that depends on versions 24 or 25, so
a v25 body has the PC v1.15 (v23) layout: the save is written little-endian as version 23, with the
length trailer. Every GUID and name is checked against the PC game data (--pc-pak), quests whose
GUID differs are mapped by name through the 360 data (--pak), and the dialog state of active
quests is fitted to the PC definitions. The source is only read; the destination is created
exclusively (or swapped in atomically with --force) and never replaced unless --force is given.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from convert import _write_new  # noqa: E402
from gamedata import GameData, adapt_quest_dialogs, check_references  # noqa: E402
from savefile import SaveError, load_schema, parse_body, read_pc, split_360, summary, x360_to_pc  # noqa: E402


def convert_to_pc(source_path, destination_path, pak_path, pc_pak_path, force=False, log=print):
    """Convert one file. Returns the replacements applied; raises SaveError with a user message."""
    if os.path.exists(destination_path) and os.path.samefile(source_path, destination_path):
        raise SaveError('the destination is the same file as the source')
    if os.path.exists(destination_path) and not force:
        raise SaveError('%s already exists; it is not replaced without --force' % destination_path)

    schema = load_schema()
    with open(source_path, 'rb') as f:
        data = f.read()
    parsed = parse_body(schema, split_360(data), 'big')
    info = summary(parsed)
    log('360 save: %s (%s), version %d, %d items in the inventory'
        % (info['name'], info['class'], info['version'], info['items']))

    target = GameData.from_pak(pc_pak_path)
    source = GameData.from_pak(pak_path)
    replacements, problems = check_references(parsed, target, source)
    if problems:
        lines = '\n'.join('  - %s' % p for p in problems)
        raise SaveError('the save references data that does not exist in the PC game (%s); it is '
                        'not converted:\n%s' % (pc_pak_path, lines))

    dialog = {}

    def adapt(x360_save):
        dialog['changes'], dialog['problems'] = adapt_quest_dialogs(x360_save, target, source,
                                                                    names=('360', 'PC'))

    output = x360_to_pc(schema, data, replacements, adapt)
    if dialog['problems']:
        lines = '\n'.join('  - %s' % p for p in dialog['problems'])
        raise SaveError('the quest state cannot be adapted to the PC game data without guessing; '
                        'it is not converted:\n%s' % lines)
    # The result must read back as a valid PC save before it is written.
    _, check = read_pc(schema, output)
    for quest in check.tree['quests']['active']:
        lines = target.quest_dialogs.get(quest['name'].upper())
        if lines is not None and [len(p) for p in quest['quest']['byte_pairs']] != [len(l) for l in lines]:
            raise SaveError('internal error: the dialog state of quest %s does not match the PC '
                            'game data' % quest['name'])

    _write_new(destination_path, output, force)
    for offset, guid in sorted(replacements.items()):
        log('quest GUID at offset %d replaced with %d' % (offset, guid))
    for change in dialog['changes']:
        log('adapted %s' % change)
    log('Wrote %s (%d bytes). The source was not modified.' % (destination_path, len(output)))
    return replacements


def main(argv=None):
    parser = argparse.ArgumentParser(description='Converts a Torchlight 360 / recomp save (.TSV) to '
                                     'the PC v1.15 format (.SVT).')
    parser.add_argument('source', help='360 save (N.TSV); only read')
    parser.add_argument('destination', help='save to create (N.SVT)')
    parser.add_argument('--pak', required=True, help='pak.zip of the 360 game the save comes from')
    parser.add_argument('--pc-pak', required=True, help='Pak.zip of the PC game (next to Torchlight.exe)')
    parser.add_argument('--force', action='store_true', help='replace the destination if it exists')
    args = parser.parse_args(argv)
    try:
        convert_to_pc(args.source, args.destination, args.pak, args.pc_pak, args.force)
    except (SaveError, OSError) as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
