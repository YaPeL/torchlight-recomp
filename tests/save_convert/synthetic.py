"""Synthetic Torchlight saves and paks for the save_convert tests (no game data involved).

make_save walks the same schema the converter uses and emits a PC (little-endian) save in which
every struct appears at least once. GUIDs and names come from small invented pools, returned so
the tests can build matching (or deliberately incomplete) game data.
"""

import struct
import zipfile

UNIT_GUIDS = [0x1111000000000001 + i for i in range(4)]
UNIQUE_GUIDS = [0x2222000000000001 + i for i in range(3)]
QUESTS = {'QUEST_ALPHA': 0x3333000000000001, 'QUEST_BETA': 0x3333000000000002}
EFFECT_NAMES = ['EFFECT ONE', 'EFFECT TWO']
SKILL_NAMES = ['SKILL ONE']
AFFIX_NAMES = ['AFFIX ONE']
GRID_SIZE = {'width': 2, 'height': 3}


class _Writer:
    def __init__(self, schema, version, overrides):
        self.schema = schema
        self.version = version
        self.overrides = overrides  # {(struct, field): value} for every element of that struct
        self.out = bytearray()
        self.counter = 0
        self.depth = {}

    def next(self):
        self.counter += 1
        return self.counter

    def put(self, fmt, value):
        self.out += struct.pack('<' + fmt, value)

    def text(self, value, wide32=False):
        self.put('I' if wide32 else 'H', len(value))
        self.out += value.encode('utf-16-le')

    def condition(self, cond):
        if 'version_ge' in cond:
            return self.version >= cond['version_ge']
        if 'version_lt' in cond:
            return self.version < cond['version_lt']
        return self.version == cond['version_eq']

    def fields(self, fields, struct_name, values):
        if isinstance(fields, dict):
            fields = [fields]
        for field in fields:
            if 'if' in field:
                self.fields(field['then'] if self.condition(field['if']) else field.get('else', []),
                            struct_name, values)
            else:
                values[field['name']] = self.field(field, struct_name, values)

    def scalar(self, field, struct_name):
        key = (struct_name, field['name'])
        if key in self.overrides:
            return self.overrides[key]
        role = field.get('role')
        n = self.next()
        if role == 'version':
            return self.version
        if role == 'unit_guid':
            return UNIT_GUIDS[n % len(UNIT_GUIDS)]
        if role == 'unique_guid':
            return UNIQUE_GUIDS[n % len(UNIQUE_GUIDS)]
        if role == 'quest_guid':
            return list(QUESTS.values())[n % len(QUESTS)]
        if field['name'] in GRID_SIZE:
            return GRID_SIZE[field['name']]
        return n

    def field(self, field, struct_name, values):
        kind = field['type']
        fmt = {'u8': 'B', 'u16': 'H', 'u32': 'I', 'u64': 'Q', 'f32': 'f'}.get(kind)
        if fmt:
            if 'count' in field or 'count_product' in field:
                count = field.get('count', 1)
                for name in field.get('count_product', []):
                    count *= values[name]
                for i in range(count):
                    self.put(fmt, 0.5 * i if kind == 'f32' else (i + 1) & 0xFF)
                return None
            if field.get('check') == 'end_of_element':
                at = len(self.out)
                self.put('I', 0)
                self.pending_end.append(at)
                return None
            value = self.scalar(field, struct_name)
            if kind == 'f32':
                self.put('f', float(value))
            else:
                self.put(fmt, value & {'B': 0xFF, 'H': 0xFFFF, 'I': 0xFFFFFFFF, 'Q': 0xFFFFFFFFFFFFFFFF}[fmt])
            return value
        if kind in ('wstring16', 'wstring32'):
            role = field.get('role')
            n = self.next()
            if role == 'effect_name':
                value = EFFECT_NAMES[n % len(EFFECT_NAMES)]
            elif role == 'effect_source':
                value = (SKILL_NAMES + AFFIX_NAMES)[n % 2]
            elif role == 'quest_name':
                value = list(QUESTS)[n % len(QUESTS)]
            else:
                value = 'T%d' % n
            self.text(value, kind == 'wstring32')
            return value
        if kind == 'struct':
            return self.element(field['struct'])
        if kind == 'repeat':
            for _ in range(field['times']):
                self.item(field['of'], struct_name)
            return None
        if kind == 'list':
            recursive = isinstance(field['of'], dict) and field['of'].get('struct') == struct_name
            count = 0 if recursive and self.depth.get(struct_name, 0) > 1 else 1
            self.put('H' if field['count'] == 'u16' else 'I', count)
            for _ in range(count):
                self.item(field['of'], struct_name)
            return None
        raise ValueError(kind)

    def item(self, of, struct_name):
        if isinstance(of, list):
            saved, self.pending_end = getattr(self, 'pending_end', []), []
            self.fields(of, struct_name, {})
            for at in self.pending_end:
                struct.pack_into('<I', self.out, at, len(self.out))
            self.pending_end = saved
        else:
            self.field(of, struct_name, {})

    def element(self, name):
        self.depth[name] = self.depth.get(name, 0) + 1
        saved, self.pending_end = getattr(self, 'pending_end', []), []
        values = {}
        self.fields(self.schema['structs'][name]['fields'], name, values)
        for at in self.pending_end:
            struct.pack_into('<I', self.out, at, len(self.out))
        self.pending_end = saved
        self.depth[name] -= 1
        return values


def make_save(schema, version=23, overrides=None, visited=None):
    """Return the bytes of a synthetic PC save (body + u32 total length).

    visited, if given, is a set that receives the name of every struct written.
    """
    body = _body(schema, schema['root'], version, overrides, visited)
    return body + struct.pack('<I', len(body) + 4)


def make_stash(schema, version=23, overrides=None, visited=None):
    """Return the bytes of a synthetic PC sharedstash.bin (no trailer), with one item."""
    return _body(schema, schema['stash_root'], version, overrides, visited)


def _body(schema, root, version, overrides, visited):
    writer = _Writer(schema, version, overrides or {})
    writer.element(root)
    if visited is not None:
        visited.update(writer.depth)
    return bytes(writer.out)


def make_adm(root):
    """Serialize an .adm node tree: root = (name, [(key, type, value)], [children])."""
    strings = {}

    def sid(text):
        if text not in strings:
            strings[text] = len(strings) + 1
        return strings[text]

    def node(n):
        name, props, children = n
        out = struct.pack('<II', sid(name), len(props))
        for key, kind, value in props:
            out += struct.pack('<II', sid(key), kind)
            if kind in (5, 8):
                out += struct.pack('<I', sid(value))
            elif kind == 7:
                out += struct.pack('<q', value)
            elif kind == 2:
                out += struct.pack('<f', value)
            else:
                out += struct.pack('<I', value)
        out += struct.pack('<I', len(children))
        for child in children:
            out += node(child)
        return out

    tree = node(root)
    head = struct.pack('<II', 1, len(strings))
    for text, number in strings.items():
        head += struct.pack('<II', number, len(text)) + text.encode('utf-16-le')
    return head + tree


def signed(value):
    return value - (1 << 64) if value >= 1 << 63 else value


def dialog_node(sections):
    """DIALOG block of a quest: sections = {section: [(UNITNAME, DIALOG text)]}."""
    children = [(section, [('UNITNAME', 5, unit), ('DIALOG', 8, text)], [])
                for section, lines in sections.items() for unit, text in lines]
    return ('DIALOG', [], children)


class _FixedTimeZip:
    """Writes entries with a fixed date and system, so the same pak gives the same bytes anywhere."""

    def __init__(self, zip_file):
        self.zip_file = zip_file

    def writestr(self, name, data):
        info = zipfile.ZipInfo(name, date_time=(2010, 1, 1, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        info.create_system = 3  # ZipInfo's default depends on the OS (0 on Windows, 3 elsewhere)
        self.zip_file.writestr(info, data)


def make_pak(path, quests=None, drop_unit=None, dialogs=None):
    """Write a synthetic pak.zip with the pools above.

    Optionally with different quest GUIDs, a unit missing, or DIALOG blocks per quest
    (dialogs = {quest name: {section: [(UNITNAME, DIALOG text)]}}).
    """
    quests = QUESTS if quests is None else quests
    # By default every quest has one line per dialog section, like the state make_save writes.
    default = {section: [('UNIT', 'line %s' % section)] for section in ('INTRO', 'RETURN', 'COMPLETE', 'PASSIVE')}
    dialogs = {name: dict(default, **(dialogs or {}).get(name, {})) for name in quests}
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as zip_file:
        z = _FixedTimeZip(zip_file)
        for i, guid in enumerate(UNIT_GUIDS):
            if guid == drop_unit:
                continue
            props = [('NAME', 5, 'Unit %d' % i), ('UNIT_GUID', 5, str(signed(guid)))]
            effects = [('EFFECT', [('NAME', 5, n)], []) for n in EFFECT_NAMES] if i == 0 else []
            z.writestr('media/units/items/unit%d.dat.adm' % i, make_adm(('UNIT', props, effects)))
        for i, guid in enumerate(UNIQUE_GUIDS):
            z.writestr('media/units/items/unique%d.dat.adm' % i,
                       make_adm(('UNIT', [('UNIQUE_GUID', 7, signed(guid))], [])))
        for name, guid in quests.items():
            children = [dialog_node(dialogs[name])] if name in dialogs else []
            z.writestr('media/quests/%s.dat.adm' % name.lower(),
                       make_adm(('QUEST', [('NAME', 5, name), ('QUEST_GUID', 7, signed(guid))], children)))
        for name in SKILL_NAMES:
            z.writestr('media/skills/s.dat.adm', make_adm(('SKILL', [('NAME', 5, name)], [])))
        for name in AFFIX_NAMES:
            z.writestr('media/affixes/a.dat.adm', make_adm(('AFFIX', [('NAME', 5, name)], [])))
        z.writestr('media/other/ignored.txt', b'not adm')
