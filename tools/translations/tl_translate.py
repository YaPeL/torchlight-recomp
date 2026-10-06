#!/usr/bin/env python3
"""Language packs for the game: what a translation is missing, and adding what someone translated.

A language pack is translations/<code>/translation.dat.adm in the game data (see
src/game_menu/language_pack.cpp). The texts the game needs translated are the originals of its own
translations (translations/de|fr|es/translation.dat.adm), read from the user's game data: nothing of
the game is copied into this repository.

  tl_translate.py missing --game-data DIR --lang CODE [--out FILE]
      Writes the texts the pack has no translation for (default translations/<code>/missing.txt).
  tl_translate.py merge --game-data DIR --lang CODE FILE
      Adds the translated entries of FILE to the pack (creating it if needed); the previous
      translation.dat.adm is kept as translation.dat.adm.bak.

The text file is UTF-8, one entry per pair of lines, taken exactly as written after the prefix (texts
can start or end with spaces):

  EN: New Character
  TR: Новый персонаж

An entry with an empty TR is left out. "\\n" in a text is the game's line break, and markers like
[VALUE], [ITEM] or |cFFFFBA00...|u must stay in the translation; merge refuses entries whose markers
differ from the original's.
"""

import argparse
import os
import re
import shutil
import struct
import sys

# ---- .adm (Runic's compiled DAT) ------------------------------------------------------------------
# u32 version, u32 string count, then (u32 id, u32 length, UTF-16LE text) per string; then a tree of
# nodes: u32 name id, u32 property count, properties (u32 key id, u32 type, value), u32 child count,
# children. Values are 4 bytes except type 7 (8 bytes); types 5 and 8 are string ids. All
# little-endian.

STRING_TYPES = (5, 8)
WIDE_TYPES = (7,)


class AdmError(Exception):
    pass


def read_adm(data):
    """Returns the root node as (name, [(key, type, value)], [children]), strings resolved."""
    try:
        version, count = struct.unpack_from("<II", data, 0)
        offset = 8
        strings = {}
        for _ in range(count):
            sid, length = struct.unpack_from("<II", data, offset)
            offset += 8
            strings[sid] = data[offset:offset + 2 * length].decode("utf-16-le")
            offset += 2 * length

        def u32():
            nonlocal offset
            value = struct.unpack_from("<I", data, offset)[0]
            offset += 4
            return value

        def node():
            nonlocal offset
            name = strings[u32()]
            props = []
            for _ in range(u32()):
                key = strings[u32()]
                kind = u32()
                if kind in WIDE_TYPES:
                    value = struct.unpack_from("<q", data, offset)[0]
                    offset += 8
                else:
                    value = u32()
                    if kind in STRING_TYPES:
                        value = strings[value]
                props.append((key, kind, value))
            children = [node() for _ in range(u32())]
            return (name, props, children)

        root = node()
    except (struct.error, KeyError, UnicodeDecodeError) as e:
        raise AdmError(f"not a valid .adm file ({e})")
    if offset != len(data):
        raise AdmError(f"{len(data) - offset} bytes left after the tree")
    if version != 1:
        raise AdmError(f"unknown version {version}")
    return root


def write_adm(root):
    """The bytes of an .adm holding `root` (strings properties only, as translation files have)."""
    ids = {}

    def sid(text):
        if text not in ids:
            ids[text] = len(ids) + 1
        return ids[text]

    tree = bytearray()

    def node(n):
        name, props, children = n
        tree.extend(struct.pack("<II", sid(name), len(props)))
        for key, kind, value in props:
            if kind not in STRING_TYPES:
                raise AdmError("only string properties can be written")
            tree.extend(struct.pack("<III", sid(key), kind, sid(value)))
        tree.extend(struct.pack("<I", len(children)))
        for child in children:
            node(child)

    node(root)
    out = bytearray(struct.pack("<II", 1, len(ids)))
    for text, i in ids.items():
        encoded = text.encode("utf-16-le")
        out.extend(struct.pack("<II", i, len(encoded) // 2))
        out.extend(encoded)
    out.extend(tree)
    return bytes(out)


# ---- translation files ----------------------------------------------------------------------------

def read_translations(root):
    """{original: translation} of a TRANSLATIONS tree (entries without both are skipped)."""
    name, _, children = root
    if name != "TRANSLATIONS":
        raise AdmError(f"root is {name}, not TRANSLATIONS")
    pairs = {}
    for child_name, props, _ in children:
        if child_name != "TRANSLATION":
            continue
        p = {key: value for key, kind, value in props if kind in STRING_TYPES}
        if p.get("ORIGINAL") and p.get("TRANSLATION"):
            pairs[p["ORIGINAL"]] = p["TRANSLATION"]
    return pairs


def translations_tree(pairs):
    children = [("TRANSLATION", [("ORIGINAL", 5, o), ("TRANSLATION", 5, t)], [])
                for o, t in pairs.items()]
    return ("TRANSLATIONS", [], children)


NATIVE_LANGUAGES = ("de", "fr", "es")


def game_originals(game_data):
    """The texts the game needs translated: the originals of its own translations, in a stable
    order (as they appear in the first file, then new ones from the others)."""
    ordered = {}
    found = False
    for lang in NATIVE_LANGUAGES:
        path = pack_file(game_data, lang)
        if not os.path.exists(path):
            continue
        found = True
        with open(path, "rb") as f:
            for original in read_translations(read_adm(f.read())):
                ordered.setdefault(original, None)
    if not found:
        raise AdmError(f"no translations/de|fr|es/translation.dat.adm in {game_data}")
    return list(ordered)


def pack_file(game_data, lang):
    return os.path.join(game_data, "translations", lang, "translation.dat.adm")


def read_pack(game_data, lang):
    path = pack_file(game_data, lang)
    if not os.path.exists(path):
        return {}
    with open(path, "rb") as f:
        return read_translations(read_adm(f.read()))


# ---- the editable text file -----------------------------------------------------------------------

def format_entries(entries, header=""):
    lines = [f"# {line}" for line in header.splitlines()]
    for original, translation in entries:
        lines += ["", f"EN: {original}", f"TR: {translation}"]
    return "\n".join(lines) + "\n"


def parse_entries(text):
    """[(original, translation, line)] of the EN/TR pairs; raises ValueError on a broken pair."""
    entries = []
    original = None
    for number, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip("\r")
        if line.startswith("EN: ") or line == "EN:":
            if original is not None:
                raise ValueError(f"line {number}: EN without the TR of line {original[1]}")
            original = (line[4:], number)
        elif line.startswith("TR: ") or line == "TR:":
            if original is None:
                raise ValueError(f"line {number}: TR without an EN before it")
            entries.append((original[0], line[4:], original[1]))
            original = None
        elif line.strip() and not line.startswith("#"):
            raise ValueError(f"line {number}: expected EN:, TR:, a comment or a blank line")
    if original is not None:
        raise ValueError(f"line {original[1]}: EN without a TR")
    return entries


MARKER = re.compile(r"\[[A-Z_]+\]|\|c[0-9A-Fa-f]{8}|\|u|\\n")


def markers(text):
    return sorted(MARKER.findall(text))


def missing_markers(original, translation):
    """Markers the translation does not have the same number of; empty when they match. Line breaks
    may move and change in number."""
    a = [m for m in markers(original) if m != "\\n"]
    b = [m for m in markers(translation) if m != "\\n"]
    return [] if a == b else sorted(set(a) ^ set(b)) or ["(count)"]


# ---- commands -------------------------------------------------------------------------------------

def command_missing(game_data, lang, out):
    originals = game_originals(game_data)
    pack = read_pack(game_data, lang)
    missing = [o for o in originals if o not in pack]
    header = (f"Texts of the game without a translation in translations/{lang}/: {len(missing)} of "
              f"{len(originals)}.\nWrite the translation after \"TR: \"; entries left empty are "
              f"ignored.\nKeep markers like [VALUE] or |cFFFFBA00...|u; \\n is a line break.\n"
              f"Then: tl_translate.py merge --game-data DIR --lang {lang} THIS_FILE")
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "w", encoding="utf-8") as f:
        f.write(format_entries([(o, "") for o in missing], header))
    return len(missing), len(originals)


def command_merge(game_data, lang, text):
    """Returns (added, replaced, refused) and writes the pack."""
    entries = parse_entries(text)
    pack = read_pack(game_data, lang)
    added = replaced = 0
    refused = []
    for original, translation, line in entries:
        if not translation.strip():
            continue
        bad = missing_markers(original, translation)
        if bad:
            refused.append((line, original, bad))
            continue
        if original in pack:
            replaced += pack[original] != translation
        else:
            added += 1
        pack[original] = translation
    if added or replaced:
        path = pack_file(game_data, lang)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.exists(path):
            shutil.copyfile(path, path + ".bak")
        data = write_adm(translations_tree(pack))
        assert read_translations(read_adm(data)) == pack
        with open(path, "wb") as f:
            f.write(data)
    return added, replaced, refused


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("missing", help="list the texts a pack has no translation for")
    g = sub.add_parser("merge", help="add translated entries to a pack")
    for p in (m, g):
        p.add_argument("--game-data", required=True, help="the extracted game data (has pak.zip)")
        p.add_argument("--lang", required=True, help="language code, the translations/<code> folder")
    m.add_argument("--out", help="output file (default translations/<code>/missing.txt)")
    g.add_argument("file", help="the EN/TR text file")
    args = parser.parse_args(argv)
    lang = args.lang.lower()
    try:
        if args.command == "missing":
            out = args.out or os.path.join(args.game_data, "translations", lang, "missing.txt")
            count, total = command_missing(args.game_data, lang, out)
            print(f"{count} of {total} texts missing; written to {out}")
        else:
            with open(args.file, encoding="utf-8") as f:
                added, replaced, refused = command_merge(args.game_data, lang, f.read())
            for line, original, bad in refused:
                print(f"line {line}: markers differ ({' '.join(bad)}), skipped: {original[:60]}",
                      file=sys.stderr)
            print(f"{added} added, {replaced} changed, {len(refused)} skipped; "
                  f"{pack_file(args.game_data, lang)}")
    except (AdmError, ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
