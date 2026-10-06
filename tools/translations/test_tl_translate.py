#!/usr/bin/env python3
"""Tests for tl_translate.py on synthetic game data (no game files needed)."""

import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import tl_translate as tt  # noqa: E402


def make_game(root, native, packs=None):
    """translations/<lang>/translation.dat.adm files holding the given {original: translation}."""
    for lang, pairs in {**native, **(packs or {})}.items():
        path = tt.pack_file(root, lang)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(tt.write_adm(tt.translations_tree(pairs)))


class AdmTest(unittest.TestCase):
    def test_round_trip(self):
        pairs = {"Continue": "Привет", " leading space": "x ", "A\\nB [VALUE]": "Б\\nА [VALUE]"}
        data = tt.write_adm(tt.translations_tree(pairs))
        self.assertEqual(tt.read_translations(tt.read_adm(data)), pairs)

    def test_rejects_garbage(self):
        with self.assertRaises(tt.AdmError):
            tt.read_adm(b"\x01\x00\x00\x00\x05\x00\x00\x00")
        data = tt.write_adm(tt.translations_tree({"a": "b"}))
        with self.assertRaises(tt.AdmError):
            tt.read_adm(data + b"\x00")

    def test_reads_other_types(self):
        # A node with an int (type 1) and an int64 (type 7), as game DATs have.
        import struct
        strings = {1: "UNIT", 2: "LEVEL", 3: "GUID"}
        out = bytearray(struct.pack("<II", 1, len(strings)))
        for i, s in strings.items():
            e = s.encode("utf-16-le")
            out += struct.pack("<II", i, len(e) // 2) + e
        out += struct.pack("<II", 1, 2) + struct.pack("<III", 2, 1, 7)
        out += struct.pack("<II", 3, 7) + struct.pack("<q", -5) + struct.pack("<I", 0)
        self.assertEqual(tt.read_adm(bytes(out)), ("UNIT", [("LEVEL", 1, 7), ("GUID", 7, -5)], []))


class FileFormatTest(unittest.TestCase):
    def test_entries_keep_spaces(self):
        text = tt.format_entries([(" x ", " y"), ("a", "")], header="h1\nh2")
        self.assertEqual(tt.parse_entries(text), [(" x ", " y", 4), ("a", "", 7)])

    def test_broken_pairs(self):
        for bad in ["EN: a\nEN: b\nTR: c\n", "TR: a\n", "EN: a\n", "EN: a\nhello\nTR: b\n"]:
            with self.assertRaises(ValueError):
                tt.parse_entries(bad)

    def test_markers(self):
        self.assertEqual(tt.missing_markers("+[VALUE] to [ITEM]", "[ITEM]: +[VALUE]"), [])
        self.assertEqual(tt.missing_markers("|cFFFFBA00inventory|u", "|cFFFFBA00инвентарь|u"), [])
        self.assertNotEqual(tt.missing_markers("[VALUE] Damage", "Урон"), [])
        self.assertNotEqual(tt.missing_markers("[VALUE] x [VALUE]", "[VALUE] x"), [])
        self.assertEqual(tt.missing_markers("one\\ntwo", "one two"), [], "line breaks may move")


class CommandsTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = self.dir.name
        make_game(self.root,
                  native={"es": {"Continue": "Continuar", "New Character": "Nuevo personaje"},
                          "fr": {"Continue": "Continuer", "Back to Game": "Retour au jeu"}},
                  packs={"ru": {"Continue": "Привет"}})

    def tearDown(self):
        self.dir.cleanup()

    def test_originals_union(self):
        self.assertEqual(tt.game_originals(self.root), ["Continue", "Back to Game", "New Character"],
                         "de, fr, es order, first appearance")

    def test_missing_then_merge(self):
        out = os.path.join(self.root, "missing.txt")
        self.assertEqual(tt.command_missing(self.root, "ru", out), (2, 3))
        with open(out, encoding="utf-8") as f:
            text = f.read()
        self.assertIn("EN: New Character\nTR: \n", text)
        text = text.replace("EN: New Character\nTR: ", "EN: New Character\nTR: Новый персонаж")
        added, replaced, refused = tt.command_merge(self.root, "ru", text)
        self.assertEqual((added, replaced, refused), (1, 0, []))
        pack = tt.read_pack(self.root, "ru")
        self.assertEqual(pack, {"Continue": "Привет", "New Character": "Новый персонаж"})
        self.assertTrue(os.path.exists(tt.pack_file(self.root, "ru") + ".bak"), "previous kept")
        self.assertEqual(tt.command_missing(self.root, "ru", out), (1, 3), "only the rest missing")

    def test_merge_new_language_and_refusals(self):
        text = tt.format_entries([("Continue", "Continua"), ("Back to Game", ""),
                                  ("+[VALUE] Armor", "Armatura")])
        added, replaced, refused = tt.command_merge(self.root, "it", text)
        self.assertEqual((added, replaced, len(refused)), (1, 0, 1))
        self.assertEqual(tt.read_pack(self.root, "it"), {"Continue": "Continua"})
        added, replaced, _ = tt.command_merge(self.root, "it", tt.format_entries([("Continue", "Avanti")]))
        self.assertEqual((added, replaced), (0, 1))

    def test_cli(self):
        out = os.path.join(self.root, "m.txt")
        self.assertEqual(tt.main(["missing", "--game-data", self.root, "--lang", "RU", "--out", out]), 0)
        self.assertEqual(tt.main(["merge", "--game-data", self.root, "--lang", "ru", out]), 0)
        self.assertEqual(tt.main(["missing", "--game-data", "/nonexistent", "--lang", "ru",
                                  "--out", out]), 1)


if __name__ == "__main__":
    unittest.main()
