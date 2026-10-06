#!/usr/bin/env python3
"""Find guest code that materialises a 32-bit constant (lis + addi/ori/load/store) or that
calls an address (bl).

Usage: xref.py IMAGE FUNCS {const|call} ADDR [ADDR ...]
  IMAGE  image from tools/xex/xex_dump.py
  FUNCS  generated/ directory or a text list of function starts
Register tracking is reset at each function start, so a hit is reported with the
function containing it.
"""

import struct
import sys

from guest_image import BASE, GuestImage

CODE_END = 0x83027448  # end of the executable range in this XEX
ADDI, ORI, LIS = 14, 24, 15
MEMORY_OPS = {32, 34, 40, 36, 38, 44, 48, 52}  # lwz lbz lhz stw stb sth lfs stfs


def scan(img):
    consts, calls = {}, {}
    start = img.funcs[0]
    count = (CODE_END - start) // 4
    words = struct.unpack_from('>%dI' % count, img.data, start - BASE)
    hi = [None] * 32
    fi = 0
    for k, w in enumerate(words):
        addr = start + 4 * k
        if fi + 1 < len(img.funcs) and addr >= img.funcs[fi + 1]:
            while fi + 1 < len(img.funcs) and addr >= img.funcs[fi + 1]:
                fi += 1
            hi = [None] * 32
        op = w >> 26
        rd = (w >> 21) & 31
        ra = (w >> 16) & 31
        imm = w & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == LIS and ra == 0:
            hi[rd] = (imm << 16) & 0xFFFFFFFF
        elif op == 18 and w & 1:  # bl
            target = (addr + (((w & 0x3FFFFFC) ^ 0x2000000) - 0x2000000)) & 0xFFFFFFFF
            calls.setdefault(target, []).append(addr)
        elif (op in (ADDI, ORI) or op in MEMORY_OPS) and hi[ra] is not None:
            value = (hi[ra] + (imm if op == ORI else simm)) & 0xFFFFFFFF
            consts.setdefault(value, []).append(addr)
            if op in (ADDI, ORI, 32, 34, 40) and rd != ra:
                hi[rd] = None
        elif op in (ADDI, ORI, 32, 34, 40, 21):
            hi[rd] = None
    return consts, calls


def main():
    if len(sys.argv) < 5 or sys.argv[3] not in ('const', 'call'):
        sys.exit(__doc__)
    img = GuestImage(sys.argv[1], sys.argv[2])
    consts, calls = scan(img)
    table = consts if sys.argv[3] == 'const' else calls
    for arg in sys.argv[4:]:
        value = int(arg, 16)
        refs = table.get(value, [])
        print(f'{value:#010x} [{len(refs)}]:',
              ' '.join(f'{r:#x}(sub_{img.function_of(r):08X})' for r in refs))


if __name__ == '__main__':
    main()
