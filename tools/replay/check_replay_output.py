#!/usr/bin/env python3
"""Checks a replay's output for what a GLES render system hides: a program that does not link
draws nothing, silently, and the replay still writes its image and exits 0.

Usage: check_replay_output.py OGRE_LOG IMAGE.png

Fails (exit 1) when OGRE's log reports a program that did not compile or link, or when the image
is black (no pixel brighter than 16 of 255 in any channel). docs/regressions.md, "develop never
drew on Android". No dependencies: reads 8-bit RGB/RGBA PNGs, non-interlaced, as the replay
writes them.
"""

import struct
import sys
import zlib

# OGRE's GLSL and GLSL ES logs for a program that will draw nothing.
LOG_FAILURES = (
    "Link failed",
    "compile log:",
    "Compile error",
    "The program object is incomplete",
)


def png_pixels(path):
    """Yields each row of an 8-bit RGB or RGBA non-interlaced PNG as bytes (filters undone)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width, height, channels = 8, b"", 0, 0, 0
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
            if depth != 8 or color not in (2, 6) or interlace:
                raise ValueError(f"{path}: only 8-bit RGB/RGBA non-interlaced PNGs")
            channels = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * channels
    previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            if kind == 1:
                row[i] = (row[i] + left) & 0xFF
            elif kind == 2:
                row[i] = (row[i] + up) & 0xFF
            elif kind == 3:
                row[i] = (row[i] + ((left + up) >> 1)) & 0xFF
            elif kind == 4:
                upleft = previous[i - channels] if i >= channels else 0
                p = left + up - upleft
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - upleft)
                pred = left if pa <= pb and pa <= pc else (up if pb <= pc else upleft)
                row[i] = (row[i] + pred) & 0xFF
        previous = row
        yield channels, bytes(row)


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    log_path, image_path = sys.argv[1], sys.argv[2]
    ok = True
    with open(log_path, encoding="utf-8", errors="replace") as f:
        log = f.read()
    for failure in LOG_FAILURES:
        count = log.count(failure)
        if count:
            print(f"error: OGRE's log reports {count} x '{failure}' ({log_path})")
            ok = False
    try:
        rows = list(png_pixels(image_path))
    except (OSError, ValueError) as e:
        print(f"error: no image to check: {e}")
        return 1
    lit = 0
    for channels, row in rows:
        for i in range(0, len(row), channels):
            if max(row[i], row[i + 1], row[i + 2]) > 16:
                lit += 1
    if lit == 0:
        print(f"error: the image is black ({image_path})")
        ok = False
    else:
        print(f"{image_path}: {lit} pixels brighter than 16")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
