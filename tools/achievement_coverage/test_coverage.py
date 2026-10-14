#!/usr/bin/env python3
"""Tests of coverage.py on synthetic tables."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import coverage  # noqa: E402

HEAD = "| PC achievement | Source | Status | Tests | Real game | Lifecycle | Notes |\n|---|---|---|---|---|---|---|\n"


def row(name, status, game):
    return f"| `{name}` | x | {status} | x | {game} | x | x |\n"


failures = 0


def check(ok, what):
    global failures
    if not ok:
        failures += 1
        print(f"FAIL: {what}", file=sys.stderr)


table = HEAD + row("A", "implemented", "Unlocked — a run") + row("B", "implemented", "Producer — a run") \
    + row("C", "partial", "No") + row("D", "implemented", "Pending run")
line = coverage.summary(table)
check("3 implemented and unit-tested, 1 partial, 0 blocked, 0 out of scope (4 total)" in line, "statuses")
check("1/4 unlocked, 1/4 with their event or counter seen but not unlocked, 2/4 not seen yet" in line,
      "real game")
check(line.startswith(coverage.SUMMARY_PREFIX), "the prefix --check looks for")
try:
    coverage.summary(HEAD + row("E", "done", "No"))
    check(False, "an unknown status is refused")
except ValueError:
    pass

# The shipped document's summary matches its rows.
document = Path(__file__).resolve().parents[2] / "docs" / "achievement-coverage.md"
text = document.read_text(encoding="utf-8")
check([l for l in text.splitlines() if l.startswith(coverage.SUMMARY_PREFIX)] == [coverage.summary(text)],
      "docs/achievement-coverage.md: the summary line matches the rows (run coverage.py to get it)")

if failures:
    sys.exit(1)
print("test_coverage: ok")
