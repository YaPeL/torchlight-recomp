#!/usr/bin/env python3
"""Checks that a profile was taken inside the fight of a benchmark run.

Usage: profile_window.py LOG START END [--step fight] [--open-end]

LOG is the game's log of the run; START and END are when the profile began and ended, as the
device's clock gives them (`date +%s.%N` on the device, seconds since the epoch). The log writes the
device's local time without a zone, read here in this machine's zone: the two must match. A mismatch
shifts every window by whole half hours, so it fails (never passes by mistake) with a large offset
"from the mark". The fight is the time between the input script's "perf step N: fight" mark and its
"perf step 'fight':" summary (with --open-end, for a run stopped before the summary, the log's last
line). Prints the mark, the end and the profile's window, and fails (exit 1) unless the window lies
entirely inside the fight.

Written after profiles of 2026-10-21/22 started on the rest mark, 20 s before the fight, because
the run scripts' `adb shell grep -q "perf step 1" LOG` lost its quotes on the device and matched
"perf" alone (docs/regressions.md).
"""

import argparse
import datetime
import re
import sys

LINE_TIME = re.compile(r"^\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\]")


def line_time(line):
    m = LINE_TIME.match(line)
    if not m:
        return None
    return datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f").timestamp()


def fight_window(lines, step, open_end=False):
    """(start, end) of the step in the log's own clock, or None for a missing mark."""
    start = end = last = None
    start_mark = re.compile(r"perf step \d+: " + re.escape(step) + r"$")
    end_mark = "perf step '" + step + "':"
    for line in lines:
        text = line.rstrip("\n")
        if line_time(text) is not None:
            last = line_time(text)
        if start is None and start_mark.search(text):
            start = line_time(text)
        elif start is not None and end_mark in text:
            end = line_time(text)
            break
    if start is not None and end is None and open_end:
        end = last
    if start is None or end is None:
        return None
    return start, end


def check(lines, start, end, step="fight", open_end=False):
    """(ok, message) for a profile from `start` to `end` against the step in the log."""
    window = fight_window(lines, step, open_end)
    if window is None:
        return False, f"no complete '{step}' step in the log"
    fight_start, fight_end = window
    msg = (f"{step}: {fight_start:.3f} .. {fight_end:.3f}; profile: {start:.3f} .. {end:.3f} "
           f"({start - fight_start:+.1f} s .. {end - fight_start:+.1f} s from the mark)")
    if end <= start:
        return False, msg + " -- the profile ends before it starts"
    if start < fight_start or end > fight_end:
        return False, msg + " -- the profile is not entirely inside the fight"
    return True, msg


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("log")
    ap.add_argument("start", type=float)
    ap.add_argument("end", type=float)
    ap.add_argument("--step", default="fight")
    ap.add_argument("--open-end", action="store_true",
                    help="the run was stopped before the step's summary: end at the last line")
    a = ap.parse_args()
    try:
        with open(a.log, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        print(f"error: {e}")
        return 1
    ok, msg = check(lines, a.start, a.end, a.step, a.open_end)
    print(("ok: " if ok else "error: ") + msg)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
