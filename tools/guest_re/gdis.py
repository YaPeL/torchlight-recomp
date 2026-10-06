#!/usr/bin/env python3
"""Disassemble one guest function (or an address range) from the dumped image.

Usage: gdis.py IMAGE FUNCS START [END]
  IMAGE  image from tools/xex/xex_dump.py
  FUNCS  generated/ directory or a text list of function starts
Annotates lis+addi/lwz pairs with the resulting address, a C string found there,
and for loads the 32-bit word stored at that address.
"""

import re
import sys

import capstone

from guest_image import GuestImage

IMM = r'(-?0x[0-9a-f]+|-?\d+)'


def disassemble(img, start, end=None, out=print):
    end = end or img.function_end(start)
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    hi = {}
    for ins in md.disasm(img.bytes_at(start, end), start):
        note = ''
        m = re.match(r'r(\d+), ' + IMM + '$', ins.op_str)
        if ins.mnemonic == 'lis' and m:
            hi[m.group(1)] = (int(m.group(2), 0) << 16) & 0xFFFFFFFF
        else:
            m = re.match(r'r(\d+), (?:r(\d+), ' + IMM + r'|' + IMM + r'\(r(\d+)\))', ins.op_str)
            if m and ins.mnemonic in ('addi', 'lwz', 'lfs', 'stw', 'lbz', 'lhz'):
                dst = m.group(1)
                src = m.group(2) or m.group(5)
                imm = int(m.group(3) or m.group(4), 0)
                if src in hi:
                    addr = (hi[src] + imm) & 0xFFFFFFFF
                    s = img.cstr(addr)
                    note = f'  ; ={addr:#x}' + (f' "{s}"' if s else '')
                    if ins.mnemonic == 'lwz' and img.cstr(addr) is None:
                        try:
                            note += f' [{img.u32(addr):#x}]'
                        except Exception:
                            pass
                if ins.mnemonic == 'addi' and src == dst and src in hi:
                    hi[dst] = (hi[src] + imm) & 0xFFFFFFFF
                elif ins.mnemonic != 'stw':
                    hi.pop(dst, None)
        out(f'{ins.address:08X}  {ins.mnemonic:8s}{ins.op_str}{note}')


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    img = GuestImage(sys.argv[1], sys.argv[2])
    start = int(sys.argv[3], 16)
    end = int(sys.argv[4], 16) if len(sys.argv) == 5 else None
    if start not in img.funcs:
        f = img.function_of(start)
        print(f'# not a function start; inside sub_{f:08X}' if f else '# outside known functions')
    disassemble(img, start, end)


if __name__ == '__main__':
    main()
