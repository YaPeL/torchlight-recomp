#!/usr/bin/env python3
"""Fails a PGO build whose profile no longer matches the code (docs/perf/pgo.md).

clang reports, per translation unit compiled with -fprofile-instr-use, how many of its functions
have profile data that no longer matches their code and is ignored:

  warning: profile data may be out of date: of 5120 functions, 37 have mismatched data that will
  be ignored [-Wprofile-instr-out-of-date]

This sums those counts over a build log and exits 1 when the mismatched share passes the limit
(a ReXGlue update or a codegen change regenerates every function, so the profile must be made
again), 0 otherwise; it also fails when the log has no such line at all, which means the profile
was not used.

Usage: check_profile_match.py BUILD_LOG [--max-mismatch-percent P] [--functions N]
  --functions  the number of profiled functions to measure against when clang reports none out of
               date (it says nothing for a unit whose functions all match).
"""

import argparse
import re
import sys

LINE = re.compile(r"profile data may be out of date: of (\d+) functions?, (\d+) (?:has|have) "
                  r"mismatched data")


def mismatch(log_text):
    """(functions in the units that reported, mismatched functions, lines found)."""
    total = bad = lines = 0
    for m in LINE.finditer(log_text):
        total += int(m.group(1))
        bad += int(m.group(2))
        lines += 1
    return total, bad, lines


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--max-mismatch-percent", type=float, default=5.0)
    ap.add_argument("--functions", type=int, default=0)
    ap.add_argument("--used-marker", default="-fprofile-instr-use=",
                    help="text that proves the profile was passed to the compiler")
    args = ap.parse_args(argv)
    text = open(args.log, encoding="utf-8", errors="replace").read()
    total, bad, lines = mismatch(text)
    if lines == 0 and args.used_marker not in text:
        print("pgo check: no -fprofile-instr-use in the log: the profile was not used")
        return 1
    denominator = max(total, args.functions)
    share = 100.0 * bad / denominator if denominator else 0.0
    print(f"pgo check: {bad} of {denominator} functions with mismatched profile data "
          f"({share:.2f}%, limit {args.max_mismatch_percent}%)")
    return 1 if share > args.max_mismatch_percent else 0


if __name__ == "__main__":
    sys.exit(main())
