#!/usr/bin/env python3
"""Runs check_guest_call_binding.py on the fixtures of tests/guest_call_binding.

OK, the hooked build, has to pass, its hook's call to __imp__ in outlined pieces included; BYPASS,
generated code calling a hooked __imp__ directly, has to fail on that, three times (a generated
body, a compiler piece, and the outlined piece the generated body reaches too). On Android, PLT, the library
linked without -Wl,-Bsymbolic-functions, has to fail on its PLT calls. On Windows each executable
comes with its linker map.

Usage: test_check_guest_call_binding.py OK BYPASS [--plt PLT] [--ok-map MAP --bypass-map MAP]
                                        --nm LLVM_NM --objdump LLVM_OBJDUMP
Exit status: 0 when the check tells them apart, 1 when not.
"""

import argparse
import os
import pathlib
import subprocess
import sys

CHECK = pathlib.Path(__file__).with_name("check_guest_call_binding.py")


def run(binary, arguments):
    result = subprocess.run([sys.executable, "-I", str(CHECK), binary, *arguments],
                            capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def check_map_parser():
    """map_symbols on a synthetic map, on every platform: lld-link's lines, link.exe's with its f
    and i flags, an object name with a space, and import thunks (left out: their object names a
    DLL)."""
    import importlib.util
    import tempfile
    spec = importlib.util.spec_from_file_location("check", CHECK)
    check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(check)
    text = """ Start         Length     Name                   Class
 0001:00000000 00001000H .text                   CODE
 0002:00000000 00000100H .idata$5                DATA

  Address         Publics by Value              Rva+Base               Lib:Object

 0001:00000000       __imp__RtlCaptureContext   0000000140001000     a b.obj
 0001:00000040       RtlCaptureContext          0000000140001040     kernel32:KERNEL32.dll
 0001:00000050       sub_1                      0000000140001050 f   fixture.obj
 0001:00000060       GetTickCount               0000000140001060 f i kernel32:KERNEL32.dll
 0002:00000038       __imp_RtlCaptureContext    0000000140002038     kernel32:KERNEL32.dll
"""
    with tempfile.NamedTemporaryFile("w", suffix=".map", delete=False) as f:
        f.write(text)
    try:
        got = check.map_symbols(f.name)
    finally:
        os.unlink(f.name)
    want = {"__imp__RtlCaptureContext": ("T", 0x140001000), "sub_1": ("T", 0x140001050)}
    return [] if got == want else [f"map_symbols on the synthetic map: {got}, expected {want}"]


def has_import_thunk(map_path, name):
    """Whether the map lists `name` as a DLL import thunk in a code section (the fixture's case)."""
    for line in open(map_path, encoding="utf-8", errors="replace"):
        fields = line.split()
        if len(fields) >= 4 and fields[1] == name and fields[-1].lower().endswith(".dll") \
                and fields[0].startswith("0001:"):
            return True
    return False


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

    # BYPASS has three: a generated body, a piece split off one, and an outlined piece the
    # generated body reaches besides the hook (fixture.cpp, hook.cpp).
    cases = [(args.ok, args.ok_map, 0, None), (args.bypass, args.bypass_map, 1, "bypass: 3")]
    if args.plt:
        cases.append((args.plt, None, 1, "plt:"))
    failures = check_map_parser()
    # On Windows the OK fixture must still carry the RtlCaptureContext import thunk, or its case
    # (an import named like an SDK kernel export) would pass without testing anything.
    if args.ok_map and not has_import_thunk(args.ok_map, "RtlCaptureContext"):
        failures.append(f"{args.ok_map}: no RtlCaptureContext import thunk; "
                        "the fixture lost its case")
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
