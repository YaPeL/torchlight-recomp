#!/usr/bin/env python3
"""Synthetic fixtures for the C++ port (src/save_import), from the Python tool's own generator.

    python3 tests/save_convert/make_fixtures.py

writes tests/save_import/fixtures/: PC inputs made by synthetic.py and the outputs the Python tool
gives for them. The C++ tests must produce the same bytes. No game data is involved, and
test_save_convert checks that the committed fixtures match what this script makes now (so a schema
change that forgets them fails).
"""

import hashlib
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'save_convert'))

import synthetic  # noqa: E402
from convert_stash import slot_problems  # noqa: E402
from gamedata import DIALOG_SECTIONS, GameData, adapt_quest_dialogs, check_references  # noqa: E402
from settings import BOM, SETTINGS_FILES, SettingsFile, new_360_file, plan_import  # noqa: E402
from savefile import (load_schema, parse_body, pc_stash_to_360, pc_to_360, read_pc,  # noqa: E402
                      read_pc_stash, signed64, write_body)

# Dialog lines of the synthetic quest whose PASSIVE section differs between the two paks.
LINE_A = ('SPEAKER1', 'line one')
LINE_D = ('SPEAKER4', 'line four')
NEW_ALPHA_GUID = 0x4444000000000001

FIXTURES = os.path.join(HERE, '..', 'save_import', 'fixtures')


def fixtures():
    """{file name: bytes}."""
    schema = load_schema()
    pc_save = synthetic.make_save(schema)
    pc_stash = synthetic.make_stash(schema)
    # A native-style v25 save (big-endian with its SHA-256), to read and write back unchanged.
    body25 = synthetic.make_save(schema, version=25)[:-4]
    tree25 = parse_body(schema, body25, 'little').tree
    big25 = write_body(schema, tree25, 'big')
    files = {}
    with tempfile.TemporaryDirectory() as folder:
        # The PC pak, the 360 pak (QUEST_ALPHA has a new GUID and one more PASSIVE line), and a 360
        # pak without one unit.
        paks = {
            'pak_pc.zip': dict(dialogs={'QUEST_ALPHA': {'PASSIVE': [LINE_A]}}),
            'pak_360.zip': dict(quests={'QUEST_ALPHA': NEW_ALPHA_GUID,
                                        'QUEST_BETA': synthetic.QUESTS['QUEST_BETA']},
                                dialogs={'QUEST_ALPHA': {'PASSIVE': [LINE_D, LINE_A]}}),
            'pak_360_missing_unit.zip': dict(drop_unit=synthetic.UNIT_GUIDS[1]),
        }
        data = {}
        for name, options in paks.items():
            path = os.path.join(folder, name)
            synthetic.make_pak(path, **options)
            with open(path, 'rb') as f:
                files[name] = f.read()
            data[name] = GameData.from_pak(path)
    for name in ('pak_pc.zip', 'pak_360.zip'):
        files[name.replace('.zip', '.dump.txt')] = dump(data[name]).encode()

    # A save with an item of QUEST_ALPHA (old GUID): converted with both paks, as the tool does.
    quest_save = synthetic.make_save(schema, overrides={('item', 'q4'): synthetic.QUESTS['QUEST_ALPHA']})
    _, parsed = read_pc(schema, quest_save)
    replacements, problems = check_references(parsed, data['pak_360.zip'], data['pak_pc.zip'])
    assert not problems, problems
    report = {}

    def adapt(pc_save):
        report['changes'], report['problems'] = adapt_quest_dialogs(pc_save, data['pak_360.zip'],
                                                                    data['pak_pc.zip'])

    files['quest_save.svt'] = quest_save
    files['quest_save.expected.tsv'] = pc_to_360(schema, quest_save, replacements, adapt)
    files['quest_save.report.txt'] = ''.join(
        ['replace %d %d\n' % item for item in sorted(replacements.items())] +
        ['change %s\n' % c for c in report['changes']] +
        ['problem %s\n' % p for p in report['problems']]).encode()

    # The problems a 360 pak without one unit gives for pc_save.svt.
    _, parsed = read_pc(schema, pc_save)
    _, problems = check_references(parsed, data['pak_360_missing_unit.zip'], data['pak_pc.zip'])
    files['pc_save.missing_unit.txt'] = ''.join('%s\n' % p for p in problems).encode()

    # A stash with two items in one slot (and two in the automatic one, which is fine).
    stash = read_pc_stash(schema, pc_stash)
    item = stash.tree['items'][0]
    stash.tree['items'] = [dict(item, w2=slot & 0xFFFFFFFF) for slot in (20, 21, 21, -1, -1)]
    files['stash_duplicate.bin'] = write_body(schema, stash.tree, 'little', schema['stash_root'])
    files['stash_duplicate.problems.txt'] = ''.join(
        p + '\n' for p in slot_problems(read_pc_stash(schema, files['stash_duplicate.bin']))).encode()

    files.update(settings_fixtures())
    return dict(files, **{
        'pc_save.svt': pc_save,
        'pc_save.expected.tsv': pc_to_360(schema, pc_save),
        'pc_stash.bin': pc_stash,
        'pc_stash.expected.bin': pc_stash_to_360(schema, pc_stash),
        'native_v25.tsv': big25 + hashlib.sha256(big25).digest(),
    })


def _settings(lines, encoding, newline):
    return BOM + ''.join(line + newline for line in lines).encode(encoding)


# A PC install keeps SHOW TIPS and GAME_COMPLETED_ONCE in settings.txt (the 360 in
# local_settings.txt), writes little-endian with CRLF, and has keys that are never imported.
PC_SETTINGS = ['SHOW TIPS :0', 'CONSOLE :1', 'SHOW BLOOD :0', 'GAME_COMPLETED_ONCE :1',
               'KEYMAP_PET :80', 'AUTOMAP ZOOM :40.000000']
PC_LOCAL = ['OPENGL :1', 'SOUND VOLUME :0.300000', 'MUSIC VOLUME :0.500000', 'BRIGHTNESS :300.000000',
            'ZIP :pak.zip']
RECOMP_SETTINGS = ['CONSOLE :1', 'AUTOMAP :1', 'SHOW BLOOD :1', 'AUTOMAP ZOOM :30.000000']
RECOMP_LOCAL = ['OPENGL :0', 'SOUND VOLUME :0.800000', 'MUSIC VOLUME :0.500000', 'SHOW TIPS :1',
                'GAME_COMPLETED_ONCE :0', 'BRIGHTNESS :290.000000', 'ZIP :game:\\pak.zip']


def settings_fixtures():
    """PC and recomp settings files, and what the Python tool does with them (three cases)."""
    files = {
        'settings_pc.settings.txt': _settings(PC_SETTINGS, 'utf-16-le', '\r\n'),
        'settings_pc.local_settings.txt': _settings(PC_LOCAL, 'utf-16-le', '\r\n'),
        'settings_recomp.settings.txt': _settings(RECOMP_SETTINGS, 'utf-16-be', '\n'),
        'settings_recomp.local_settings.txt': _settings(RECOMP_LOCAL, 'utf-16-be', '\n'),
    }
    pc = [SettingsFile.parse(files['settings_pc.%s' % name]) for name in SETTINGS_FILES]
    for case, existing, force in (('fresh', False, False), ('existing', True, False),
                                  ('forced', True, True)):
        plan = []
        for name in SETTINGS_FILES:
            recomp = SettingsFile.parse(files['settings_recomp.%s' % name]) if existing else None
            changes, skipped, problems = plan_import(name, pc, recomp, force)
            plan += ['%s change %s %s %s' % (name, k, o, n) for k, o, n in changes]
            plan += ['%s skip %s %s %s' % (name, k, o, n) for k, o, n in skipped]
            plan += ['%s problem %s' % (name, p) for p in problems]
            result = recomp if recomp is not None else new_360_file()
            for key, _, new in changes:
                result.set(key, new)
            files['settings_%s.%s' % (case, name)] = result.to_bytes()
        files['settings_%s.plan.txt' % case] = ''.join(line + '\n' for line in plan).encode()
    return files


def dump(data):
    """A canonical text of a GameData, compared line by line with the C++ port's."""
    lines = ['unit %d' % g for g in data.unit_guids]
    lines += ['unique %d' % g for g in data.unique_guids]
    lines += ['quest %s %d' % (n, signed64(g & 0xFFFFFFFFFFFFFFFF)) for n, g in data.quests.items()]
    for name, sections in data.quest_dialogs.items():
        for section, lines_of in zip(DIALOG_SECTIONS, sections):
            lines += ['dialog %s %s %s|%s' % (name, section, unit, text) for unit, text in lines_of]
    lines += ['effect %s' % n for n in data.effect_names]
    lines += ['skill %s' % n for n in data.skill_names]
    lines += ['affix %s' % n for n in data.affix_names]
    return ''.join(line + '\n' for line in sorted(lines))


def main():
    os.makedirs(FIXTURES, exist_ok=True)
    for name, data in fixtures().items():
        with open(os.path.join(FIXTURES, name), 'wb') as f:
            f.write(data)
        print('wrote %s (%d bytes)' % (name, len(data)))


if __name__ == '__main__':
    main()
