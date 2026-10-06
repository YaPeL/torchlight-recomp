#!/usr/bin/env python3
"""Rank guest functions that may correspond to a function of the PC executable, by the string
literals both reference (their own and their direct callees'), weighted by rarity.

Usage: pc_match.py PC_EXE IMAGE FUNCS PC_ADDR [PC_ADDR ...] [--top N]
  PC_EXE   reference/pc/Torchlight.exe (read only; disassembled with objdump, nothing written)
  IMAGE    guest image from tools/xex/xex_dump.py
  FUNCS    generated/ directory or a text list of guest function starts
  PC_ADDR  any address inside the PC function (its start is found by the heuristic below)

PC function starts are direct call targets plus addresses right after int3 padding; a
function ends at the next start. PC strings are ASCII (>= 4 chars) or UTF-16LE (>= 3 chars)
in .rdata/.data referenced by an immediate; guest strings are the constants xref.py sees
materialised (lis + addi/ori), read as ASCII or UTF-16BE. The score is the sum, over shared
strings, of 2 / log2(2 + uses) for the function's own strings and 1 / log2(2 + uses) for its
callees', where uses is how many guest functions reference the string. A ranking is a lead, not
evidence: confirm every match in the code before citing it.
"""

import math
import re
import struct
import subprocess
import sys
from bisect import bisect_right
from collections import defaultdict

from guest_image import BASE, GuestImage
from xref import scan

PC_BASE = 0x400000


class PcImage:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        pe = struct.unpack_from('<I', self.data, 0x3C)[0]
        count = struct.unpack_from('<H', self.data, pe + 6)[0]
        optional = struct.unpack_from('<H', self.data, pe + 20)[0]
        self.sections = []
        for i in range(count):
            o = pe + 24 + optional + 40 * i
            name = self.data[o:o + 8].rstrip(b'\0').decode('latin1')
            vsize, va, rsize, raw = struct.unpack_from('<IIII', self.data, o + 8)
            self.sections.append((name, PC_BASE + va, vsize, raw, rsize))
        self._load_code(path)

    def _load_code(self, path):
        out = subprocess.run(['objdump', '-d', '-Mintel', '--no-show-raw-insn', path],
                             capture_output=True, text=True, check=True).stdout
        line = re.compile(r'\s+([0-9a-f]+):\s+(.*)')
        self.code = []
        for text in out.split('\n'):
            m = line.match(text)
            if m:
                self.code.append((int(m.group(1), 16), m.group(2).strip()))
        self.addrs = [a for a, _ in self.code]
        starts = set()
        for i, (addr, ins) in enumerate(self.code):
            m = re.match(r'call\s+0x([0-9a-f]+)$', ins)
            if m:
                starts.add(int(m.group(1), 16))
            if ins.startswith('int3') and i + 1 < len(self.code) and \
                    not self.code[i + 1][1].startswith('int3'):
                starts.add(self.code[i + 1][0])
        self.starts = sorted(starts)

    def read(self, va, length):
        for name, start, vsize, raw, rsize in self.sections:
            if start <= va < start + min(vsize, rsize):
                off = raw + va - start
                return self.data[off:off + length]
        return None

    def is_data(self, va):
        return any(name in ('.rdata', '.data') and start <= va < start + vsize
                   for name, start, vsize, _, _ in self.sections)

    def string(self, va):
        if not self.is_data(va):
            return None
        raw = self.read(va, 160)
        if not raw:
            return None
        ascii_text = raw.split(b'\0')[0]
        if len(ascii_text) >= 4 and all(32 <= c < 127 for c in ascii_text):
            return ascii_text.decode('latin1')
        units = []
        for k in range(0, len(raw) - 1, 2):
            u = raw[k] | raw[k + 1] << 8
            if u == 0:
                break
            units.append(u)
        if len(units) >= 3 and all(32 <= u < 127 for u in units):
            return ''.join(map(chr, units))
        return None

    def function(self, addr):
        i = bisect_right(self.starts, addr) - 1
        start = self.starts[i]
        end = self.starts[i + 1] if i + 1 < len(self.starts) else start + 0x1000
        return start, end

    def features(self, start, end):
        strings, callees = set(), set()
        lo, hi = bisect_right(self.addrs, start - 1), bisect_right(self.addrs, end - 1)
        for _, ins in self.code[lo:hi]:
            m = re.match(r'call\s+0x([0-9a-f]+)$', ins)
            if m:
                callees.add(int(m.group(1), 16))
                continue
            for value in re.findall(r'0x([0-9a-f]{6,8})\b', ins):
                s = self.string(int(value, 16))
                if s:
                    strings.add(s)
        return strings, callees


def guest_string(img, value):
    off = value - BASE
    if not 0 <= off < len(img.data):
        return None
    raw = img.data[off:off + 160]
    ascii_text = raw.split(b'\0')[0]
    if len(ascii_text) >= 4 and all(32 <= c < 127 for c in ascii_text):
        return ascii_text.decode('latin1')
    units = []
    for k in range(0, len(raw) - 1, 2):
        u = raw[k] << 8 | raw[k + 1]
        if u == 0:
            break
        units.append(u)
    if len(units) >= 3 and all(32 <= u < 127 for u in units):
        return ''.join(map(chr, units))
    return None


def guest_features(img):
    consts, calls = scan(img)
    strings, callees = defaultdict(set), defaultdict(set)
    for value, refs in consts.items():
        s = guest_string(img, value)
        if s:
            for r in refs:
                strings[img.function_of(r)].add(s)
    for target, refs in calls.items():
        for r in refs:
            callees[img.function_of(r)].add(target)
    return strings, callees


def main():
    args = [a for a in sys.argv[1:]]
    top = 8
    if '--top' in args:
        i = args.index('--top')
        top = int(args[i + 1])
        del args[i:i + 2]
    if len(args) < 4:
        sys.exit(__doc__)
    pc = PcImage(args[0])
    img = GuestImage(args[1], args[2])
    g_strings, g_callees = guest_features(img)
    uses = defaultdict(int)
    for s in g_strings.values():
        for text in s:
            uses[text] += 1

    def weight(text):
        return 1.0 / math.log2(2 + uses[text])

    def callee_strings(strings_of, callees):
        out = set()
        for c in callees:
            out |= strings_of(c)
        return out

    pc_cache = {}

    def pc_strings(addr):
        if addr not in pc_cache:
            pc_cache[addr] = pc.features(*pc.function(addr))
        return pc_cache[addr][0]

    for arg in args[3:]:
        start, end = pc.function(int(arg, 16))
        own, callees = pc.features(start, end)
        around = callee_strings(pc_strings, callees) - own
        print(f'PC {start:#x}..{end:#x}: {len(own)} strings, {len(around)} callee strings')
        scores = []
        for func in set(g_strings) | set(g_callees):
            g_own = g_strings.get(func, set())
            g_around = callee_strings(lambda f: g_strings.get(f, set()),
                                      g_callees.get(func, ())) - g_own
            shared = own & g_own
            shared_around = (own | around) & (g_own | g_around) - shared
            score = sum(2 * weight(t) for t in shared) + sum(weight(t) for t in shared_around)
            if score > 0:
                scores.append((score, func, shared, shared_around))
        scores.sort(key=lambda x: -x[0])
        for score, func, shared, shared_around in scores[:top]:
            names = sorted(shared)[:6] + ['~' + t for t in sorted(shared_around)[:4]]
            print(f'  {score:6.2f} sub_{func:08X}  {", ".join(names)}')


if __name__ == '__main__':
    main()
