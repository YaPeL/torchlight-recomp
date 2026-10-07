"""Game data references for save conversion, read from the user's own pak.zip.

Nothing from the game is stored in the repo: GUIDs and names are read at conversion time from
the .adm files (compiled DAT) inside the pak. The .adm layout is the same in the PC and the
Xbox 360 paks (little-endian in both):

  u32 version, u32 string count, strings (u32 id, u32 length, UTF-16LE chars),
  then a node tree: u32 name id, u32 property count, properties (u32 key id, u32 type, value),
  u32 child count, children.
  Property types and sizes: 1 int32, 2 float, 3 double, 4 uint32, 5 string id, 6 bool (4 bytes),
  7 int64, 8 translated string id.
"""

import struct
import zipfile

from savefile import SaveError, signed64

_VALUE_SIZES = {1: 4, 2: 4, 3: 8, 4: 4, 5: 4, 6: 4, 7: 8, 8: 4}
_STRING_TYPES = (5, 8)

# Folders whose definitions a character save can reference.
_FOLDERS = ('media/units/', 'media/skills/', 'media/affixes/', 'media/quests/')

NONE_GUIDS = (-1, 0)


class AdmError(Exception):
    pass


def parse_adm(data):
    """Return the root node of an .adm file as (name, [(key, type, value)], [children])."""
    offset = 0

    def u32():
        nonlocal offset
        if offset + 4 > len(data):
            raise AdmError('truncated adm')
        value = struct.unpack_from('<I', data, offset)[0]
        offset += 4
        return value

    u32()  # version
    strings = {}
    for _ in range(u32()):
        string_id = u32()
        length = u32()
        strings[string_id] = data[offset:offset + 2 * length].decode('utf-16-le', errors='replace')
        offset += 2 * length

    def node():
        nonlocal offset
        name = strings.get(u32(), '')
        props = []
        for _ in range(u32()):
            key = strings.get(u32(), '')
            kind = u32()
            size = _VALUE_SIZES.get(kind)
            if size is None or offset + size > len(data):
                raise AdmError('unknown property type %d' % kind)
            raw = data[offset:offset + size]
            offset += size
            if kind in _STRING_TYPES:
                value = strings.get(struct.unpack('<I', raw)[0], '')
            elif kind == 1:
                value = struct.unpack('<i', raw)[0]
            elif kind == 2:
                value = struct.unpack('<f', raw)[0]
            elif kind == 3:
                value = struct.unpack('<d', raw)[0]
            elif kind == 7:
                value = struct.unpack('<q', raw)[0]
            else:
                value = struct.unpack('<I', raw)[0]
            props.append((key, kind, value))
        children = [node() for _ in range(u32())]
        return name, props, children

    root = node()
    if offset != len(data):
        raise AdmError('adm with %d leftover bytes' % (len(data) - offset))
    return root


def _as_guid(value):
    try:
        return signed64(int(value) & 0xFFFFFFFFFFFFFFFF)
    except (TypeError, ValueError):
        return None


class GameData:
    """GUIDs and names a save may reference, from one pak (PC or 360)."""

    def __init__(self, unit_guids=(), unique_guids=(), quests=None, effect_names=(), skill_names=(),
                 affix_names=(), quest_dialogs=None):
        self.unit_guids = set(unit_guids)
        self.unique_guids = set(unique_guids)
        self.quests = dict(quests or {})  # upper-case quest NAME -> QUEST_GUID
        self.quest_names = set(self.quests)
        self.quest_guids = {guid: name for name, guid in self.quests.items()}
        # upper-case quest NAME -> one list per DIALOG_SECTIONS of (UNITNAME, DIALOG text) lines
        self.quest_dialogs = dict(quest_dialogs or {})
        self.effect_names = {n.upper() for n in effect_names}
        self.skill_names = {n.upper() for n in skill_names}
        self.affix_names = {n.upper() for n in affix_names}

    @classmethod
    def from_pak(cls, path):
        data = cls()
        try:
            archive = zipfile.ZipFile(path)
        except (OSError, zipfile.BadZipFile) as e:
            raise SaveError('could not open %s as a game pak: %s' % (path, e))
        with archive:
            read = 0
            for info in archive.infolist():
                name = info.filename.replace('\\', '/').lower()
                if not name.endswith('.adm') or not name.startswith(_FOLDERS):
                    continue
                try:
                    root = parse_adm(archive.read(info))
                except AdmError as e:
                    raise SaveError('could not read %s inside %s: %s' % (info.filename, path, e))
                data._add(name, root)
                read += 1
        if not read:
            raise SaveError('%s has no Torchlight data (media/units, media/skills...)' % path)
        data.quest_names = set(data.quests)
        data.quest_guids = {guid: quest for quest, guid in data.quests.items()}
        return data

    def _add(self, path, root):
        _, props, _ = root
        values = {key: value for key, _, value in props}
        for key, _, value in props:
            if key == 'UNIT_GUID':
                guid = _as_guid(value)
                if guid is not None:
                    self.unit_guids.add(guid)
            elif key == 'UNIQUE_GUID':
                guid = _as_guid(value)
                if guid is not None:
                    self.unique_guids.add(guid)
        name = str(values.get('NAME', '')).upper()
        if path.startswith('media/skills/') and name:
            self.skill_names.add(name)
        if path.startswith('media/affixes/') and name:
            self.affix_names.add(name)
        if path.startswith('media/quests/') and name and 'QUEST_GUID' in values:
            self.quests[name] = _as_guid(values['QUEST_GUID'])
            self.quest_dialogs[name] = _dialog_lines(root)
        self._add_effects(root)

    def _add_effects(self, node):
        name, props, children = node
        if name == 'EFFECT':
            for key, _, value in props:
                if key == 'NAME' and value:
                    self.effect_names.add(str(value).upper())
        for child in children:
            self._add_effects(child)


# Sections of a quest's DIALOG block, in the order of the quest's byte_pairs lists in a save. The
# quest definition parser (sub_823C7090) builds one vector per section (quest +204, +220, +236,
# +252) with every child block of that name, in file order; the save stores two state bytes per
# line and nothing that identifies it (sub_823CB2B0). A new line starts at (0, 0) (sub_823CD1F0).
DIALOG_SECTIONS = ('INTRO', 'RETURN', 'COMPLETE', 'PASSIVE')
NEW_LINE_STATE = [0, 0]


def _find_node(node, name):
    if node[0] == name:
        return node
    for child in node[2]:
        found = _find_node(child, name)
        if found:
            return found
    return None


def _dialog_lines(root):
    dialog = _find_node(root, 'DIALOG')
    sections = []
    for section in DIALOG_SECTIONS:
        lines = []
        for child in (dialog[2] if dialog else []):
            if child[0] == section:
                values = {key: value for key, _, value in child[1]}
                lines.append((str(values.get('UNITNAME', '')).upper(), str(values.get('DIALOG', ''))))
        sections.append(lines)
    return sections


def adapt_quest_dialogs(parsed, target, source, names=('PC', '360')):
    """Fit the dialog state of every active quest to the target game's quest definitions.

    The reader (PC or 360) reads as many state pairs as its own definition has lines and does not
    skip the rest, so a save written with the other game's definition is misread wherever they
    differ. Each target line takes the state of the source line with the same speaker and text; a
    target line with no such line starts as new. Returns (changes [str], problems [str]); a problem means a state that
    cannot be placed without guessing, and the save must not be converted. names: how the
    messages call the source and the target game (PC to 360 by default; the other way round for a
    360 save converted to PC).
    """
    source_name, target_name = names
    changes, problems = [], []
    quests = parsed.tree.get('quests', {}).get('active', [])
    for quest in quests:
        name = quest['name'].upper()
        new_lines = target.quest_dialogs.get(name)
        if new_lines is None:
            continue  # unknown to the 360: the reader skips the block through its end offset
        old_lines = source.quest_dialogs.get(name)
        pairs = quest['quest']['byte_pairs']
        for index, section in enumerate(DIALOG_SECTIONS):
            old, new, states = old_lines[index] if old_lines else None, new_lines[index], pairs[index]
            where = 'quest %s, %s dialog' % (quest['name'], section)
            expected = new if old is None else old
            if len(states) != len(expected):
                problems.append('%s: the save has %d lines and the %s game data %d' % (
                    where, len(states), target_name if old is None else source_name, len(expected)))
                continue
            if old is None or old == new:
                continue
            adapted, used, ambiguous = [], set(), set()
            for line in new:
                matches = [i for i, candidate in enumerate(old) if candidate == line]
                if len(matches) == 1:
                    used.add(matches[0])
                    adapted.append(states[matches[0]])
                elif any(states[i] != NEW_LINE_STATE for i in matches):
                    ambiguous.update(matches)
                    problems.append('%s: the line of %s appears %d times in the %s data and one has '
                                    'a state; which one is meant cannot be known'
                                    % (where, line[0], len(matches), source_name))
                    adapted.append(list(NEW_LINE_STATE))
                else:
                    adapted.append(list(NEW_LINE_STATE))
            for i, state in enumerate(states):
                if i not in used and i not in ambiguous and state != NEW_LINE_STATE:
                    problems.append('%s: the line of %s has state %s and the %s game has no line '
                                    'with the same speaker and text'
                                    % (where, old[i][0], tuple(state), target_name))
            pairs[index] = adapted
            changes.append('%s: %d lines (%s) -> %d (%s)' % (where, len(old), source_name, len(new),
                                                             target_name))
    return changes, problems


class Problem:
    def __init__(self, kind, value, path, context):
        self.kind = kind
        self.value = value
        self.path = path
        self.context = context

    def __str__(self):
        value = ('%d' % self.value) if isinstance(self.value, int) else repr(self.value)
        extra = (' (%s)' % self.context) if self.context else ''
        return '%s %s at %s%s' % (self.kind, value, self.path, extra)


_KIND_NAMES = {
    'unit_guid': 'unit (UNIT_GUID)',
    'unique_guid': 'unique (UNIQUE_GUID)',
    'quest_guid': 'quest (QUEST_GUID)',
    'effect_name': 'effect',
    'effect_source': 'effect source (skill, affix or effect)',
    'quest_name': 'quest',
}


def check_references(parsed, target, source=None):
    """Check every GUID and name of a parsed save against the data of the game it goes to (target).

    Quest GUIDs missing in the target are mapped through the data of the game it comes from
    (source) by quest name (PC to 360, or the other way round).
    Returns (replacements {offset: new_guid}, problems [Problem]). Problems mean "do not convert".
    """
    replacements = {}
    problems = []
    seen = set()
    # The source of an effect is a skill, an affix or an effect defined on a unit (e.g. a class's
    # innate armor, an EFFECT node in media/units/players/*.dat).
    effect_sources = target.skill_names | target.affix_names | target.effect_names

    def problem(ref, value, context=''):
        key = (ref.role, value)
        if key in seen:
            return
        seen.add(key)
        problems.append(Problem(_KIND_NAMES[ref.role], value, ref.path, context))

    for ref in parsed.refs:
        element_name = ref.element.get('s0') or ref.element.get('name') or ''
        if ref.role in ('unit_guid', 'unique_guid', 'quest_guid'):
            value = signed64(ref.value)
            if value in NONE_GUIDS:
                continue
            if ref.only_if_set and signed64(ref.element.get(ref.only_if_set, -1)) in NONE_GUIDS:
                continue  # e.g. gold piles: no unit GUID, the rest of the item is uninitialised memory
            if ref.role == 'unit_guid' and value not in target.unit_guids:
                problem(ref, value, element_name)
            elif ref.role == 'unique_guid' and value not in target.unique_guids:
                problem(ref, value, element_name)
            elif ref.role == 'quest_guid' and value not in target.quest_guids:
                name = source.quest_guids.get(value) if source else None
                if name and name in target.quests:
                    replacements[ref.offset] = target.quests[name]
                elif source is None:
                    problem(ref, value, element_name + '; the PC Pak.zip is needed to know which quest it is')
                else:
                    problem(ref, value, element_name)
        else:
            text = (ref.value or '').upper()
            if not text:
                continue
            if ref.role == 'effect_name' and text not in target.effect_names:
                problem(ref, ref.value, element_name)
            elif ref.role == 'effect_source' and text not in effect_sources:
                problem(ref, ref.value, element_name)
            elif ref.role == 'quest_name' and text not in target.quest_names:
                problem(ref, ref.value)
    return replacements, problems
