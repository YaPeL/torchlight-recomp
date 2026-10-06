#!/usr/bin/env python3
"""Convert a Torchlight PC character save (N.SVT) into an Xbox 360 one (N.TSV) for the recomp.

Usage:
  convert.py SOURCE.SVT DESTINATION.TSV --pak PATH/pak.zip --pc-pak PATH/Pak.zip [--force]

--pak is the 360 pak.zip of the user's own game (the extracted XBLA package). --pc-pak is the
Pak.zip of the PC game the save comes from: the save's quest state follows the PC quest
definitions, which differ from the 360 ones in a few quests, and some quest GUIDs changed. The
source is only read; the destination is created exclusively (or swapped in atomically with
--force) and never replaced unless --force is given.
"""

import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from gamedata import GameData, adapt_quest_dialogs, check_references  # noqa: E402
from savefile import SaveError, load_schema, pc_to_360, read_pc, split_360, parse_body, summary  # noqa: E402


PC_PAK_MISSING = (
    'the Pak.zip of Torchlight PC is needed (--pc-pak). The save keeps the state of each quest '
    'according to the PC game data, and a few quests differ on the 360, so the converter has to '
    'compare both to adapt it (and to recognise quests whose GUID changed). It is the Pak.zip '
    'next to Torchlight.exe in the PC installation, for example '
    '"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Torchlight\\Pak.zip" on Windows or '
    '"~/.local/share/Steam/steamapps/common/Torchlight/Pak.zip" with Steam on Linux (other Steam '
    'libraries and GOG installs have it in their own Torchlight folder).')


def convert(source_path, destination_path, pak_path, pc_pak_path, force=False, log=print):
    """Convert one file. Returns the replacements applied; raises SaveError with a user message."""
    if not pc_pak_path:
        raise SaveError(PC_PAK_MISSING)
    if os.path.exists(destination_path) and os.path.samefile(source_path, destination_path):
        raise SaveError('the destination is the same file as the source')
    if os.path.exists(destination_path) and not force:
        raise SaveError('%s already exists; it is not replaced without --force' % destination_path)

    schema = load_schema()
    with open(source_path, 'rb') as f:
        data = f.read()
    _, parsed = read_pc(schema, data)
    info = summary(parsed)
    log('PC save: %s (%s), version %d, %d items in the inventory'
        % (info['name'], info['class'], info['version'], info['items']))

    target = GameData.from_pak(pak_path)
    source = GameData.from_pak(pc_pak_path)
    replacements, problems = check_references(parsed, target, source)
    if problems:
        lines = '\n'.join('  - %s' % p for p in problems)
        raise SaveError('the save references data that does not exist in the 360 game (%s); it is '
                        'not converted:\n%s' % (pak_path, lines))

    dialog = {}

    def adapt(pc_save):
        dialog['changes'], dialog['problems'] = adapt_quest_dialogs(pc_save, target, source)

    output = pc_to_360(schema, data, replacements, adapt)
    if dialog['problems']:
        lines = '\n'.join('  - %s' % p for p in dialog['problems'])
        raise SaveError('the quest state cannot be adapted to the 360 game data without guessing; '
                        'it is not converted:\n%s' % lines)
    # The result must read back as a valid 360 save before it is written.
    check = parse_body(schema, split_360(output), 'big')
    if check.end != len(output) - 32:
        raise SaveError('internal error: the converted save cannot be read back')
    for quest in check.tree['quests']['active']:
        lines = target.quest_dialogs.get(quest['name'].upper())
        if lines is not None and [len(p) for p in quest['quest']['byte_pairs']] != [len(l) for l in lines]:
            raise SaveError('internal error: the dialog state of quest %s does not match the 360 '
                            'game data' % quest['name'])

    _write_new(destination_path, output, force)
    for offset, guid in sorted(replacements.items()):
        log('quest GUID at offset %d replaced with %d' % (offset, guid))
    for change in dialog['changes']:
        log('adapted %s' % change)
    log('Wrote %s (%d bytes). The source was not modified.' % (destination_path, len(output)))
    return replacements


def _write_new(path, data, force):
    if not force:
        # Exclusive create: never replaces a file, even one that appeared after the first check.
        try:
            fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_BINARY', 0), 0o644)
        except FileExistsError:
            raise SaveError('%s already exists; it is not replaced without --force' % path)
        try:
            with os.fdopen(fd, 'wb') as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
        except BaseException:
            os.unlink(path)
            raise
        return
    # --force: write aside and swap in, so a failure never leaves a half-written save.
    fd, temporary = tempfile.mkstemp(prefix='.save_convert_', dir=os.path.dirname(os.path.abspath(path)))
    try:
        # mkstemp creates the file private (0600); give it the replaced file's mode instead, or the
        # usual one for a new file.
        if os.path.exists(path):
            os.chmod(temporary, os.stat(path).st_mode & 0o7777)
        else:
            umask = os.umask(0)
            os.umask(umask)
            os.chmod(temporary, 0o666 & ~umask)
        with os.fdopen(fd, 'wb') as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Converts a Torchlight PC save (.SVT) to the '
                                     '360 format (.TSV) the recomp uses.')
    parser.add_argument('source', help='PC save (N.SVT); only read')
    parser.add_argument('destination', help='save to create (N.TSV)')
    parser.add_argument('--pak', required=True, help='pak.zip of the 360 game (the recomp data)')
    parser.add_argument('--pc-pak', help='Pak.zip of the PC game (required; next to Torchlight.exe)')
    parser.add_argument('--force', action='store_true', help='replace the destination if it exists')
    args = parser.parse_args(argv)
    try:
        convert(args.source, args.destination, args.pak, args.pc_pak, args.force)
    except SaveError as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    except OSError as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
