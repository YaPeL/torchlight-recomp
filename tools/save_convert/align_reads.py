#!/usr/bin/env python3
"""Find where the game's reads of a save stop matching the converter's schema.

Input: a game log with one line per read the game made while loading the save, and the save
itself. The log lines come from a temporary hook on the guest stream read (see README.md, "Finding
where the game misreads a save"); the schema's reads come from parsing the same file with
character_save.schema.json.

    python3 tools/save_convert/align_reads.py LOG N.TSV [SECTION]

SECTION is the traced guest function whose reads are compared (default sub_8221DC20, the character
loader); when it ran several times, the run with the most reads is used.

A game read matches when it starts on the start of a schema field and ends on the end of one (the
game may read several consecutive fields at once, and reads a string one character at a time).
Reads past the end of the body (the SHA-256) are ignored. Reads the guest makes directly from the
stream buffer are not logged, so a gap between two logged reads is normal; what matters is the
first logged read that lands where the schema has no field boundary.
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import savefile  # noqa: E402

READ = re.compile(r'save-trace: read lr=(\w+) pos=(\d+) size=(\d+)x(\d+) got=(\d+)')


def game_reads(log_path, section):
    """Reads (lr, pos, bytes, got) of the longest run of SECTION in the log."""
    best, current, active = [], [], False
    with open(log_path, errors='replace') as log:
        for line in log:
            if not active and 'save-trace: %s begin' % section in line:
                current, active = [], True
            elif active and 'save-trace: %s end' % section in line:
                active = False
                if len(current) > len(best):
                    best = current
            elif active:
                match = READ.search(line)
                if match:
                    lr, pos, size, count, got = match.groups()
                    current.append((lr, int(pos), int(size) * int(count), int(got)))
    return best


def schema_fields(body):
    """Start offset -> (size, kind) of every field the schema reads; strings per character."""
    parsed = savefile.parse_body(savefile.load_schema(), body, 'big')
    starts = {}
    for offset, size, unit in parsed.tokens:
        if unit == 2 and size > 2:
            for char in range(offset, offset + size, 2):
                starts[char] = (2, 'text')
        else:
            starts[offset] = (size, 'number')
    return starts


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('log')
    parser.add_argument('save')
    parser.add_argument('section', nargs='?', default='sub_8221DC20')
    args = parser.parse_args()

    with open(args.save, 'rb') as f:
        body = savefile.split_360(f.read())
    starts = schema_fields(body)
    ends = {offset + size for offset, (size, _) in starts.items()}
    reads = game_reads(args.log, args.section)
    print('%d game reads in %s, %d schema fields, body of %d bytes'
          % (len(reads), args.section, len(starts), len(body)))

    for index, (lr, pos, size, got) in enumerate(reads):
        if pos >= len(body):
            continue
        if pos in starts and pos + size in ends:
            continue
        print('first divergence at game read #%d: lr=%s pos=%d bytes=%d; schema field here: %s'
              % (index, lr, pos, size, starts.get(pos)))
        print('game reads around it (lr, pos, bytes, got):')
        for read in reads[max(0, index - 8):index + 3]:
            print('   ', read)
        near = sorted(offset for offset in starts if pos - 40 <= offset <= pos + 40)
        print('schema fields nearby (offset, size, kind):')
        print('   ', [(offset,) + starts[offset] for offset in near])
        return 1
    print('no divergence: every game read lands on schema field boundaries')
    return 0


if __name__ == '__main__':
    sys.exit(main())
