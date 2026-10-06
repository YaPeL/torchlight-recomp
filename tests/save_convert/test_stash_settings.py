"""Tests for the shared stash and settings import of tools/save_convert (synthetic data only).

Run: python3 -m unittest discover tests/save_convert
"""

import io
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'save_convert'))

import convert_settings  # noqa: E402
import convert_stash  # noqa: E402
import synthetic  # noqa: E402
from savefile import (SaveError, load_schema, read_360_stash, read_pc_stash,  # noqa: E402
                      write_body)
from settings import BOM, SettingsFile, plan_import  # noqa: E402

SCHEMA = load_schema()
STASH = SCHEMA['stash_root']


def run(main, *args):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main(list(args))
    return code, out.getvalue(), err.getvalue()


def stash_with_slots(*slots):
    """A synthetic PC stash with one item per slot."""
    parsed = read_pc_stash(SCHEMA, synthetic.make_stash(SCHEMA))
    item = parsed.tree['items'][0]
    parsed.tree['items'] = [dict(item, w2=slot & 0xFFFFFFFF) for slot in slots]
    return write_body(SCHEMA, parsed.tree, 'little', STASH)


class StashFormatTest(unittest.TestCase):
    def test_synthetic_stash_round_trip(self):
        data = synthetic.make_stash(SCHEMA)
        parsed = read_pc_stash(SCHEMA, data)
        self.assertEqual(len(parsed.tree['items']), 1)
        self.assertEqual(write_body(SCHEMA, parsed.tree, 'little', STASH), data)

    def test_empty_360_stash(self):
        empty = struct.pack('>II', 25, 0)
        parsed = read_360_stash(SCHEMA, empty)
        self.assertEqual(parsed.tree, {'version': 25, 'items': []})
        self.assertEqual(write_body(SCHEMA, parsed.tree, 'big', STASH), empty)

    def test_other_pc_version_is_rejected(self):
        with self.assertRaisesRegex(SaveError, 'unsupported stash version 25'):
            read_pc_stash(SCHEMA, struct.pack('<II', 25, 0))

    def test_truncated_stash_is_rejected(self):
        with self.assertRaises(SaveError):
            read_pc_stash(SCHEMA, synthetic.make_stash(SCHEMA)[:-3])


class StashCommandTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        # Each pak in its own folder: 'pak.zip' and 'Pak.zip' side by side are one file on Windows.
        os.makedirs(os.path.join(self.dir.name, '360'))
        os.makedirs(os.path.join(self.dir.name, 'pc_install'))
        self.pak = os.path.join(self.dir.name, '360', 'pak.zip')
        self.pc_pak = os.path.join(self.dir.name, 'pc_install', 'Pak.zip')
        synthetic.make_pak(self.pak)
        synthetic.make_pak(self.pc_pak)
        os.makedirs(os.path.join(self.dir.name, 'pc'))
        os.makedirs(os.path.join(self.dir.name, 'recomp'))
        self.source = os.path.join(self.dir.name, 'pc', 'sharedstash.bin')
        self.destination = os.path.join(self.dir.name, 'recomp', 'sharedstash.bin')
        self.write_source(synthetic.make_stash(SCHEMA))

    def write_source(self, data):
        with open(self.source, 'wb') as f:
            f.write(data)
        self.source_bytes = data

    def tearDown(self):
        with open(self.source, 'rb') as f:
            self.assertEqual(f.read(), self.source_bytes, 'the source is never modified')
        self.dir.cleanup()

    def convert(self, *extra, destination=None):
        return run(convert_stash.main, self.source, destination or self.destination,
                   '--pak', self.pak, '--pc-pak', self.pc_pak, *extra)

    def test_converts_to_big_endian_with_the_same_items(self):
        code, _, err = self.convert()
        self.assertEqual(code, 0, err)
        with open(self.destination, 'rb') as f:
            converted = read_360_stash(SCHEMA, f.read())
        self.assertEqual(converted.tree, read_pc_stash(SCHEMA, self.source_bytes).tree)

    def test_existing_destination_is_kept_and_its_items_counted(self):
        with open(self.destination, 'wb') as f:
            f.write(struct.pack('>II', 25, 0))
        code, _, err = self.convert()
        self.assertEqual(code, 1)
        self.assertIn('holds 0 items', err)
        self.assertIn('--force', err)
        with open(self.destination, 'rb') as f:
            self.assertEqual(f.read(), struct.pack('>II', 25, 0))
        code, _, err = self.convert('--force')
        self.assertEqual(code, 0, err)

    def test_hardcore_and_normal_stash_are_not_mixed(self):
        code, _, err = self.convert(destination=os.path.join(self.dir.name, 'recomp', 'sharedstashh.bin'))
        self.assertEqual(code, 1)
        self.assertIn('hardcore', err)

    def test_missing_unit_is_refused(self):
        guid = read_pc_stash(SCHEMA, self.source_bytes).tree['items'][0]['unit_guid']
        synthetic.make_pak(self.pak, drop_unit=guid)
        code, _, err = self.convert()
        self.assertEqual(code, 1)
        self.assertIn('unit (UNIT_GUID) %d' % (guid - (1 << 64) if guid >= 1 << 63 else guid), err)
        self.assertFalse(os.path.exists(self.destination))

    def test_two_items_in_one_slot_are_refused(self):
        self.write_source(stash_with_slots(20, 21, 21))
        code, _, err = self.convert()
        self.assertEqual(code, 1)
        self.assertIn('slot 21 holds 2 items', err)
        self.assertFalse(os.path.exists(self.destination))

    def test_automatic_slots_may_repeat(self):
        self.write_source(stash_with_slots(-1, -1, 999, 20))
        code, _, err = self.convert()
        self.assertEqual(code, 0, err)

    def test_missing_pc_pak_is_explained(self):
        code, _, err = run(convert_stash.main, self.source, self.destination, '--pak', self.pak)
        self.assertEqual(code, 1)
        self.assertIn('Torchlight.exe', err)


def settings_bytes(lines, encoding='utf-16-le', newline='\n'):
    return BOM + ''.join(line + newline for line in lines).encode(encoding)


PC_LOCAL = ['OPENGL :1', 'RES_WIDTH :1920', 'SOUND VOLUME :0.300000', 'MUSIC VOLUME :0.100000',
            'GAME_COMPLETED_ONCE :1', 'BRIGHTNESS :300.000000', 'ZIP :pak.zip']
PC_SETTINGS = ['CONSOLE :1', 'SHOW BLOOD :0', 'KEYMAP_PET :80', 'AUTOMAP ZOOM :40.000000']
RECOMP_LOCAL = ['OPENGL :0', 'SOUND VOLUME :0.800000', 'MUSIC VOLUME :0.500000',
                'GAME_COMPLETED_ONCE :0', 'BRIGHTNESS :290.000000', 'ZIP :game:\\pak.zip']


class SettingsFormatTest(unittest.TestCase):
    def test_both_byte_orders_round_trip(self):
        for encoding in ('utf-16-be', 'utf-16-le'):
            data = settings_bytes(RECOMP_LOCAL, encoding)
            parsed = SettingsFile.parse(data)
            self.assertEqual(parsed.encoding, encoding)
            self.assertEqual(parsed.get('ZIP'), 'game:\\pak.zip')  # value with ':'
            self.assertEqual(parsed.to_bytes(), data)

    def test_crlf_lines_are_read_and_kept(self):
        data = settings_bytes(PC_LOCAL, newline='\r\n')
        parsed = SettingsFile.parse(data)
        self.assertEqual(parsed.get('SOUND VOLUME'), '0.300000')
        self.assertEqual(parsed.to_bytes(), data)
        parsed.set('SOUND VOLUME', '0.5')
        self.assertIn('SOUND VOLUME :0.5\r', parsed.lines)

    def test_not_a_settings_file(self):
        with self.assertRaisesRegex(SaveError, 'FF FE'):
            SettingsFile.parse('SOUND VOLUME :1\n'.encode('utf-16-le'))
        with self.assertRaisesRegex(SaveError, 'byte order'):
            SettingsFile.parse(BOM + bytes([0x00, 0x01, 0x02, 0x03]))


class SettingsPlanTest(unittest.TestCase):
    def plan(self, recomp_lines, force=False, name='local_settings.txt', pc_lines=PC_LOCAL):
        pc = SettingsFile.parse(settings_bytes(pc_lines))
        recomp = None if recomp_lines is None else SettingsFile.parse(settings_bytes(recomp_lines, 'utf-16-be'))
        return plan_import(name, pc, recomp, force)

    def test_new_file_takes_only_imported_keys(self):
        changes, skipped, problems = self.plan(None)
        self.assertEqual(problems, [])
        self.assertEqual({k for k, _, _ in changes}, {'SOUND VOLUME', 'MUSIC VOLUME', 'GAME_COMPLETED_ONCE'})
        changes, _, _ = self.plan(None, name='settings.txt', pc_lines=PC_SETTINGS)
        self.assertEqual({k for k, _, _ in changes}, {'SHOW BLOOD', 'AUTOMAP ZOOM'})

    def test_existing_preferences_are_kept_without_force(self):
        changes, skipped, _ = self.plan(RECOMP_LOCAL)
        self.assertEqual([k for k, _, _ in changes], ['GAME_COMPLETED_ONCE'])  # progress goes up
        self.assertEqual({k for k, _, _ in skipped}, {'SOUND VOLUME', 'MUSIC VOLUME'})
        changes, skipped, _ = self.plan(RECOMP_LOCAL, force=True)
        self.assertEqual({k for k, _, _ in changes}, {'SOUND VOLUME', 'MUSIC VOLUME', 'GAME_COMPLETED_ONCE'})
        self.assertEqual(skipped, [])

    def test_progress_never_goes_down(self):
        recomp = [line.replace('GAME_COMPLETED_ONCE :0', 'GAME_COMPLETED_ONCE :1') for line in RECOMP_LOCAL]
        pc = [line.replace('GAME_COMPLETED_ONCE :1', 'GAME_COMPLETED_ONCE :0') for line in PC_LOCAL]
        changes, _, _ = self.plan(recomp, force=True, pc_lines=pc)
        self.assertNotIn('GAME_COMPLETED_ONCE', [k for k, _, _ in changes])

    def test_value_that_is_not_a_number_is_a_problem(self):
        pc = PC_LOCAL + ['MUSIC MUTE :maybe']
        _, _, problems = self.plan(None, pc_lines=pc)
        self.assertEqual(len(problems), 1)


class SettingsCommandTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.pc = os.path.join(self.dir.name, 'pc')
        self.recomp = os.path.join(self.dir.name, 'recomp')
        os.makedirs(self.pc)
        os.makedirs(self.recomp)
        self.sources = {'local_settings.txt': settings_bytes(PC_LOCAL),
                        'settings.txt': settings_bytes(PC_SETTINGS)}
        for name, data in self.sources.items():
            with open(os.path.join(self.pc, name), 'wb') as f:
                f.write(data)

    def tearDown(self):
        for name, data in self.sources.items():
            with open(os.path.join(self.pc, name), 'rb') as f:
                self.assertEqual(f.read(), data, 'the PC files are never modified')
        self.dir.cleanup()

    def read(self, name):
        with open(os.path.join(self.recomp, name), 'rb') as f:
            return SettingsFile.parse(f.read())

    def test_creates_the_recomp_files_with_imported_keys_only(self):
        code, _, err = run(convert_settings.main, self.pc, self.recomp)
        self.assertEqual(code, 0, err)
        local = self.read('local_settings.txt')
        self.assertEqual(local.encoding, 'utf-16-be')  # the way the 360 writes it
        self.assertEqual(sorted(line.split(' :')[0] for line in local.lines),
                         ['GAME_COMPLETED_ONCE', 'MUSIC VOLUME', 'SOUND VOLUME'])
        self.assertEqual(self.read('settings.txt').get('SHOW BLOOD'), '0')

    def test_existing_file_changes_only_the_imported_lines(self):
        before = settings_bytes(RECOMP_LOCAL, 'utf-16-be')
        with open(os.path.join(self.recomp, 'local_settings.txt'), 'wb') as f:
            f.write(before)
        code, out, err = run(convert_settings.main, self.pc, self.recomp)
        self.assertEqual(code, 0, err)
        self.assertIn('--force', out)  # the preferences that differ are listed
        after = self.read('local_settings.txt')
        expected = [line.replace('GAME_COMPLETED_ONCE :0', 'GAME_COMPLETED_ONCE :1') for line in RECOMP_LOCAL]
        self.assertEqual(after.to_bytes(), settings_bytes(expected, 'utf-16-be'))

    def test_force_also_takes_the_preferences(self):
        with open(os.path.join(self.recomp, 'local_settings.txt'), 'wb') as f:
            f.write(settings_bytes(RECOMP_LOCAL, 'utf-16-be'))
        code, _, err = run(convert_settings.main, self.pc, self.recomp, '--force')
        self.assertEqual(code, 0, err)
        after = self.read('local_settings.txt')
        self.assertEqual(after.get('SOUND VOLUME'), '0.300000')
        self.assertEqual(after.get('OPENGL'), '0')        # not imported
        self.assertEqual(after.get('BRIGHTNESS'), '290.000000')
        self.assertEqual(after.get('ZIP'), 'game:\\pak.zip')

    def test_pc_layout_upper_case_names_and_keys_in_the_other_file(self):
        # A real PC install writes SETTINGS.TXT with CRLF, and keeps SHOW TIPS and
        # GAME_COMPLETED_ONCE there; the 360 keeps them in local_settings.txt.
        for name in self.sources:
            os.remove(os.path.join(self.pc, name))
        self.sources = {
            'SETTINGS.TXT': settings_bytes(['SHOW TIPS :0', 'SHOW BLOOD :0', 'GAME_COMPLETED_ONCE :1'],
                                           newline='\r\n'),
            'local_settings.txt': settings_bytes(['SOUND VOLUME :0.300000'], newline='\r\n')}
        for name, data in self.sources.items():
            with open(os.path.join(self.pc, name), 'wb') as f:
                f.write(data)
        code, _, err = run(convert_settings.main, self.pc, self.recomp)
        self.assertEqual(code, 0, err)
        local = self.read('local_settings.txt')
        self.assertEqual(local.get('SHOW TIPS'), '0')
        self.assertEqual(local.get('GAME_COMPLETED_ONCE'), '1')
        self.assertEqual(local.get('SOUND VOLUME'), '0.300000')
        self.assertEqual(self.read('settings.txt').lines, ['SHOW BLOOD :0'])

    def test_damaged_recomp_file_stops_everything(self):
        with open(os.path.join(self.recomp, 'settings.txt'), 'wb') as f:
            f.write(b'garbage')
        code, _, err = run(convert_settings.main, self.pc, self.recomp)
        self.assertEqual(code, 1)
        self.assertFalse(os.path.exists(os.path.join(self.recomp, 'local_settings.txt')))


if __name__ == '__main__':
    unittest.main()
