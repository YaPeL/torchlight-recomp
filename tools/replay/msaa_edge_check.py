#!/usr/bin/env python3
"""Checks that multisampling changed only the edges: the same frame replayed without and with
MSAA (replay --msaa N) must differ only where surfaces meet.

Usage: msaa_edge_check.py OFF.png MSAA.png [--radius R] [--tolerance D] [--max-out-of-range F]

A pixel changed when a channel differs by more than D (default 4, of 255). A resolve averages the
samples of the surfaces that cover the pixel, so:
- a changed pixel where the image without MSAA is flat (every channel within D over the pixel's
  neighbourhood of radius R, default 2) is a failure when it changed by more than 2D or has a
  changed neighbour: nothing meets there (a resolve at the wrong time, a sample count read as
  pixels, a pass that blends the wrong target). A lone pixel changed by at most 2D is geometry
  thinner than a pixel over a surface of nearly its colour, and is only counted;
- a changed pixel should lie within its neighbourhood's range in the image without MSAA (within
  D). Geometry thinner than a pixel, which no pixel centre hit, can bring a colour of its own; at
  most F of the changed pixels (default 0.01) may do that.
Prints the counts; exit 1 on a failure, when nothing changed at all (multisampling did not apply),
or when the out-of-range share passes F; 2 on bad input. No dependencies: reads the replay's PNGs
with check_replay_output.png_pixels.
"""

import argparse
import importlib.util
import pathlib
import sys

_reader = importlib.util.spec_from_file_location(
    "check_replay_output", pathlib.Path(__file__).with_name("check_replay_output.py"))
check_replay_output = importlib.util.module_from_spec(_reader)
_reader.loader.exec_module(check_replay_output)


def read_rgb(path):
    """(width, height, rows): each row a list of (r, g, b)."""
    rows = []
    for channels, row in check_replay_output.png_pixels(path):
        rows.append([tuple(row[i:i + 3]) for i in range(0, len(row), channels)])
    return (len(rows[0]) if rows else 0), len(rows), rows


def check(off, msaa, width, height, radius, tolerance):
    """(changed, flat failures, lone small flat changes, out_of_range, first flat failure)."""
    changed = flat = lone = out_of_range = 0
    first_flat = None
    for y in range(height):
        for x in range(width):
            p, q = msaa[y][x], off[y][x]
            if max(abs(p[c] - q[c]) for c in range(3)) <= tolerance:
                continue
            changed += 1
            ys = range(max(0, y - radius), min(height, y + radius + 1))
            xs = range(max(0, x - radius), min(width, x + radius + 1))
            is_flat, in_range = True, True
            for c in range(3):
                values = [off[yy][xx][c] for yy in ys for xx in xs]
                lo, hi = min(values), max(values)
                if hi - lo > tolerance:
                    is_flat = False
                if p[c] < lo - tolerance or p[c] > hi + tolerance:
                    in_range = False
            if is_flat:
                big = max(abs(p[c] - q[c]) for c in range(3)) > 2 * tolerance
                neighbours = any(
                    max(abs(msaa[yy][xx][c] - off[yy][xx][c]) for c in range(3)) > tolerance
                    for yy in range(max(0, y - 1), min(height, y + 2))
                    for xx in range(max(0, x - 1), min(width, x + 2)) if (xx, yy) != (x, y))
                if big or neighbours:
                    flat += 1
                    first_flat = first_flat or (x, y)
                else:
                    lone += 1
            elif not in_range:
                out_of_range += 1
    return changed, flat, lone, out_of_range, first_flat


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("off")
    parser.add_argument("msaa")
    parser.add_argument("--radius", type=int, default=2)
    parser.add_argument("--tolerance", type=int, default=4)
    parser.add_argument("--max-out-of-range", type=float, default=0.01)
    args = parser.parse_args()
    try:
        w, h, off = read_rgb(args.off)
        w2, h2, msaa = read_rgb(args.msaa)
    except (OSError, ValueError) as error:
        print(f"msaa_edge_check: {error}", file=sys.stderr)
        return 2
    if (w, h) != (w2, h2) or not w or not h:
        print(f"msaa_edge_check: sizes differ ({w}x{h}, {w2}x{h2})", file=sys.stderr)
        return 2
    changed, flat, lone, out_of_range, first_flat = check(off, msaa, w, h, args.radius,
                                                          args.tolerance)
    share = out_of_range / changed if changed else 0.0
    print(f"msaa_edge_check: {w}x{h}, {changed} changed, {flat} in flat areas"
          + (f" (first at {first_flat[0]},{first_flat[1]})" if first_flat else "")
          + f", {lone} lone small ones"
          + f", {out_of_range} outside their neighbourhood's range ({share:.2%}, "
          f"limit {args.max_out_of_range:.2%})")
    if not changed:
        print("msaa_edge_check: nothing changed: multisampling did not apply")
        return 1
    return 1 if flat or share > args.max_out_of_range else 0


if __name__ == "__main__":
    sys.exit(main())
