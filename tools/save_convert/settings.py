"""Torchlight settings files (settings.txt, local_settings.txt): reading, editing and import rules.

Both are UTF-16 text with one "KEY :value" per line, '\\n' line ends and a final '\\n'. The 360 writes
the bytes FF FE first and then the text big-endian (measured on the recomp's own files). The PC
writes them little-endian; that comes from Torchlight.exe (_wfopen_s, wide strings) and is NOT yet
verified with a real PC file, so both byte orders are accepted only when the text decodes cleanly.

The 360 reader (sub_823DEF18) registers every key with a default (sub_82398428): a key missing
from the file takes the game's default, so a file may hold only some keys. Lines that are not
changed are written back byte for byte.
"""

from savefile import SaveError

SETTINGS_FILES = ('settings.txt', 'local_settings.txt')
BOM = b'\xff\xfe'

# The keys are listed under the 360 file that holds them. The PC keeps some of them in the other
# file (SHOW TIPS and GAME_COMPLETED_ONCE are in its settings.txt, measured on a real PC install),
# so a key is looked up in both PC files.
#
# User preferences that mean the same on both platforms. Everything else is not imported: video
# and renderer options (the recomp's settings.toml and the 360 game handle them), brightness and
# contrast (the 360 game's own gamma options), measurements, debug and console switches, Steam
# options, paths, and keyboard bindings.
PREFERENCES = {
    'settings.txt': ('AUTOMAP', 'SHOW BLOOD', 'NO CAMERA SHAKE', 'AUTOMAP ZOOM'),
    'local_settings.txt': ('SOUND VOLUME', 'MUSIC VOLUME', 'SOUND MUTE', 'MUSIC MUTE', 'SHOW TIPS',
                           'FLOATY_NUMBERS'),
}
# Progress flags: integers that only go up (0 -> 1). Imported whenever the PC value is higher.
PROGRESS = {
    'settings.txt': (),
    'local_settings.txt': ('GAME_COMPLETED_ONCE',),
}


class SettingsFile:
    def __init__(self, encoding, lines):
        self.encoding = encoding  # 'utf-16-be' or 'utf-16-le'
        self.lines = lines        # text of each line, without its '\n'

    @classmethod
    def parse(cls, data, what='settings file'):
        if not data.startswith(BOM):
            raise SaveError('%s does not start with the FF FE mark of a Torchlight settings file' % what)
        body = data[len(BOM):]
        if len(body) % 2:
            raise SaveError('%s has an odd number of bytes after its mark' % what)
        decoded = []
        for encoding in ('utf-16-be', 'utf-16-le'):
            try:
                text = body.decode(encoding)
            except UnicodeDecodeError:
                continue
            if all(c in '\n\r\t' or 32 <= ord(c) < 127 for c in text):
                decoded.append((encoding, text))
        if len(decoded) != 1:
            raise SaveError('%s is not plain "KEY :value" text in UTF-16 (byte order unclear)' % what)
        encoding, text = decoded[0]
        if text and not text.endswith('\n'):
            raise SaveError('%s does not end with a line break' % what)
        return cls(encoding, text.split('\n')[:-1] if text else [])

    def to_bytes(self):
        return BOM + ''.join(line + '\n' for line in self.lines).encode(self.encoding)

    def _find(self, key):
        for index, line in enumerate(self.lines):
            name, sep, _ = line.partition(' :')
            if sep and name == key:
                return index
        return None

    def get(self, key):
        index = self._find(key)
        if index is None:
            return None
        return self.lines[index].partition(' :')[2].rstrip('\r')

    def set(self, key, value):
        index = self._find(key)
        line = '%s :%s' % (key, value)
        if index is None:
            self.lines.append(line)
        else:
            ending = '\r' if self.lines[index].endswith('\r') else ''
            self.lines[index] = line + ending


def new_360_file():
    """An empty settings file the way the 360 writes them (FF FE, big-endian text)."""
    return SettingsFile('utf-16-be', [])


def _number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def plan_import(name, pc, recomp, force):
    """Decide the changes for one settings file of the recomp.

    pc: the PC SettingsFile, or a list of them (a key is taken from the first that has it);
    recomp: the recomp's (None if it does not exist yet). Returns
    (changes [(key, old, new)], skipped [(key, recomp value, pc value)], problems [str]). Without
    an existing recomp file every imported key is taken; with one, preferences that differ are only
    taken with force, and progress flags are taken when the PC value is higher.
    """
    changes, skipped, problems = [], [], []
    pc_files = pc if isinstance(pc, list) else [pc]
    for key in PREFERENCES[name] + PROGRESS[name]:
        value = next((v for v in (f.get(key) for f in pc_files) if v is not None), None)
        if value is None:
            continue
        if _number(value) is None:
            problems.append('%s in the PC %s is not a number: %r' % (key, name, value))
            continue
        old = recomp.get(key) if recomp else None
        if old == value:
            continue
        if key in PROGRESS[name]:
            if old is not None and _number(old) is None:
                problems.append('%s in the recomp %s is not a number: %r' % (key, name, old))
            elif old is None or _number(value) > _number(old):
                changes.append((key, old, value))
            continue
        if recomp is None or old is None or force:
            changes.append((key, old, value))
        else:
            skipped.append((key, old, value))
    return changes, skipped, problems
