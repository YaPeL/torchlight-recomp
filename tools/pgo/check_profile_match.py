#!/usr/bin/env python3
"""Fails a PGO build whose profile no longer matches the code (docs/perf/pgo.md).

clang reports, per translation unit compiled with -fprofile-instr-use, how many of its functions
have profile data that no longer matches their code and is ignored:

  warning: profile data may be out of date: of 5120 functions, 37 have mismatched data that will
  be ignored [-Wprofile-instr-out-of-date]

This sums the mismatched functions over a build log and exits 1 when their share of the
profile's functions passes the limit (a ReXGlue update or a codegen change regenerates every
function, so the profile must be made again), 0 otherwise; it also fails when the log has no such
line and the profile was not passed to the compiler. The share is measured against the profile's
own count of functions (llvm-profdata show): clang says nothing for a unit whose functions all
match, so the units that warned are not the whole.

The log is read as UTF-8, or as UTF-16 when it is (a BOM, or the NUL bytes of UTF-16 without one):
Windows PowerShell 5.1's Tee-Object writes UTF-16, which read as UTF-8 showed no warning and passed
with 0 mismatched. And a log with no compile line at all (a ninja "[N/M] " step, "Building ...
object", or a compiler command with the profile) fails: 0 mismatched is only a result when
something was compiled.

Usage: check_profile_match.py BUILD_LOG [--build-dir DIR] [--profdata LLVM_PROFDATA]
                               [--profile PROFILE] [--max-mismatch-percent P] [--functions N]
  --build-dir  the build folder: its build.ninja proves the profile was passed to the compiler
               (ninja does not print the commands in the log), and names the profile.
  --profdata   llvm-profdata, to count the profile's functions (the profile from --profile or
               from build.ninja).
  --functions  the number of profiled functions, instead of --profdata.
"""

import argparse
import os
import re
import subprocess
import sys

USE = re.compile(r"-fprofile-instr-use=(\S+)")
COMPILE = re.compile(r"^\[\d+/\d+\] |Building (?:C|CXX|ASM) object|-fprofile-instr-use=", re.M)
LINE = re.compile(r"profile data may be out of date: of (\d+) functions?, (\d+) (?:has|have) "
                  r"mismatched data")


def decode_log(data):
    """The log's text, from UTF-8 or UTF-16 (with or without a BOM)."""
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", errors="replace")
    if data.startswith(b"\xef\xbb\xbf"):
        return data[3:].decode("utf-8", errors="replace")
    head = data[:4096]
    if len(head) >= 2 and head.count(b"\x00") * 4 >= len(head):
        # UTF-16 without a BOM: the NUL half of each ASCII character shows its byte order.
        odd_nuls = head[1::2].count(b"\x00")
        return data.decode("utf-16-le" if odd_nuls >= head[0::2].count(b"\x00") else "utf-16-be",
                           errors="replace")
    return data.decode("utf-8", errors="replace")


def mismatch(log_text):
    """(functions in the units that reported, mismatched functions, lines found)."""
    total = bad = lines = 0
    for m in LINE.finditer(log_text):
        total += int(m.group(1))
        bad += int(m.group(2))
        lines += 1
    return total, bad, lines


def profile_functions(profdata, profile):
    """The profile's "Total functions" per llvm-profdata show."""
    out = subprocess.run([profdata, "show", profile], check=True, capture_output=True,
                         text=True).stdout
    m = re.search(r"^Total functions: (\d+)", out, re.M)
    if not m:
        raise ValueError(f"{profdata} show {profile}: no function count")
    return int(m.group(1))


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--max-mismatch-percent", type=float, default=5.0)
    ap.add_argument("--functions", type=int, default=0)
    ap.add_argument("--build-dir", default="")
    ap.add_argument("--profdata", default="")
    ap.add_argument("--profile", default="")
    args = ap.parse_args(argv)
    text = decode_log(open(args.log, "rb").read())
    use = USE.search(text)
    if not use and args.build_dir:
        ninja = os.path.join(args.build_dir, "build.ninja")
        if os.path.exists(ninja):
            use = USE.search(open(ninja, errors="replace").read())
    total, bad, lines = mismatch(text)
    if lines == 0 and not COMPILE.search(text):
        print("pgo check: no compile line in the log (not a build log, or an encoding it cannot "
              "read): no result")
        return 1
    if lines == 0 and not use:
        print("pgo check: no -fprofile-instr-use in the log: the profile was not used")
        return 1
    functions = args.functions
    if args.profdata:
        profile = args.profile or (use.group(1) if use else "")
        try:
            functions = profile_functions(args.profdata, profile)
        except (OSError, subprocess.CalledProcessError, ValueError) as error:
            print(f"pgo check: {error}")
            return 1
    if lines and not functions:
        print("pgo check: mismatched functions reported but no profile count: pass --profdata "
              "or --functions")
        return 1
    # The profile's own count: the units that warned also count their functions with no profile.
    denominator = functions
    share = 100.0 * bad / denominator if denominator else 0.0
    print(f"pgo check: {bad} of {denominator} functions with mismatched profile data "
          f"({share:.2f}%, limit {args.max_mismatch_percent}%)")
    return 1 if share > args.max_mismatch_percent else 0


if __name__ == "__main__":
    sys.exit(main())
