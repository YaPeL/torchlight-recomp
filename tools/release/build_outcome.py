#!/usr/bin/env python3
"""Prints a game build log's outcome for a public CI log (release-check.yml).

The log itself stays on the runner: it holds the build's paths and may hold the generated code's
diagnostics. This prints only what tells whether the build passed:

- the binding check's summary and problems (tools/android/check_guest_call_binding.py), the
  binary named without its folder;
- how many translation units reported mismatched profile data (the count is the match check's);
- ninja's FAILED targets and the compilers' and linkers' error lines, folders stripped from their
  file names, at most 20 of each (never the command lines or the source lines under them);
- ninja's last progress line.

Usage: build_outcome.py BUILD_LOG
"""

import os
import re
import sys

SUMMARY = re.compile(r"^(\S+): (\d+ guest functions, \d+ hooked, \d+ direct branches to guest "
                     r"functions)$")
PROBLEM = re.compile(r"^(bypass|plt|alias|missing|vacuous): \d+$")
OUT_OF_DATE = re.compile(r"profile data may be out of date")
FAILED = re.compile(r"^FAILED: (.*)$")
# "path/file.cpp:12:3: error: message", "ld: error: message", "LINK : fatal error LNK1120: ..."
ERROR = re.compile(r"^(?:(\S+?):(?:\d+:)*\s*)?(?:fatal )?error\b:?\s*(.*)$")
PROGRESS = re.compile(r"^\[\d+/\d+\] ")
SHOWN = 20


def base(path):
    return os.path.basename(path.replace("\\", "/"))


def outcome(lines):
    out, failed, errors = [], [], []
    out_of_date, progress, in_problem = 0, "", False
    for line in lines:
        line = line.rstrip("\r\n")
        m = SUMMARY.match(line)
        if m:
            out.append(f"{base(m.group(1))}: {m.group(2)}")
            in_problem = False
            continue
        if PROBLEM.match(line):
            out.append(line)
            in_problem = True
            continue
        if in_problem and line.startswith("  "):
            out.append(line)
            continue
        in_problem = False
        if OUT_OF_DATE.search(line):
            out_of_date += 1
            continue
        m = FAILED.match(line)
        if m:
            failed.append(f"FAILED: {base(m.group(1).split()[0]) if m.group(1).split() else ''}")
            continue
        m = ERROR.match(line)
        if m and not line.startswith(" "):
            where = f"{base(m.group(1))}: " if m.group(1) else ""
            errors.append(f"{where}error: {m.group(2)}")
            continue
        if PROGRESS.match(line):
            progress = line
    if out_of_date:
        out.append(f"{out_of_date} translation units reported mismatched profile data")
    out += failed[:SHOWN] + errors[:SHOWN]
    if len(failed) > SHOWN or len(errors) > SHOWN:
        out.append(f"({len(failed)} failed targets and {len(errors)} errors in all)")
    if progress:
        out.append(progress.split(" ", 1)[0] + " (ninja's last step)")
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    with open(sys.argv[1], encoding="utf-8", errors="replace") as f:
        for line in outcome(f):
            print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
