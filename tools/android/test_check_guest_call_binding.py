#!/usr/bin/env python3
"""Runs check_guest_call_binding.py on the fixtures of tests/guest_call_binding.

OK, the hooked build, has to pass, its hook's call to __imp__ in outlined pieces included; BYPASS,
generated code calling a hooked __imp__ directly, has to fail on that, three times (a generated
body, a compiler piece, and the outlined piece the generated body reaches too). On Android, PLT,
the library linked without -Wl,-Bsymbolic-functions, has to fail on its PLT calls. On Windows each
executable comes with its linker map.

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
    import tempfile
    check = load_check()
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


def load_check():
    import importlib.util
    spec = importlib.util.spec_from_file_location("check", CHECK)
    check = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(check)
    return check


def check_outlined_pieces():
    """check() on synthetic llvm-nm and llvm-objdump output of an ELF arm64 library, on every
    platform: the outlined piece paths no compiler makes on demand (a range thunk to a piece, a
    name the outliner reused or no piece has, a branch into a piece, the hook's own .cold piece)."""
    check = load_check()
    nm = """0000000000001000 T __imp__sub_1
0000000000001000 W sub_1
0000000000002000 T __imp__sub_2
0000000000003000 T sub_2
0000000000004000 T __imp__sub_4
0000000000004000 W sub_4
0000000000005000 t OUTLINED_FUNCTION_1
"""
    head = """0000000000001000 <__imp__sub_1>:
    1000:      \tbl\t0x3000 <sub_2>
0000000000002000 <__imp__sub_2>:
    2000:      \tret
0000000000003000 <sub_2>:
{hook}
0000000000004000 <__imp__sub_4>:
{generated}
0000000000005000 <OUTLINED_FUNCTION_1>:
    5000:      \tmov\tx0, x19
    5004:      \tb\t0x2000 <__imp__sub_2>
{more}
0000000000007000 <__AArch64ADRPThunk_OUTLINED_FUNCTION_1>:
    7000:      \tadrp\tx16, 0x5000
    7004:      \tbr\tx16
"""
    direct = "    3000:      \tbl\t0x5000 <OUTLINED_FUNCTION_1>"
    cases = [
        ("the hook only", dict(hook=direct, generated="", more=""), 0),
        ("generated code through a range thunk",
         dict(hook=direct, generated="    4000:      \tbl\t0x7000 "
              "<__AArch64ADRPThunk_OUTLINED_FUNCTION_1>", more=""), 1),
        ("generated code into the piece",
         dict(hook=direct, generated="    4000:      \tbl\t0x5004 <OUTLINED_FUNCTION_1+0x4>",
              more=""), 1),
        ("a thunk to a reused name",
         dict(hook=direct + "\n    3004:      \tbl\t0x5800 <OUTLINED_FUNCTION_1>\n"
              "    3008:      \tbl\t0x7000 <__AArch64ADRPThunk_OUTLINED_FUNCTION_1>",
              generated="",
              more="0000000000005800 <OUTLINED_FUNCTION_1>:\n"
                   "    5800:      \tb\t0x2000 <__imp__sub_2>"), 2),
        ("the hook's own .cold piece",
         dict(hook="    3000:      \tb\t0x8000 <sub_2.cold.1>", generated="",
              more="0000000000008000 <sub_2.cold.1>:\n"
                   "    8000:      \tbl\t0x5000 <OUTLINED_FUNCTION_1>"), 0),
        ("the hook's .cold piece calling its __imp__",
         dict(hook=direct + "\n    3004:      \tb\t0x8000 <sub_2.cold.1>", generated="",
              more="0000000000008000 <sub_2.cold.1>:\n"
                   "    8000:      \tbl\t0x2000 <__imp__sub_2>"), 0),
        ("a thunk to no piece",
         dict(hook=direct, generated="    4000:      \tbl\t0x7100 "
              "<__AArch64ADRPThunk_OUTLINED_FUNCTION_9>", more=""), 1),
        ("another function's .cold piece",
         dict(hook=direct, generated="",
              more="0000000000008000 <__imp__sub_4.cold.1>:\n"
                   "    8000:      \tbl\t0x5000 <OUTLINED_FUNCTION_1>"), 1),
    ]

    failures = []
    for name, parts, bypasses in cases:
        failures += run_synthetic(check, f"outlined pieces, {name}", ELF_ARM64, nm,
                                  head.format(**parts), None, bypasses)
    return failures


ELF_ARM64 = bytes(b"\x7fELF" + bytes(14) + (183).to_bytes(2, "little") + bytes(1004))
PE_X86_64 = bytes(b"MZ" + bytes(0x3a) + (0x80).to_bytes(4, "little") + bytes(0x40) + b"PE\0\0"
                  + (0x8664).to_bytes(2, "little") + bytes(886))


def run_synthetic(check, name, header, nm, disassembly, map_text, bypasses):
    """check() on a fake binary (`header`), with llvm-nm and llvm-objdump stubbed to print `nm`
    and `disassembly`, and the map `map_text` if any: [] when it reports `bypasses` bypasses."""
    import contextlib
    import io
    import tempfile
    import types

    class Dump:
        def __init__(self, text):
            self.stdout, self.returncode = io.StringIO(text), 0

        def __enter__(self):
            return self

        def __exit__(self, *exc):
            return False

    check.subprocess = types.SimpleNamespace(
        run=lambda *a, **k: types.SimpleNamespace(stdout=nm),
        Popen=lambda *a, **k: Dump(disassembly),
        PIPE=None, CalledProcessError=subprocess.CalledProcessError)
    paths = []
    try:
        with tempfile.NamedTemporaryFile("wb", delete=False) as f:
            f.write(header)
        paths.append(f.name)
        if map_text is not None:
            with tempfile.NamedTemporaryFile("w", suffix=".map", delete=False) as m:
                m.write(map_text)
            paths.append(m.name)
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            code = check.check(f.name, "nm", "objdump", paths[1] if map_text else None)
    finally:
        for path in paths:
            os.unlink(path)
    output = out.getvalue()
    if code != (1 if bypasses else 0) or (bypasses and f"\nbypass: {bypasses}\n" not in output):
        return [f"{name}: exit {code}, expected {bypasses} bypasses\n{output}"]
    return []


def check_outlined_pieces_map():
    """The same on Windows' footing: a PE with the linker's map and a disassembly without names,
    where each branch is pinned on the function holding its site or target."""
    check = load_check()
    publics = [("__imp__sub_1", 0x1000), ("sub_1", 0x1000), ("__imp__sub_2", 0x2000),
               ("sub_2", 0x3000), ("__imp__sub_4", 0x4000), ("sub_4", 0x4000),
               ("OUTLINED_FUNCTION_1", 0x5000), ("host", 0x6000)]
    map_text = (" 0001:00000000 00010000H .text                   CODE\n\n"
                + "".join(f" 0001:{a:08x}       {n}   {0x140000000 + a:016x} f   x.obj\n"
                          for n, a in publics))
    body = """   140001000: \tcall\t0x140003000
   140005000: \tmovq\t%rbx, %rcx
   140005004: \tjmp\t0x140002000
{extra}
"""
    cases = [
        ("the hook into the piece", "   140003000: \tcall\t0x140005004", 0),
        ("generated code into the piece", "   140003000: \tcall\t0x140005000\n"
         "   140004000: \tcall\t0x140005004", 1),
        ("host code to the piece", "   140003000: \tcall\t0x140005000\n"
         "   140006000: \tcall\t0x140005000", 1),
    ]
    failures = []
    for name, extra, bypasses in cases:
        failures += run_synthetic(check, f"outlined pieces with a map, {name}", PE_X86_64, "",
                                  body.format(extra=extra), map_text, bypasses)
    return failures


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
    failures = check_map_parser() + check_outlined_pieces() + check_outlined_pieces_map()
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
