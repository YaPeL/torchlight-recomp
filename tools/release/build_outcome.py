#!/usr/bin/env python3
"""Prints a game build log's outcome for a public CI log (release-check.yml).

The log itself stays on the runner: it holds the build's paths and may hold the generated code's
diagnostics. This prints only what tells whether the build passed:

- the binding check's summary and problems (tools/android/check_guest_call_binding.py), the
  binary named without its folder;
- how many translation units reported mismatched profile data (the count is the match check's);
- ninja's FAILED targets and the compilers' and linkers' error lines, folders stripped from their
  leading file names, at most 20 of each (never the command lines or the source lines under them);
  an error's message is kept as it is, so it may name runner paths or generated functions
  (release.yml prints the whole log);
- the binding check's own failure line (check_guest_call_binding: ...);
- ninja's last progress line.

Usage: build_outcome.py BUILD_LOG
"""

import os
import re
import sys

SUMMARY = re.compile(r"^(.+): (\d+ guest functions, \d+ hooked, \d+ direct branches to guest "
                     r"functions)$")
PROBLEM = re.compile(r"^(bypass|plt|alias|missing|vacuous): \d+$")
TOOL_ERROR = re.compile(r"^check_guest_call_binding: ")
OUT_OF_DATE = re.compile(r"profile data may be out of date")
# ninja 1.13 and later: "FAILED: [code=1] target".
FAILED = re.compile(r"^FAILED: (?:\[code=\d+\] )?(\S*)")
# "path/file.cpp:12:3: error: message", "C:\Program Files\x.h(3): error: message",
# "ld: error: message", "LINK : fatal error LNK1120: message", "a.obj : error LNK2019: message"
# The file part stops at its first colon (a drive letter aside), so a warning or note that only
# mentions an error further on is not one. The output is normalized ("error: message").
ERROR = re.compile(r"^(?:((?:[A-Za-z]:)?[^:]+?)(?:\(\d+(?:,\d+)?\)|(?::\d+)+)?\s?:\s*)?"
                   r"((?:fatal )?error)\b:?\s*(.*)$")
PROGRESS = re.compile(r"^\[\d+/\d+\] ")
SHOWN = 20


def base(path):
    return os.path.basename(path.replace("\\", "/"))


def outcome(lines):
    out, failed, errors = [], [], []
    out_of_date, out_of_date_errors, progress, in_problem = 0, 0, "", False
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
        if TOOL_ERROR.match(line):
            out.append(line)
            continue
        if OUT_OF_DATE.search(line):
            if "-Werror" in line or ": error:" in line:
                out_of_date_errors += 1
            else:
                out_of_date += 1
            continue
        m = FAILED.match(line)
        if m:
            failed.append(f"FAILED: {base(m.group(1))}")
            continue
        m = ERROR.match(line)
        if m and not line.startswith((" ", "-- ")):  # source lines, CMake's status lines
            where = f"{base(m.group(1))}: " if m.group(1) else ""
            errors.append(f"{where}{m.group(2)}: {m.group(3)}")
            continue
        if PROGRESS.match(line):
            progress = line
    if out_of_date:
        out.append(f"{out_of_date} translation units warned of mismatched profile data")
    if out_of_date_errors:
        out.append(f"{out_of_date_errors} translation units failed on mismatched profile data "
                   "(generated code: the profile no longer matches)")
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
