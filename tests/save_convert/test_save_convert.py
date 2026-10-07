"""Tests for tools/save_convert with synthetic saves and paks (no game files).

Run: python3 -m unittest discover tests/save_convert
"""

import hashlib
import io
import os
import struct
import sys
import tempfile
import unittest
import zipfile
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'save_convert'))

import convert  # noqa: E402
import convert_to_pc  # noqa: E402
import synthetic  # noqa: E402
from gamedata import GameData, adapt_quest_dialogs, check_references, parse_adm  # noqa: E402
from savefile import (SaveError, load_schema, parse_body, pc_to_360, read_pc, signed64,  # noqa: E402
                      split_360, write_body, x360_to_pc)

SCHEMA = load_schema()


def game_data(**changes):
    values = dict(unit_guids=[signed64(g) for g in synthetic.UNIT_GUIDS],
                  unique_guids=[signed64(g) for g in synthetic.UNIQUE_GUIDS],
                  quests={n: signed64(g) for n, g in synthetic.QUESTS.items()},
                  effect_names=synthetic.EFFECT_NAMES, skill_names=synthetic.SKILL_NAMES,
                  affix_names=synthetic.AFFIX_NAMES)
    values.update(changes)
    return GameData(**values)


class SchemaTest(unittest.TestCase):
    def test_every_struct_is_defined_and_cites_its_source(self):
        structs = SCHEMA['structs']
        self.assertIn(SCHEMA['root'], structs)

        def walk(fields):
            for f in fields if isinstance(fields, list) else [fields]:
                if 'if' in f:
                    walk(f['then'])
                    walk(f.get('else', []))
                    continue
                self.assertIn(f['type'], ('u8', 'u16', 'u32', 'u64', 'f32', 'wstring16', 'wstring32',
                                          'struct', 'list', 'repeat'))
                if f['type'] == 'struct':
                    self.assertIn(f['struct'], structs)
                if f['type'] in ('list', 'repeat'):
                    walk(f['of'])

        for name, struct_def in structs.items():
            self.assertRegex(struct_def['source']['function'], r'^sub_[0-9A-F]{8}$', name)
            self.assertEqual(struct_def['source']['address'], '0x' + struct_def['source']['function'][4:])
            walk(struct_def['fields'])


class FormatTest(unittest.TestCase):
    def setUp(self):
        self.save = synthetic.make_save(SCHEMA)

    def test_schema_consumes_every_byte_and_tokens_tile_the_body(self):
        body, parsed = read_pc(SCHEMA, self.save)
        self.assertEqual(parsed.end, len(body))
        position = 0
        for offset, size, _ in sorted(parsed.tokens):
            self.assertEqual(offset, position)
            position += size
        self.assertEqual(position, len(body))

    def test_synthetic_save_covers_every_struct(self):
        visited = set()
        synthetic.make_save(SCHEMA, visited=visited)
        synthetic.make_stash(SCHEMA, visited=visited)
        self.assertEqual(visited, set(SCHEMA['structs']))

    def test_round_trip_is_byte_identical(self):
        x360 = pc_to_360(SCHEMA, self.save)
        self.assertEqual(hashlib.sha256(x360[:-32]).digest(), x360[-32:])
        self.assertEqual(struct.unpack_from('>I', x360, 0)[0], 23)
        self.assertEqual(x360_to_pc(SCHEMA, x360), self.save)

    def test_writer_gives_back_the_same_bytes(self):
        body, parsed = read_pc(SCHEMA, self.save)
        self.assertEqual(write_body(SCHEMA, parsed.tree, 'little'), body)
        x360 = split_360(pc_to_360(SCHEMA, self.save))
        self.assertEqual(write_body(SCHEMA, parse_body(SCHEMA, x360, 'big').tree, 'big'), x360)

    def test_writer_recomputes_end_offsets(self):
        _, parsed = read_pc(SCHEMA, self.save)
        quest = parsed.tree['quests']['active'][0]
        quest['quest']['byte_pairs'][3].append([0, 0])  # the block grows by one pair
        body = write_body(SCHEMA, parsed.tree, 'big')
        again = parse_body(SCHEMA, body, 'big')  # raises if an end offset is wrong
        self.assertEqual(len(again.tree['quests']['active'][0]['quest']['byte_pairs'][3]), 2)

    def test_360_side_reads_back_with_the_same_values(self):
        _, pc = read_pc(SCHEMA, self.save)
        x360 = parse_body(SCHEMA, split_360(pc_to_360(SCHEMA, self.save)), 'big')
        self.assertEqual([r.value for r in pc.refs], [r.value for r in x360.refs])
        self.assertEqual(pc.tree, x360.tree)

    def test_truncated_file_is_rejected(self):
        with self.assertRaisesRegex(SaveError, 'length stored'):
            read_pc(SCHEMA, self.save[:-10])

    def test_wrong_length_trailer_inside_body_is_rejected(self):
        body = self.save[:-4][:-6]
        damaged = body + struct.pack('<I', len(body) + 4)
        with self.assertRaisesRegex(SaveError, 'ends earlier|uninterpreted'):
            read_pc(SCHEMA, damaged)

    def test_extra_bytes_are_rejected(self):
        body = self.save[:-4] + b'\0\0'
        with self.assertRaisesRegex(SaveError, 'uninterpreted'):
            read_pc(SCHEMA, body + struct.pack('<I', len(body) + 4))

    def test_unsupported_version_is_rejected(self):
        with self.assertRaisesRegex(SaveError, 'unsupported save version 25'):
            read_pc(SCHEMA, synthetic.make_save(SCHEMA, version=25))

    def test_damaged_360_hash_is_rejected(self):
        x360 = bytearray(pc_to_360(SCHEMA, self.save))
        x360[10] ^= 1
        with self.assertRaisesRegex(SaveError, 'SHA-256'):
            split_360(bytes(x360))


class ReferenceTest(unittest.TestCase):
    def test_all_references_found(self):
        _, parsed = read_pc(SCHEMA, synthetic.make_save(SCHEMA))
        replacements, problems = check_references(parsed, game_data())
        self.assertEqual((replacements, problems), ({}, []))

    def test_missing_unit_is_listed(self):
        _, parsed = read_pc(SCHEMA, synthetic.make_save(SCHEMA))
        missing = signed64(synthetic.UNIT_GUIDS[0])
        data = game_data(unit_guids=[signed64(g) for g in synthetic.UNIT_GUIDS[1:]])
        _, problems = check_references(parsed, data)
        self.assertTrue(problems)
        self.assertTrue(all(p.value == missing for p in problems))

    def test_missing_effect_name_is_listed(self):
        _, parsed = read_pc(SCHEMA, synthetic.make_save(SCHEMA))
        _, problems = check_references(parsed, game_data(effect_names=['OTHER']))
        self.assertIn('EFFECT ONE', {p.value for p in problems} | {p.value.upper() for p in problems})

    def test_changed_quest_guid_is_replaced_by_name(self):
        old = synthetic.QUESTS['QUEST_ALPHA']
        new = 0x4444000000000001
        _, parsed = read_pc(SCHEMA, synthetic.make_save(SCHEMA, overrides={('item', 'q4'): old}))
        target = game_data(quests={'QUEST_ALPHA': signed64(new), 'QUEST_BETA': signed64(synthetic.QUESTS['QUEST_BETA'])})
        source = game_data()
        replacements, problems = check_references(parsed, target, source)
        self.assertEqual(problems, [])
        self.assertIn(signed64(new), replacements.values())

        x360 = parse_body(SCHEMA, split_360(pc_to_360(SCHEMA, synthetic.make_save(
            SCHEMA, overrides={('item', 'q4'): old}), replacements)), 'big')
        values = {r.offset: r.value for r in x360.refs}
        for offset in replacements:
            self.assertEqual(values[offset], new)

    def test_changed_quest_guid_without_pc_data_is_an_error(self):
        old = synthetic.QUESTS['QUEST_ALPHA']
        _, parsed = read_pc(SCHEMA, synthetic.make_save(SCHEMA, overrides={('item', 'q4'): old}))
        target = game_data(quests={'QUEST_ALPHA': 1, 'QUEST_BETA': signed64(synthetic.QUESTS['QUEST_BETA'])})
        _, problems = check_references(parsed, target)
        self.assertTrue(any('PC Pak.zip' in str(p) for p in problems))

    def test_item_without_unit_guid_ignores_its_quest_field(self):
        # Gold piles have no unit GUID and the 360 writes uninitialised memory in their quest field.
        overrides = {('item', 'unit_guid'): 0xFFFFFFFFFFFFFFFF, ('item', 'q4'): 0x0BADBADBADBADBAD}
        _, parsed = read_pc(SCHEMA, synthetic.make_save(SCHEMA, overrides=overrides))
        self.assertEqual(check_references(parsed, game_data())[1], [])


class _Save:
    def __init__(self, active):
        self.tree = {'quests': {'active': active}}


def quest(name, *sections):
    return {'name': name, 'quest': {'byte_pairs': [[list(p) for p in pairs] for pairs in sections]}}


def dialogs(**quests):
    """{quest: (INTRO, RETURN, COMPLETE, PASSIVE) line lists} as GameData.quest_dialogs."""
    return {name: [list(lines) for lines in sections] for name, sections in quests.items()}


A, B, C, D = ('SPEAKER1', 'line one'), ('SPEAKER2', 'line two'), ('SPEAKER3', 'line three'), ('SPEAKER4', 'line four')


class QuestDialogTest(unittest.TestCase):
    def adapt(self, save, pc, x360):
        return adapt_quest_dialogs(save, GameData(quest_dialogs=x360), GameData(quest_dialogs=pc))

    def test_same_definitions_are_left_alone(self):
        save = _Save([quest('Q', [], [], [], [(1, 1), (0, 1)])])
        changes, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B])), dialogs(Q=([], [], [], [A, B])))
        self.assertEqual((changes, problems), ([], []))
        self.assertEqual(save.tree['quests']['active'][0]['quest']['byte_pairs'][3], [[1, 1], [0, 1]])

    def test_removed_line_without_state_is_dropped(self):
        save = _Save([quest('Q', [], [], [], [(0, 0), (1, 0), (0, 1)])])
        changes, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B, C])), dialogs(Q=([], [], [], [B, C])))
        self.assertEqual(problems, [])
        self.assertEqual(save.tree['quests']['active'][0]['quest']['byte_pairs'][3], [[1, 0], [0, 1]])
        self.assertEqual(len(changes), 1)

    def test_inserted_line_starts_new_and_the_rest_keep_their_state(self):
        save = _Save([quest('Q', [], [], [], [(1, 1), (1, 0)])])
        _, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B])), dialogs(Q=([], [], [], [A, D, B])))
        self.assertEqual(problems, [])
        self.assertEqual(save.tree['quests']['active'][0]['quest']['byte_pairs'][3], [[1, 1], [0, 0], [1, 0]])

    def test_removed_line_with_state_is_refused(self):
        save = _Save([quest('Q', [], [], [], [(1, 1), (0, 0)])])
        _, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B])), dialogs(Q=([], [], [], [B])))
        self.assertEqual(len(problems), 1)
        self.assertIn('SPEAKER1', problems[0])

    def test_line_changed_speaker_or_text_is_not_matched(self):
        # The 360 kept the speaker but gave it another line's text: with state, it is refused.
        save = _Save([quest('Q', [], [], [], [(1, 0), (0, 0)])])
        moved = ('SPEAKER2', 'line one')
        _, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B])), dialogs(Q=([], [], [], [moved])))
        self.assertEqual(len(problems), 1)
        # Without state there is nothing to lose.
        save = _Save([quest('Q', [], [], [], [(0, 0), (0, 0)])])
        _, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B])), dialogs(Q=([], [], [], [moved])))
        self.assertEqual(problems, [])
        self.assertEqual(save.tree['quests']['active'][0]['quest']['byte_pairs'][3], [[0, 0]])

    def test_ambiguous_line_with_state_is_refused(self):
        save = _Save([quest('Q', [], [], [], [(0, 0), (1, 1)])])
        _, problems = self.adapt(save, dialogs(Q=([], [], [], [A, A])), dialogs(Q=([], [], [], [A])))
        self.assertEqual(len(problems), 1)
        self.assertIn('cannot be known', problems[0])

    def test_save_not_matching_the_pc_definition_is_refused(self):
        save = _Save([quest('Q', [], [], [], [(0, 0)])])
        _, problems = self.adapt(save, dialogs(Q=([], [], [], [A, B])), dialogs(Q=([], [], [], [A])))
        self.assertEqual(len(problems), 1)

    def test_quest_unknown_to_the_360_is_left_alone(self):
        save = _Save([quest('GONE', [], [], [], [(1, 1)])])
        self.assertEqual(self.adapt(save, dialogs(GONE=([], [], [], [A])), dialogs()), ([], []))


class GameDataTest(unittest.TestCase):
    def test_adm_round_trip(self):
        root = ('UNIT', [('NAME', 5, 'x'), ('GUID', 7, -5), ('N', 1, 3)], [('EFFECT', [('NAME', 5, 'e')], [])])
        name, props, children = parse_adm(synthetic.make_adm(root))
        self.assertEqual(name, 'UNIT')
        self.assertEqual(props, [('NAME', 5, 'x'), ('GUID', 7, -5), ('N', 1, 3)])
        self.assertEqual(children[0][0], 'EFFECT')

    def test_from_pak_reads_guids_and_names(self):
        with tempfile.TemporaryDirectory() as d:
            pak = os.path.join(d, 'pak.zip')
            synthetic.make_pak(pak)
            data = GameData.from_pak(pak)
        self.assertEqual(data.unit_guids, {signed64(g) for g in synthetic.UNIT_GUIDS})
        self.assertEqual(data.unique_guids, {signed64(g) for g in synthetic.UNIQUE_GUIDS})
        self.assertEqual(data.quests, {n: signed64(g) for n, g in synthetic.QUESTS.items()})
        self.assertEqual(data.effect_names, set(synthetic.EFFECT_NAMES))

    def test_from_pak_reads_quest_dialogs(self):
        with tempfile.TemporaryDirectory() as d:
            pak = os.path.join(d, 'pak.zip')
            synthetic.make_pak(pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [A, B], 'INTRO': [C]}})
            data = GameData.from_pak(pak)
        self.assertEqual(data.quest_dialogs['QUEST_ALPHA'],
                         [[C], [('UNIT', 'line RETURN')], [('UNIT', 'line COMPLETE')], [A, B]])
        self.assertEqual(len(data.quest_dialogs['QUEST_BETA'][3]), 1)  # synthetic default

    def test_not_a_pak(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'pak.zip')
            with open(path, 'wb') as f:
                f.write(b'nope')
            with self.assertRaisesRegex(SaveError, 'could not open'):
                GameData.from_pak(path)


class CommandLineTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        # Each pak in its own folder, as in real installs: 'pak.zip' and 'Pak.zip' side by side are
        # the same file on a case-insensitive file system (Windows).
        os.makedirs(os.path.join(self.dir.name, '360'))
        os.makedirs(os.path.join(self.dir.name, 'pc'))
        self.pak = os.path.join(self.dir.name, '360', 'pak.zip')
        synthetic.make_pak(self.pak)
        self.pc_pak = os.path.join(self.dir.name, 'pc', 'Pak.zip')
        synthetic.make_pak(self.pc_pak)
        self.source = os.path.join(self.dir.name, '0.SVT')
        with open(self.source, 'wb') as f:
            f.write(synthetic.make_save(SCHEMA))
        with open(self.source, 'rb') as f:
            self.source_bytes = f.read()
        self.destination = os.path.join(self.dir.name, '0.TSV')

    def tearDown(self):
        with open(self.source, 'rb') as f:
            self.assertEqual(f.read(), self.source_bytes, 'the source is never modified')
        self.dir.cleanup()

    def run_cli(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = convert.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def test_converts(self):
        code, out, _ = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 0)
        with open(self.destination, 'rb') as f:
            self.assertEqual(x360_to_pc(SCHEMA, f.read()), self.source_bytes)

    def test_existing_destination_is_kept(self):
        with open(self.destination, 'wb') as f:
            f.write(b'keep me')
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 1)
        self.assertIn('--force', err)
        with open(self.destination, 'rb') as f:
            self.assertEqual(f.read(), b'keep me')

    def test_force_replaces_destination(self):
        with open(self.destination, 'wb') as f:
            f.write(b'old')
        code, _, _ = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak, '--force')
        self.assertEqual(code, 0)
        with open(self.destination, 'rb') as f:
            self.assertNotEqual(f.read(), b'old')

    def test_force_keeps_the_replaced_file_mode(self):
        with open(self.destination, 'wb') as f:
            f.write(b'old')
        os.chmod(self.destination, 0o644)  # on Windows only the read-only bit exists
        mode = os.stat(self.destination).st_mode & 0o777
        code, _, _ = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak, '--force')
        self.assertEqual(code, 0)
        self.assertEqual(os.stat(self.destination).st_mode & 0o777, mode)

    def test_destination_equal_to_source_is_refused(self):
        code, _, err = self.run_cli(self.source, self.source, '--pak', self.pak, '--pc-pak', self.pc_pak, '--force')
        self.assertEqual(code, 1)
        self.assertIn('same file', err)

    def test_missing_data_writes_nothing(self):
        pak = os.path.join(self.dir.name, 'partial.zip')
        synthetic.make_pak(pak, drop_unit=synthetic.UNIT_GUIDS[0])
        code, _, err = self.run_cli(self.source, self.destination, '--pak', pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 1)
        self.assertIn('does not exist in the 360 game', err)
        self.assertFalse(os.path.exists(self.destination))

    def test_missing_pc_pak_explains_why_and_where(self):
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak)
        self.assertEqual(code, 1)
        self.assertIn('--pc-pak', err)
        self.assertIn('Torchlight.exe', err)
        self.assertFalse(os.path.exists(self.destination))

    def test_quest_dialog_differences_are_adapted(self):
        # The synthetic save has QUEST_ALPHA active with one state pair per dialog section.
        pc = {'QUEST_ALPHA': {'PASSIVE': [A]}}
        x360 = {'QUEST_ALPHA': {'PASSIVE': [D, A]}}
        synthetic.make_pak(self.pc_pak, dialogs=pc)
        synthetic.make_pak(self.pak, dialogs=x360)
        code, out, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 0, err)
        self.assertIn('PASSIVE', out)
        with open(self.destination, 'rb') as f:
            converted = parse_body(SCHEMA, split_360(f.read()), 'big')
        _, original = read_pc(SCHEMA, self.source_bytes)
        before = original.tree['quests']['active'][0]['quest']['byte_pairs'][3][0]
        self.assertEqual(converted.tree['quests']['active'][0]['quest']['byte_pairs'][3], [[0, 0], before])

    def test_quest_dialog_that_cannot_be_adapted_writes_nothing(self):
        synthetic.make_pak(self.pc_pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [A]}})
        synthetic.make_pak(self.pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [B]}})
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 1)
        self.assertIn('without guessing', err)
        self.assertFalse(os.path.exists(self.destination))



def as_version(x360, version):
    """The same 360 save with another version number (the recomp writes 25)."""
    parsed = parse_body(SCHEMA, split_360(x360), 'big')
    parsed.tree['version'] = version
    body = write_body(SCHEMA, parsed.tree, 'big')
    return body + hashlib.sha256(body).digest()


class ConvertToPcTest(unittest.TestCase):
    """convert_to_pc.py: a recomp (360 v25) save back to PC v1.15."""

    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        os.makedirs(os.path.join(self.dir.name, '360'))
        os.makedirs(os.path.join(self.dir.name, 'pc'))
        self.pak = os.path.join(self.dir.name, '360', 'pak.zip')
        self.pc_pak = os.path.join(self.dir.name, 'pc', 'Pak.zip')
        synthetic.make_pak(self.pak)
        synthetic.make_pak(self.pc_pak)
        self.pc_save = synthetic.make_save(SCHEMA)
        self.source = os.path.join(self.dir.name, '0.TSV')
        self.write_source(as_version(pc_to_360(SCHEMA, self.pc_save), 25))
        self.destination = os.path.join(self.dir.name, '0.SVT')

    def tearDown(self):
        with open(self.source, 'rb') as f:
            self.assertEqual(f.read(), self.source_bytes, 'the source is never modified')
        self.dir.cleanup()

    def write_source(self, data):
        with open(self.source, 'wb') as f:
            f.write(data)
        self.source_bytes = data

    def run_cli(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = convert_to_pc.main(list(args))
        return code, out.getvalue(), err.getvalue()

    def converted(self):
        with open(self.destination, 'rb') as f:
            return f.read()

    def test_v25_save_becomes_the_pc_save_it_came_from(self):
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 0, err)
        self.assertEqual(self.converted(), self.pc_save)
        _, parsed = read_pc(SCHEMA, self.converted())
        self.assertEqual(parsed.version, 23)

    def test_versions_without_the_pc_layout_are_rejected(self):
        for version in (22, 26):
            with self.assertRaisesRegex(SaveError, 'unsupported 360 save version %d' % version):
                x360_to_pc(SCHEMA, as_version(pc_to_360(SCHEMA, self.pc_save), version))

    def test_existing_destination_is_kept(self):
        with open(self.destination, 'wb') as f:
            f.write(b'keep me')
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 1)
        self.assertIn('--force', err)
        self.assertEqual(self.converted(), b'keep me')

    def test_data_missing_in_the_pc_game_writes_nothing(self):
        synthetic.make_pak(self.pc_pak, drop_unit=synthetic.UNIT_GUIDS[0])
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 1)
        self.assertIn('does not exist in the PC game', err)
        self.assertFalse(os.path.exists(self.destination))

    def test_quest_dialogs_are_adapted_back_to_the_pc_definitions(self):
        # A PC save converted to the 360 (QUEST_ALPHA's PASSIVE dialog has a line more there) comes
        # back as the original PC save.
        synthetic.make_pak(self.pc_pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [A]}})
        synthetic.make_pak(self.pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [D, A]}})
        to_360 = os.path.join(self.dir.name, 'via.TSV')
        with redirect_stdout(io.StringIO()):
            convert.convert(self._pc_file(), to_360, self.pak, self.pc_pak)
        with open(to_360, 'rb') as f:
            self.write_source(as_version(f.read(), 25))
        code, out, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 0, err)
        self.assertIn('PASSIVE dialog: 2 lines (360) -> 1 (PC)', out)
        self.assertEqual(self.converted(), self.pc_save)

    def test_quest_dialog_that_cannot_be_adapted_writes_nothing(self):
        synthetic.make_pak(self.pc_pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [B]}})
        synthetic.make_pak(self.pak, dialogs={'QUEST_ALPHA': {'PASSIVE': [A]}})
        code, _, err = self.run_cli(self.source, self.destination, '--pak', self.pak, '--pc-pak', self.pc_pak)
        self.assertEqual(code, 1)
        self.assertIn('PC game data without guessing', err)
        self.assertFalse(os.path.exists(self.destination))

    def _pc_file(self):
        path = os.path.join(self.dir.name, 'pc.SVT')
        with open(path, 'wb') as f:
            f.write(self.pc_save)
        return path


def zip_entries(data):
    """[(name, uncompressed bytes)] of a zip, in order: what a zip holds, whatever deflate made it
    (zlib and zlib-ng, as in Python 3.14's Windows builds, compress the same data differently)."""
    with zipfile.ZipFile(io.BytesIO(data)) as zip_file:
        return [(info.filename, zip_file.read(info)) for info in zip_file.infolist()]


class FixturesTest(unittest.TestCase):
    def test_cpp_fixtures_are_up_to_date(self):
        # The C++ port is tested against these; regenerate with make_fixtures.py after a change.
        import make_fixtures
        for name, data in make_fixtures.fixtures().items():
            with open(os.path.join(make_fixtures.FIXTURES, name), 'rb') as f:
                committed = f.read()
            if name.endswith('.zip'):
                committed, data = zip_entries(committed), zip_entries(data)
            self.assertEqual(committed, data, '%s is stale: run tests/save_convert/make_fixtures.py' % name)

if __name__ == '__main__':
    unittest.main()
