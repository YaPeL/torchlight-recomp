#!/usr/bin/env python3
"""Dump the decrypted, decompressed image of a retail XEX2 (Normal/LZX compression).

Usage: xex_dump.py <input.xex> <output.bin>   (needs pycryptodome)

The output is the raw image as loaded at the XEX base address, so
guest address A lives at file offset A - base.
"""

import struct
import sys

from Crypto.Cipher import AES

from lzx_decompress import LZXDecoder

XEX2_RETAIL_KEY = bytes([
    0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
    0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91,
])
AES_BLANK_IV = b'\x00' * 16
SEC_AES_KEY_OFFSET = 0x150   # security info: file key, encrypted with the retail key
SEC_IMAGE_SIZE_OFFSET = 0x4
OPT_FILE_FORMAT_INFO = 0x000003
OPT_IMAGE_BASE = 0x000102
ENCRYPTION_NORMAL = 1
COMPRESSION_NORMAL = 2


def be32(d, o):
    return struct.unpack_from('>I', d, o)[0]


def be16(d, o):
    return struct.unpack_from('>H', d, o)[0]


def dump(xex):
    if xex[:4] != b'XEX2':
        raise ValueError('not a XEX2 file')
    pe_offset = be32(xex, 8)
    sec_info = be32(xex, 16)
    ffi = base = 0
    for i in range(be32(xex, 20)):
        key, value = be32(xex, 24 + i * 8), be32(xex, 28 + i * 8)
        if key >> 8 == OPT_FILE_FORMAT_INFO:
            ffi = value
        elif key >> 8 == OPT_IMAGE_BASE:
            base = value
    if be16(xex, ffi + 6) != COMPRESSION_NORMAL:
        raise ValueError('only Normal (LZX) compression is supported')
    image_size = be32(xex, sec_info + SEC_IMAGE_SIZE_OFFSET)

    data = xex[pe_offset:]
    data += b'\x00' * ((16 - len(data) % 16) % 16)
    if be16(xex, ffi + 4) == ENCRYPTION_NORMAL:
        enc_key = xex[sec_info + SEC_AES_KEY_OFFSET:sec_info + SEC_AES_KEY_OFFSET + 16]
        file_key = AES.new(XEX2_RETAIL_KEY, AES.MODE_CBC, AES_BLANK_IV).decrypt(enc_key)
        data = AES.new(file_key, AES.MODE_CBC, AES_BLANK_IV).decrypt(data)

    # The block chain lives in the data itself: each block starts with
    # {next_block_size(4), next_block_sha1(20)} followed by {u16 size, bytes}
    # chunks up to a zero size. The file format info only holds the first
    # block's size. The concatenated chunks form a single LZX stream.
    window_size = be32(xex, ffi + 8)
    block_size = be32(xex, ffi + 12)
    stream = bytearray()
    pos = 0
    while block_size:
        block = data[pos:pos + block_size]
        next_size = be32(block, 0)
        q = 24
        while True:
            chunk = be16(block, q)
            q += 2
            if chunk == 0:
                break
            stream += block[q:q + chunk]
            q += chunk
        pos += block_size
        block_size = next_size

    window_bits = window_size.bit_length() - 1
    return base, LZXDecoder(window_bits).decompress(bytes(stream), image_size)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    with open(sys.argv[1], 'rb') as f:
        base, image = dump(f.read())
    if image[:2] != b'MZ':
        sys.exit('decompressed image has no MZ header')
    with open(sys.argv[2], 'wb') as f:
        f.write(image)
    print(f'wrote {len(image)} bytes, base 0x{base:08X}')


if __name__ == '__main__':
    main()
