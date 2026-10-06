#!/usr/bin/env python3
"""Import the Torchlight PC game settings that apply to the recomp.

Usage:
  convert_settings.py PC_DIR RECOMP_DIR [--force]

PC_DIR holds the PC settings.txt / local_settings.txt; RECOMP_DIR is the recomp's save container
(the folder with the N.TSV files). Only user preferences that mean the same on both platforms
(volumes, mutes, tips, damage numbers, blood, camera shake, automap) and the "game completed once"
progress flag are imported; video, brightness, debug, Steam, paths and keys are not (see
settings.py). Nothing the user configured in the recomp is overwritten: a recomp settings file that
does not exist yet is created with the imported keys only (the game takes its defaults for the
rest); in an existing one, preferences that differ are listed and only changed with --force, and
the progress flag is raised when the PC one is higher. The PC files are only read.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from convert import _write_new  # noqa: E402
from savefile import SaveError  # noqa: E402
from settings import SETTINGS_FILES, SettingsFile, new_360_file, plan_import  # noqa: E402


def _read(path, what):
    with open(path, 'rb') as f:
        return SettingsFile.parse(f.read(), what)


def _find(folder, name):
    """The file called name in folder, ignoring case (the PC writes SETTINGS.TXT)."""
    for entry in sorted(os.listdir(folder)):
        if entry.lower() == name and os.path.isfile(os.path.join(folder, entry)):
            return os.path.join(folder, entry)
    return None


def convert_settings(pc_dir, recomp_dir, force=False, log=print):
    """Import the settings found in pc_dir into recomp_dir. Raises SaveError with a user message."""
    if not os.path.isdir(recomp_dir):
        raise SaveError('%s is not a folder' % recomp_dir)
    if not os.path.isdir(pc_dir):
        raise SaveError('%s is not a folder' % pc_dir)
    sources = [path for path in (_find(pc_dir, name) for name in SETTINGS_FILES) if path]
    if not sources:
        raise SaveError('%s has neither settings.txt nor local_settings.txt' % pc_dir)
    pc = [_read(path, 'the PC %s' % os.path.basename(path)) for path in sources]

    plans = []
    for name in SETTINGS_FILES:
        destination = _find(recomp_dir, name) or os.path.join(recomp_dir, name)
        if any(os.path.exists(destination) and os.path.samefile(path, destination) for path in sources):
            raise SaveError('the PC and recomp %s are the same file' % name)
        recomp = _read(destination, 'the recomp %s' % name) if os.path.exists(destination) else None
        changes, skipped, problems = plan_import(name, pc, recomp, force)
        if problems:
            raise SaveError('the settings cannot be imported:\n%s'
                            % '\n'.join('  - %s' % p for p in problems))
        plans.append((name, destination, recomp, changes, skipped))

    for name, destination, recomp, changes, skipped in plans:
        for key, old, new in skipped:
            log('%s: %s kept at %s (PC: %s); use --force to take the PC value' % (name, key, old, new))
        if not changes:
            log('%s: nothing to import' % name)
            continue
        result = recomp if recomp is not None else new_360_file()
        for key, _, new in changes:
            result.set(key, new)
        data = result.to_bytes()
        SettingsFile.parse(data, 'the new %s' % name)  # must read back before it is written
        # An existing file is replaced atomically; a new one is created exclusively.
        _write_new(destination, data, recomp is not None)
        for key, old, new in changes:
            log('%s: %s %s -> %s' % (name, key, 'unset' if old is None else old, new))
        log('Wrote %s. The PC files were not modified.' % destination)


def main(argv=None):
    parser = argparse.ArgumentParser(description='Imports the Torchlight PC settings that apply '
                                     'to the recomp.')
    parser.add_argument('pc_dir', help='folder with the PC settings.txt / local_settings.txt; only read')
    parser.add_argument('recomp_dir', help="the recomp's save container (the folder with the N.TSV)")
    parser.add_argument('--force', action='store_true',
                        help='also replace preferences already set differently in the recomp')
    args = parser.parse_args(argv)
    try:
        convert_settings(args.pc_dir, args.recomp_dir, args.force)
    except (SaveError, OSError) as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
