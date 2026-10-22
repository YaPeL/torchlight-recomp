#!/usr/bin/env python3
"""Runs check_guest_call_binding.py on the fixture libraries of tests/guest_call_binding.

OK, linked with -Wl,-Bsymbolic-functions, has to pass; PLT, linked without it, has to fail on
its PLT calls; BYPASS, generated code calling a hooked __imp__ directly, has to fail on that.

Usage: test_check_guest_call_binding.py OK PLT BYPASS --nm LLVM_NM --objdump LLVM_OBJDUMP
Exit status: 0 when the check tells the three apart, 1 when not.
"""

import argparse
import pathlib
import subprocess
import sys

CHECK = pathlib.Path(__file__).with_name("check_guest_call_binding.py")


def run(library, tools):
    result = subprocess.run([sys.executable, "-I", str(CHECK), library, *tools],
                            capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("ok")
    parser.add_argument("plt")
    parser.add_argument("bypass")
    parser.add_argument("--nm", required=True)
    parser.add_argument("--objdump", required=True)
    args = parser.parse_args()
    tools = ["--nm", args.nm, "--objdump", args.objdump]

    failures = []
    for library, status, kind in ((args.ok, 0, None), (args.plt, 1, "plt:"),
                                  (args.bypass, 1, "bypass:")):
        code, output = run(library, tools)
        if code != status or (kind and f"\n{kind}" not in output):
            failures.append(f"{library}: exit {code}, expected {status}"
                            + (f" with '{kind}'" if kind else "") + f"\n{output}")
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
