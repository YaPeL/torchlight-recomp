#!/usr/bin/env python3
"""Runs check_guest_call_binding.py on the fixtures of tests/guest_call_binding.

OK, the hooked build, has to pass; BYPASS, generated code calling a hooked __imp__ directly, has
to fail on that, twice (a generated body and a compiler piece). On Android, PLT, the library
linked without -Wl,-Bsymbolic-functions, has to fail on its PLT calls. On Windows each executable
comes with its linker map.

Usage: test_check_guest_call_binding.py OK BYPASS [--plt PLT] [--ok-map MAP --bypass-map MAP]
                                        --nm LLVM_NM --objdump LLVM_OBJDUMP
Exit status: 0 when the check tells them apart, 1 when not.
"""

import argparse
import pathlib
import subprocess
import sys

CHECK = pathlib.Path(__file__).with_name("check_guest_call_binding.py")


def run(binary, arguments):
    result = subprocess.run([sys.executable, "-I", str(CHECK), binary, *arguments],
                            capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("ok")
    parser.add_argument("bypass")
    parser.add_argument("--plt")
    parser.add_argument("--ok-map")
    parser.add_argument("--bypass-map")
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objdump", required=True)
    args = parser.parse_args()
    tools = ["--nm", args.nm, "--objdump", args.objdump]

    # BYPASS has two: a generated body and a piece split off one (fixture.cpp).
    cases = [(args.ok, args.ok_map, 0, None), (args.bypass, args.bypass_map, 1, "bypass: 2")]
    if args.plt:
        cases.append((args.plt, None, 1, "plt:"))
    failures = []
    for binary, map_path, status, kind in cases:
        code, output = run(binary, tools + (["--map", map_path] if map_path else []))
        if code != status or (kind and f"\n{kind}" not in output):
            failures.append(f"{binary}: exit {code}, expected {status}"
                            + (f" with '{kind}'" if kind else "") + f"\n{output}")
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
