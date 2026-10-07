"""Torchlight character saves: schema interpreter and PC <-> Xbox 360 conversion.

The layout lives in character_save.schema.json (declarative, derived from the
guest reader sub_8221DC20 and the functions it calls); this module only
interprets it. The body is the same on both platforms except for byte order:

  PC v1.15 (N.SVT): little-endian body + u32 total file length.
  Xbox 360 (N.TSV): big-endian body + SHA-256 of the body.

Parsing gives a tree of values (and records every primitive read as a token: offset, size, swap
unit); write_body turns a tree back into bytes in either byte order, recomputing the end-of-block
offsets, so a save can be changed structurally (see gamedata.adapt_quest_dialogs). Parsing and
writing a save without changes gives back the same bytes. Error messages are user-facing.
"""

import hashlib
import json
import os
import struct

SCHEMA_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'character_save.schema.json')

_SIZES = {'u8': 1, 'u16': 2, 'u32': 4, 'u64': 8, 'f32': 4}
_FORMATS = {'u8': 'B', 'u16': 'H', 'u32': 'I', 'u64': 'Q', 'f32': 'I'}

PC_VERSION = 23        # version written by Torchlight PC v1.15 (the only one verified as input)
SHA256_SIZE = 32
LENGTH_TRAILER_SIZE = 4


class SaveError(Exception):
    """A save that cannot be read or converted; the message is meant for the user."""


def load_schema(path=SCHEMA_PATH):
    with open(path, encoding='utf-8') as f:
        return json.load(f)


class Ref:
    """A field with a role (GUID or name reference) found while parsing."""

    def __init__(self, role, offset, size, value, path, element, field):
        self.role = role
        self.offset = offset
        self.size = size
        self.value = value
        self.path = path
        self.element = element  # dict of the element that holds the field
        self.name = field['name']
        self.only_if_set = field.get('only_if_set')  # sibling GUID that must not be "none"

    def __repr__(self):
        return 'Ref(%s, %s, %r)' % (self.role, self.path, self.value)


class Parsed:
    def __init__(self, tree, tokens, refs, version, end):
        self.tree = tree
        self.tokens = tokens  # (offset, size, swap_unit)
        self.refs = refs
        self.version = version
        self.end = end


class _Reader:
    def __init__(self, schema, data, endian):
        self.schema = schema
        self.data = data
        self.prefix = '<' if endian == 'little' else '>'
        self.text_codec = 'utf-16-le' if endian == 'little' else 'utf-16-be'
        self.offset = 0
        self.tokens = []
        self.refs = []
        self.version = None
        self.path = []
        self.element_start = []

    def where(self):
        return '%s (offset %d)' % ('/'.join(self.path) or '<root>', self.offset)

    def need(self, size):
        if size < 0 or self.offset + size > len(self.data):
            raise SaveError('the file ends earlier than expected at %s' % self.where())

    def number(self, kind):
        size = _SIZES[kind]
        self.need(size)
        value = struct.unpack_from(self.prefix + _FORMATS[kind], self.data, self.offset)[0]
        self.tokens.append((self.offset, size, size))
        self.offset += size
        return value

    def text(self, length_kind):
        count = self.number(length_kind)
        self.need(2 * count)
        raw = self.data[self.offset:self.offset + 2 * count]
        if count:
            self.tokens.append((self.offset, 2 * count, 2))
        self.offset += 2 * count
        return raw.decode(self.text_codec, errors='surrogatepass')

    def condition(self, cond):
        if 'version_ge' in cond:
            return self.version >= cond['version_ge']
        if 'version_lt' in cond:
            return self.version < cond['version_lt']
        if 'version_eq' in cond:
            return self.version == cond['version_eq']
        raise ValueError('unknown condition in the schema: %r' % cond)

    def fields(self, fields, element):
        if isinstance(fields, dict):
            fields = [fields]
        for field in fields:
            if 'if' in field:
                branch = field['then'] if self.condition(field['if']) else field.get('else', [])
                self.fields(branch, element)
            else:
                element[field['name']] = self.field(field, element)
        return element

    def field(self, field, element):
        kind = field['type']
        self.path.append(field['name'])
        try:
            start = self.offset
            if kind in _SIZES:
                if 'count_product' in field:
                    count = 1
                    for name in field['count_product']:
                        count *= element[name]
                    self.need(count * _SIZES[kind])
                    return [self.number(kind) for _ in range(count)]
                if 'count' in field:
                    return [self.number(kind) for _ in range(field['count'])]
                value = self.number(kind)
                role = field.get('role')
                if role == 'version':
                    self.version = value
                elif role:
                    self.refs.append(Ref(role, start, _SIZES[kind], value, '/'.join(self.path), element, field))
                if field.get('check') == 'end_of_element':
                    element['__end_check__'] = (value, start)
                return value
            if kind in ('wstring16', 'wstring32'):
                value = self.text('u16' if kind == 'wstring16' else 'u32')
                if field.get('role'):
                    self.refs.append(Ref(field['role'], start, 0, value, '/'.join(self.path), element, field))
                return value
            if kind == 'struct':
                return self.element(self.schema['structs'][field['struct']]['fields'])
            if kind == 'repeat':
                return [self.item(field['of']) for _ in range(field['times'])]
            if kind == 'list':
                count = self.number(field['count'])
                if count > len(self.data) - self.offset:
                    raise SaveError('the list %s claims %d elements, more than the bytes left'
                                    % (self.where(), count))
                return [self.item(field['of']) for _ in range(count)]
            raise ValueError('unknown type in the schema: %r' % kind)
        finally:
            self.path.pop()

    def item(self, of):
        if isinstance(of, list):
            return self.element(of)
        holder = {}
        value = self.field(of, holder)
        return value

    def element(self, fields):
        element = self.fields(fields, {})
        check = element.pop('__end_check__', None)
        if check is not None and check[0] != self.offset:
            raise SaveError('inconsistent end-of-block offset at %s: it says %d, the block ends at %d'
                            % ('/'.join(self.path), check[0], self.offset))
        return element


def parse_body(schema, body, endian, root=None):
    """Parse a save body (without its trailer). Raises SaveError unless every byte is consumed.

    root: the struct the file holds; the character save by default (schema 'root'), the shared
    stash with schema['stash_root'].
    """
    reader = _Reader(schema, body, endian)
    tree = reader.element(schema['structs'][root or schema['root']]['fields'])
    if reader.offset != len(body):
        raise SaveError('%d bytes left uninterpreted after the save (offset %d of %d)'
                        % (len(body) - reader.offset, reader.offset, len(body)))
    return Parsed(tree, reader.tokens, reader.refs, reader.version, reader.offset)


def _swap(body, tokens):
    out = bytearray(body)
    for offset, size, unit in tokens:
        if unit == 1:
            continue
        for at in range(offset, offset + size, unit):
            out[at:at + unit] = out[at:at + unit][::-1]
    return out


def split_pc(data):
    """Body of an N.SVT, after checking its u32 length trailer."""
    if len(data) < LENGTH_TRAILER_SIZE + 4:
        raise SaveError('the file is too small to be a PC save (.SVT)')
    stated = struct.unpack_from('<I', data, len(data) - LENGTH_TRAILER_SIZE)[0]
    if stated != len(data):
        raise SaveError('the length stored at the end (%d) does not match the file size (%d): '
                        'it is not a PC .SVT or it is truncated' % (stated, len(data)))
    return data[:-LENGTH_TRAILER_SIZE]


def split_360(data):
    """Body of an N.TSV, after checking its SHA-256 trailer."""
    if len(data) < SHA256_SIZE + 4:
        raise SaveError('the file is too small to be a 360 save (.TSV)')
    body, digest = data[:-SHA256_SIZE], data[-SHA256_SIZE:]
    if hashlib.sha256(body).digest() != digest:
        raise SaveError('the trailing SHA-256 does not match: it is not a .TSV or it is damaged')
    return body


def read_pc(schema, data):
    body = split_pc(data)
    version = struct.unpack_from('<I', body, 0)[0]
    if version != PC_VERSION:
        raise SaveError('unsupported save version %d: the converter is only verified with '
                        'version %d (Torchlight PC v1.15)' % (version, PC_VERSION))
    return body, parse_body(schema, body, 'little')


class _Writer:
    """Inverse of _Reader: emits a parsed tree with the same schema."""

    def __init__(self, schema, endian):
        self.schema = schema
        self.prefix = '<' if endian == 'little' else '>'
        self.text_codec = 'utf-16-le' if endian == 'little' else 'utf-16-be'
        self.out = bytearray()
        self.version = None
        self.pending_end = []  # offsets of the end_of_element fields of the open element

    def condition(self, cond):
        return _Reader.condition(self, cond)

    def number(self, kind, value):
        self.out += struct.pack(self.prefix + _FORMATS[kind], value)

    def fields(self, fields, element):
        if isinstance(fields, dict):
            fields = [fields]
        for field in fields:
            if 'if' in field:
                self.fields(field['then'] if self.condition(field['if']) else field.get('else', []),
                            element)
            else:
                self.field(field, element[field['name']])

    def field(self, field, value):
        kind = field['type']
        if kind in _SIZES:
            if 'count_product' in field or 'count' in field:
                for item in value:
                    self.number(kind, item)
                return
            if field.get('check') == 'end_of_element':
                self.pending_end.append(len(self.out))
                self.number(kind, 0)  # patched when the element ends
                return
            if field.get('role') == 'version':
                self.version = value
            self.number(kind, value)
        elif kind in ('wstring16', 'wstring32'):
            self.number('u16' if kind == 'wstring16' else 'u32', len(value.encode('utf-16-le', 'surrogatepass')) // 2)
            self.out += value.encode(self.text_codec, 'surrogatepass')
        elif kind == 'struct':
            self.element(self.schema['structs'][field['struct']]['fields'], value)
        elif kind == 'repeat':
            if len(value) != field['times']:
                raise SaveError('internal error: %s must have %d entries' % (field['name'], field['times']))
            for item in value:
                self.item(field['of'], item)
        elif kind == 'list':
            self.number(field['count'], len(value))
            for item in value:
                self.item(field['of'], item)
        else:
            raise ValueError('unknown type in the schema: %r' % kind)

    def item(self, of, value):
        if isinstance(of, list):
            self.element(of, value)
        else:
            self.field(of, value)

    def element(self, fields, element):
        saved, self.pending_end = self.pending_end, []
        self.fields(fields, element)
        for at in self.pending_end:
            struct.pack_into(self.prefix + 'I', self.out, at, len(self.out))
        self.pending_end = saved


def write_body(schema, tree, endian, root=None):
    """Bytes of a save body (without its trailer) from a parsed tree (root as in parse_body)."""
    writer = _Writer(schema, endian)
    writer.element(schema['structs'][root or schema['root']]['fields'], tree)
    return bytes(writer.out)


def pc_to_360(schema, data, replacements=None, adapt=None):
    """Convert N.SVT bytes to N.TSV bytes.

    replacements: {offset in the PC body: new value} for u64 fields (GUIDs that changed between
    the PC and the 360 game data, see gamedata.check_references).
    adapt: optional function called with the parsed PC save to change its tree before it is
    written (see gamedata.adapt_quest_dialogs).
    """
    body, parsed = read_pc(schema, data)
    refs = {ref.offset: ref for ref in parsed.refs}
    for offset, new in (replacements or {}).items():
        ref = refs[offset]
        ref.element[ref.name] = new & 0xFFFFFFFFFFFFFFFF
    if adapt is not None:
        adapt(parsed)
    out = write_body(schema, parsed.tree, 'big')
    return out + hashlib.sha256(out).digest()


def read_pc_stash(schema, data):
    """Parse a PC sharedstash.bin (no trailer). Only the PC v1.15 version is accepted."""
    if len(data) < 8:
        raise SaveError('the file is too small to be a shared stash')
    version = struct.unpack_from('<I', data, 0)[0]
    if version != PC_VERSION:
        raise SaveError('unsupported stash version %d: the converter is only verified with '
                        'version %d (Torchlight PC v1.15)' % (version, PC_VERSION))
    return parse_body(schema, data, 'little', schema['stash_root'])


def read_360_stash(schema, data):
    """Parse a 360 sharedstash.bin (no trailer, no hash)."""
    if len(data) < 8:
        raise SaveError('the file is too small to be a shared stash')
    return parse_body(schema, data, 'big', schema['stash_root'])


def pc_stash_to_360(schema, data, replacements=None):
    """Convert PC sharedstash.bin bytes to 360 ones (replacements as in pc_to_360)."""
    parsed = read_pc_stash(schema, data)
    refs = {ref.offset: ref for ref in parsed.refs}
    for offset, new in (replacements or {}).items():
        ref = refs[offset]
        ref.element[ref.name] = new & 0xFFFFFFFFFFFFFFFF
    return write_body(schema, parsed.tree, 'big', schema['stash_root'])


# The 360 reader compares the version only up to "version >= 23" (no field depends on 24 or 25),
# so a v24 or v25 body has the v23 layout and differs from a PC v1.15 one only in byte order.
X360_VERSIONS_WITH_PC_LAYOUT = (23, 24, 25)


def x360_to_pc(schema, data, replacements=None, adapt=None):
    """Convert N.TSV bytes (version 23 to 25) to PC v1.15 N.SVT bytes (version 23).

    replacements and adapt as in pc_to_360, the other way round (offsets in the 360 body; adapt
    is called with the parsed 360 save). Unchanged, a v23 N.TSV gives back the original N.SVT.
    """
    body = split_360(data)
    parsed = parse_body(schema, body, 'big')
    if parsed.version not in X360_VERSIONS_WITH_PC_LAYOUT:
        raise SaveError('unsupported 360 save version %d: only versions %s have the PC v1.15 layout'
                        % (parsed.version, ', '.join(str(v) for v in X360_VERSIONS_WITH_PC_LAYOUT)))
    refs = {ref.offset: ref for ref in parsed.refs}
    for offset, new in (replacements or {}).items():
        ref = refs[offset]
        ref.element[ref.name] = new & 0xFFFFFFFFFFFFFFFF
    if adapt is not None:
        adapt(parsed)
    parsed.tree['version'] = PC_VERSION
    out = write_body(schema, parsed.tree, 'little')
    return out + struct.pack('<I', len(out) + LENGTH_TRAILER_SIZE)


def signed64(value):
    return value - (1 << 64) if value >= 1 << 63 else value


def summary(parsed):
    tree = parsed.tree
    player = tree.get('player', {})
    return {
        'version': parsed.version,
        'class': tree.get('class_name'),
        'name': player.get('name'),
        'items': len(player.get('items', [])),
        'pets': [pet.get('name') for pet in tree.get('pets', [])],
    }
