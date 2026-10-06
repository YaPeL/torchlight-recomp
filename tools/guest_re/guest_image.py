"""Shared helpers: the dumped guest image and the recompiled function starts."""

import bisect
import os
import re
import struct

BASE = 0x82000000


class GuestImage:
    def __init__(self, image_path, funcs_path=None):
        with open(image_path, 'rb') as f:
            self.data = f.read()
        self.funcs = load_function_starts(funcs_path) if funcs_path else []

    def u32(self, addr):
        return struct.unpack_from('>I', self.data, addr - BASE)[0]

    def bytes_at(self, start, end):
        return self.data[start - BASE:end - BASE]

    def cstr(self, addr, limit=96):
        off = addr - BASE
        if not 0 <= off < len(self.data):
            return None
        s = self.data[off:off + limit].split(b'\0')[0]
        if len(s) >= 3 and all(32 <= c < 127 for c in s):
            return s.decode('latin1')
        return None

    def function_of(self, addr):
        i = bisect.bisect_right(self.funcs, addr) - 1
        return self.funcs[i] if i >= 0 else None

    def function_end(self, start):
        i = bisect.bisect_right(self.funcs, start)
        return self.funcs[i] if i < len(self.funcs) else start + 0x1000


def load_function_starts(path):
    """Function starts from a text list (one hex address per line) or from a ReXGlue
    generated/ directory (DEFINE_REX_FUNC(sub_XXXXXXXX) in the recompiled sources)."""
    starts = set()
    if os.path.isdir(path):
        pattern = re.compile(rb'DEFINE_REX_FUNC\(sub_([0-9A-F]{8})\)')
        for root, _, files in os.walk(path):
            for name in files:
                if name.endswith('.cpp'):
                    with open(os.path.join(root, name), 'rb') as f:
                        starts.update(int(m, 16) for m in pattern.findall(f.read()))
    else:
        with open(path) as f:
            starts.update(int(line, 16) for line in f if line.strip())
    return sorted(starts)
