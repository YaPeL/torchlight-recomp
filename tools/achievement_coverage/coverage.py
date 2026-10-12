#!/usr/bin/env python3
"""The PC achievements' coverage counted from the rows of docs/achievement-coverage.md.

The document's summary line (the one starting with "**Counted from the rows") is what this
script prints; --check fails when the document's line says something else, so the summary
cannot drift from the rows. Each row's implementation status is its third column; its
real-game column (the fifth) starts with "Unlocked" (unlocked in a run of the real game),
"Producer" (its event or counter seen in a run, the unlock not reached) or anything else (not
seen in a run yet).
"""

import argparse
import sys
from collections import Counter
from pathlib import Path

SUMMARY_PREFIX = "**Counted from the rows"
STATUSES = ("implemented", "partial", "blocked", "out of scope")


def rows(text):
    for line in text.splitlines():
        if line.startswith("| `"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            yield cells


def real_game(cell):
    if cell.startswith("Unlocked"):
        return "unlocked"
    if cell.startswith("Producer"):
        return "producer"
    return "not seen"


def summary(text):
    table = list(rows(text))
    total = len(table)
    status = Counter(r[2] for r in table)
    unknown = set(status) - set(STATUSES)
    if unknown:
        raise ValueError(f"unknown implementation status: {sorted(unknown)}")
    game = Counter(real_game(r[4]) for r in table)
    return (
        f"{SUMMARY_PREFIX} (tools/achievement_coverage/coverage.py): {status['implemented']} "
        f"implemented and unit-tested, {status['partial']} partial, {status['blocked']} blocked, "
        f"{status['out of scope']} out of scope ({total} total). In the real game: "
        f"{game['unlocked']}/{total} unlocked, {game['producer']}/{total} with their event or counter "
        f"seen but not unlocked, {game['not seen']}/{total} not seen yet.**"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("document", type=Path)
    parser.add_argument("--check", action="store_true",
                        help="fail when the document's summary line differs from the rows' count")
    args = parser.parse_args()
    text = args.document.read_text(encoding="utf-8")
    expected = summary(text)
    if not args.check:
        print(expected)
        return 0
    found = [line for line in text.splitlines() if line.startswith(SUMMARY_PREFIX)]
    if found != [expected]:
        print(f"{args.document}: the summary line does not match the rows.\n"
              f"  found:    {found}\n  expected: {expected}", file=sys.stderr)
        return 1
    print("achievement coverage: summary matches the rows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
