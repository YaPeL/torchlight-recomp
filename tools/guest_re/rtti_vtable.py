#!/usr/bin/env python3
"""Find a guest class's vtables through its MSVC RTTI, and the code that installs them.

Usage: rtti_vtable.py IMAGE FUNCS CLASS [--anchor STRING ...] [--dump]
  IMAGE   image from tools/xex/xex_dump.py
  FUNCS   generated/ directory or a text list of function starts
  CLASS   C++ name (Ogre::D3D9RenderSystem) or RTTI name (.?AVD3D9RenderSystem@Ogre@@)
  --anchor  a string the class's methods use (e.g. "D3D9RenderSystem::_beginFrame"): reports
            the functions that build its address and their slots in the primary vtable
  --dump  prints every slot of each vtable

RTTI layout (32-bit MSVC, big-endian guest): the type descriptor is 8 bytes before the name;
a Complete Object Locator is {signature 0, offset of this vtable in the object, ctor
displacement, type descriptor, class hierarchy}; the vtable starts right after the word that
points to its locator. One locator per base subobject: offset 0 is the primary vtable.
"""

import argparse
import struct

from guest_image import BASE, GuestImage

ADDI, ORI, LIS, STW = 14, 24, 15, 36


def rtti_name(cls):
    if cls.startswith('.?AV') or cls.startswith('.?AU'):
        return cls.encode() + b'\0'
    parts = cls.split('::')
    return ('.?AV' + '@'.join(reversed(parts)) + '@@').encode() + b'\0'


def text_range(img):
    """Executable range from the embedded PE header (the image is in runtime layout)."""
    pe = struct.unpack_from('<I', img.data, 0x3C)[0]
    sections = struct.unpack_from('<H', img.data, pe + 6)[0]
    table = pe + 24 + struct.unpack_from('<H', img.data, pe + 20)[0]
    for i in range(sections):
        off = table + i * 40
        if img.data[off:off + 8].rstrip(b'\0') == b'.text':
            size, rva = struct.unpack_from('<II', img.data, off + 8)
            return BASE + rva, BASE + rva + size
    raise SystemExit('the image has no .text section')


def word_refs(img, value):
    needle = struct.pack('>I', value)
    out, i = [], img.data.find(needle)
    while i >= 0:
        if i % 4 == 0:
            out.append(BASE + i)
        i = img.data.find(needle, i + 1)
    return out


def locators(img, type_descriptor):
    """(locator address, offset of the vtable in the object) for every locator of the type."""
    out = []
    for ref in word_refs(img, type_descriptor):
        col = ref - 12
        if col < BASE:
            continue
        signature, offset, _ = struct.unpack_from('>III', img.data, col - BASE)
        hierarchy = img.u32(ref + 4)
        if signature == 0 and BASE <= hierarchy < BASE + len(img.data):
            out.append((col, offset))
    return out


def vtable_entries(img, vtable, code):
    entries, addr = [], vtable
    while addr + 4 <= BASE + len(img.data):
        target = img.u32(addr)
        if not (code[0] <= target < code[1] and target % 4 == 0):
            break
        entries.append(target)
        addr += 4
    return entries


def materializations(img, code, target, lookahead=16):
    """(instruction, register) where lis + addi/ori builds `target`."""
    out = []
    start, end = code[0] - BASE, min(code[1] - BASE, len(img.data))
    words = struct.unpack_from('>%dI' % ((end - start) // 4), img.data, start)
    for k, w in enumerate(words):
        if w >> 26 != LIS or (w >> 16) & 31:
            continue
        reg, high = (w >> 21) & 31, w & 0xFFFF
        shigh = high - 0x10000 if high & 0x8000 else high
        for d in range(1, lookahead + 1):
            if k + d >= len(words):
                break
            u = words[k + d]
            op, dst, src = u >> 26, (u >> 21) & 31, (u >> 16) & 31
            value = None
            if op == ADDI and src == reg:
                low = u & 0xFFFF
                value = ((shigh << 16) + (low - 0x10000 if low & 0x8000 else low)) & 0xFFFFFFFF
            elif op == ORI and dst == reg:
                value, dst = (high << 16) | (u & 0xFFFF), src
            if value == target:
                out.append((code[0] + 4 * (k + d), dst))
            if dst == reg and op in {7, 8, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29}:
                break
    return out


def vptr_store(img, instruction, reg):
    """A `stw reg, N(obj)` within 6 instructions of the address being built."""
    for addr in range(instruction + 4, instruction + 4 + 6 * 4, 4):
        u = img.u32(addr)
        if u >> 26 == STW and (u >> 21) & 31 == reg:
            disp = u & 0xFFFF
            return addr, disp - 0x10000 if disp & 0x8000 else disp
    return None


def name(img, addr):
    f = img.function_of(addr)
    return 'sub_%08X' % f if f is not None else 'unknown function'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('image')
    p.add_argument('funcs')
    p.add_argument('cls')
    p.add_argument('--anchor', action='append', default=[])
    p.add_argument('--dump', action='store_true')
    a = p.parse_args()

    img = GuestImage(a.image, a.funcs)
    code = text_range(img)
    rname = rtti_name(a.cls)
    i = img.data.find(rname)
    if i < 8:
        raise SystemExit('RTTI name %s not found' % rname[:-1].decode())
    descriptor = BASE + i - 8
    print('RTTI name            0x%08X  %s' % (BASE + i, rname[:-1].decode()))
    print('type descriptor      0x%08X' % descriptor)

    primary = []
    for col, offset in sorted(locators(img, descriptor), key=lambda c: c[1]):
        for ref in word_refs(img, col):
            vtable = ref + 4
            entries = vtable_entries(img, vtable, code)
            if not entries:
                continue
            print('vtable               0x%08X  (object offset %d, locator 0x%08X, %d slots)'
                  % (vtable, offset, col, len(entries)))
            if offset == 0:
                primary = entries
            for instruction, reg in materializations(img, code, vtable):
                store = vptr_store(img, instruction, reg)
                if store:
                    print('  installed at       0x%08X  stw +%d in %s'
                          % (store[0], store[1], name(img, instruction)))
            if a.dump:
                for k, target in enumerate(entries):
                    print('  [%3d] +0x%03X  0x%08X' % (k, k * 4, target))

    for anchor in a.anchor:
        j = img.data.find(anchor.encode() + b'\0')
        if j < 0:
            print('anchor "%s" not found' % anchor)
            continue
        print('anchor "%s" at 0x%08X' % (anchor, BASE + j))
        for instruction, _ in materializations(img, code, BASE + j):
            f = img.function_of(instruction)
            slots = [k for k, t in enumerate(primary) if t == f]
            print('  used at 0x%08X in %s%s' % (instruction, name(img, instruction),
                  ''.join(' -> primary slot %d (+0x%X)' % (s, s * 4) for s in slots)))


if __name__ == '__main__':
    main()
