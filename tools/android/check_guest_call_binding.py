#!/usr/bin/env python3
"""Checks that the recompiled code's calls in a linked library reach the hooks.

The codegen defines every guest function twice: __imp__X, the recompiled body, and X
(sub_<address>, or a configured name such as xstart), a weak alias of it that the generated code
calls. A hook is a strong X in the same link, which the static linker picks over the alias. On
Android the game is a shared library linked with -Wl,-Bsymbolic-functions (ReXGlue patch 47), so
those calls are bound inside the library. This reads the library's symbols and disassembly and
fails when:

- a branch calls a guest function through the PLT (the library was linked without the option);
- code that may be generated reaches a hooked function's __imp__X directly instead of its hook
  X: the hook would be bypassed. That is any code but the host's own functions: a branch inside
  an __imp__ body, or inside a piece the compiler or linker made (OUTLINED_FUNCTION_*, .cold,
  .llvm.* clones, range thunks), whose origin the disassembly does not show;
- a hooked X has the address of its __imp__X, or a weak X another address;
- it found no hooked function or no direct guest call (nothing would have been checked).

A branch through a linker range thunk counts as a branch to the thunk's target. A branch from a
function to its own start is not a bypass (a loop or a recursive call made by the compiler), and
the hooks' (host code's) calls to __imp__X are what they are for. Indirect guest calls go through
the function table, whose entries the static linker resolves to X too.

On the desktop the game is an executable, whose direct calls the static linker binds: the same
bypass check runs on Linux (ELF x86-64), macOS (Mach-O arm64: symbols carry a leading underscore,
a weak X is a thunk of its own rather than at its __imp__'s address, and the generated code calls
it through dyld's stubs, which bind weak definitions at load) and Windows (PE x86-64: the
executable has no symbol table, so the names and addresses come from the linker's map, --map, of
the code sections only; a hooked X is one at another address than its __imp__X, and an X the map
does not list is taken as not hooked). ThinLTO bypassed 1492 hooks
this way on Android and some on the desktop before it was taken out (docs/regressions.md).

Usage: tools/android/check_guest_call_binding.py BINARY [--nm LLVM_NM] [--objdump LLVM_OBJDUMP]
                                                 [--map LINKER_MAP]
Exit status: 0 when every call is bound as expected, 1 when not (problems printed, at most 20
of each kind), 2 on bad input.
"""

import argparse
import bisect
import re
import subprocess
import sys

LABEL = re.compile(r"^([0-9a-f]+) <(.+)>:$")
# "  39e6770:      \tbl\t0x3acef18 <_Unwind_Resume>": every direct branch (bl, b, b.cond,
# cbz/cbnz, tbz/tbnz) names its target this way.
# A target without a name (a Mach-O stub, a PE without symbols) is looked up by its address.
BRANCH = re.compile(
    r"^\s*([0-9a-f]+):\s+(bl|b|b\.\w+|cbn?z|tbn?z)\t(?:.*, )?0x([0-9a-f]+)(?: <(.+)>)?$")
# x86-64: "  4a1b2c: \tcall\t0x4a2000 <sub_82100000>" (jmp and jcc for tail calls); on Windows,
# with no symbols, the target has no name: "call\t0x140001010".
BRANCH_X86 = re.compile(r"^\s*([0-9a-f]+):\s+(call\w*|jmp\w*|j[a-z]{1,3})\s+0x([0-9a-f]+)(?: <(.+)>)?$")
# lld's range extension thunks: __AArch64ADRPThunk_<target>, __AArch64AbsLongThunk_<target>.
THUNK = re.compile(r"__AArch64\w*Thunk_(.+)$")
# Code the compiler or linker split off or made, which may belong to a generated function.
PIECE = re.compile(r"OUTLINED_FUNCTION_|\.cold|\.llvm\.|__AArch64\w*Thunk_")
SHOWN = 20


def file_format(binary):
    """("elf" | "mach-o" | "pe", is x86-64) from the binary's first bytes (llvm-objdump -f fails
    on a Mach-O executable)."""
    with open(binary, "rb") as f:
        head = f.read(1024)
    if head[:4] == b"\x7fELF":
        return "elf", int.from_bytes(head[18:20], "little") == 62  # EM_X86_64
    if head[:4] == b"\xcf\xfa\xed\xfe":  # MH_MAGIC_64, little-endian
        return "mach-o", int.from_bytes(head[4:8], "little") == 0x01000007  # CPU_TYPE_X86_64
    if head[:2] == b"MZ":
        pe = int.from_bytes(head[0x3c:0x40], "little")
        return "pe", int.from_bytes(head[pe + 4:pe + 6], "little") == 0x8664
    raise OSError(f"{binary}: not an ELF, Mach-O or PE file")


def map_symbols(map_path):
    """{name: ("T", address)} from an MSVC / lld-link map ("0001:00000010  sub_82100000
    0000000140001010  obj"): the publics' Rva+Base, of the CODE sections only (the import
    table's __imp_ entries are data). The thunks of functions imported from Windows DLLs (their
    object column names the DLL, "kernel32:KERNEL32.dll") are left out: one named like a kernel
    export the SDK implements (RtlCaptureContext beside __imp__RtlCaptureContext) is not its hook."""
    defined = {}
    section_re = re.compile(r"^\s*([0-9a-f]{4}):[0-9a-f]{8}\s+[0-9a-f]+H\s+\S+\s+(\w+)", re.I)
    line_re = re.compile(r"^\s*([0-9a-f]{4}):[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\b(.*)$", re.I)
    code = set()
    with open(map_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = section_re.match(line)
            if m:
                if m.group(2).upper() == "CODE":
                    code.add(m.group(1))
                continue
            m = line_re.match(line)
            if m and m.group(1) in code:
                rest = m.group(4).split()
                if rest and rest[-1].lower().endswith(".dll"):
                    continue  # an import thunk
                defined[m.group(2)] = ("T", int(m.group(3), 16))
    return defined


def symbols(nm, library, macho=False, map_path=None, defined=None):
    """{name: (type, address)} of the library's guest functions: each __imp__X and its X (not the
    pieces the compiler split off a body)."""
    if map_path:
        defined = defined if defined is not None else map_symbols(map_path)
    elif macho:
        defined = {}
        # llvm-nm prints a Mach-O weak definition as T; -m spells it out:
        # "00000001000003d8 (__TEXT,__text) weak external _sub_1".
        out = subprocess.run([nm, "-m", "--defined-only", library], check=True,
                             capture_output=True, text=True).stdout
        for line in out.splitlines():
            fields = line.split()
            if len(fields) >= 3 and fields[-1].startswith("_"):
                kind = "W" if "weak" in fields[2:-1] else "T"
                defined[fields[-1][1:]] = (kind, int(fields[0], 16))
    else:
        defined = {}
        out = subprocess.run([nm, "--defined-only", library], check=True, capture_output=True,
                             text=True).stdout
        for line in out.splitlines():
            fields = line.split()
            if len(fields) == 3:
                defined[fields[2]] = (fields[1], int(fields[0], 16))
    found = {}
    for name, value in defined.items():
        # A piece the compiler split off a body (__imp__X.cold.1, an .llvm. clone) is not a guest
        # function: it has no X of its own. Its branches are still checked (PIECE in check()).
        if PIECE.search(name):
            continue
        if name.startswith("__imp__"):
            found[name] = value
            guest = name[len("__imp__"):]
            if guest in defined:
                found[guest] = defined[guest]
    return found


def check(library, nm, objdump, map_path=None):
    fmt, x86 = file_format(library)
    macho = fmt == "mach-o"
    publics = map_symbols(map_path) if map_path else None
    syms = symbols(nm, library, macho, map_path, publics)
    by_address = {address: name for name, (_, address) in syms.items()}
    # With a map (no symbols in the disassembly), each branch's function is the nearest code
    # public below it, host functions included (so a host call is never pinned on a guest body).
    function_at = {}
    if map_path:
        for name, (_, address) in publics.items():
            function_at.setdefault(address, name)
        function_at.update({address: name for address, name in by_address.items()})
    starts = sorted(function_at)
    problems = {}

    def problem(kind, text):
        problems.setdefault(kind, []).append(text)

    imp_addresses = {}  # address of __imp__X -> X
    hooked = {}  # address of a hooked __imp__X -> X
    for name, (kind, address) in syms.items():
        if not name.startswith("__imp__"):
            continue
        guest = name[len("__imp__"):]
        imp_addresses[address] = guest
        if guest not in syms:
            if map_path:
                continue  # the map leaves out what the link dropped or folded
            problem("missing", f"{guest}: __imp__{guest} has no {guest}")
            continue
        alias_kind, alias_address = syms[guest]
        if map_path:
            # A map tells no binding: a hook is an X at another address than its __imp__X.
            if alias_address != address:
                hooked[address] = guest
        elif alias_kind in ("W", "w"):
            if alias_address != address and not macho:
                problem("alias", f"{guest}: weak, at {alias_address:#x}, not at its __imp__ "
                        f"({address:#x})")
        elif alias_address == address:
            problem("alias", f"{guest}: strong ({alias_kind}) but at its __imp__'s address")
        else:
            hooked[address] = guest

    calls = 0
    function_start, function_name = None, None
    with subprocess.Popen([objdump, "-d", "--no-show-raw-insn", library],
                          stdout=subprocess.PIPE, text=True, bufsize=1 << 20) as dump:
        for line in dump.stdout:
            if line.startswith("0"):
                label = LABEL.match(line)
                if label:
                    function_start, function_name = int(label.group(1), 16), label.group(2)
                    if macho and function_name.startswith("_"):
                        function_name = function_name[1:]
                continue
            branch = (BRANCH_X86 if x86 else BRANCH).match(line)
            if not branch:
                continue
            target = int(branch.group(3), 16)
            if starts:
                site = int(branch.group(1), 16)
                k = bisect.bisect_right(starts, site) - 1
                if k >= 0:
                    function_start, function_name = starts[k], function_at[starts[k]]
            # With a map the disassembly's names are section offsets ("<.text+0x70>").
            target_name = ((by_address.get(target) if map_path else None) or branch.group(4)
                           or by_address.get(target, ""))
            if macho and target_name.startswith("_"):
                target_name = target_name[1:]
            if target_name.endswith("@plt"):
                if target_name[:-len("@plt")] in syms:
                    problem("plt", f"{function_name} at {branch.group(1)}: "
                            f"{branch.group(2)} {target_name}")
                continue
            thunk = THUNK.match(target_name)
            if thunk and thunk.group(1) in syms:
                target_name, target = thunk.group(1), syms[thunk.group(1)][1]
            if target_name in syms:
                calls += 1
            if target in hooked and target != function_start and (
                    function_start in imp_addresses or PIECE.search(function_name)):
                guest = hooked[target]
                problem("bypass", f"{function_name} at {branch.group(1)}: "
                        f"{branch.group(2)} to __imp__{guest}, past the hook {guest}")
    if dump.returncode:
        raise subprocess.CalledProcessError(dump.returncode, objdump)

    if not hooked:
        problem("vacuous", "no hooked guest function (a strong X beside its __imp__X)")
    if not calls:
        problem("vacuous", "no direct branch to a guest function")

    print(f"{library}: {len(imp_addresses)} guest functions, {len(hooked)} hooked, "
          f"{calls} direct branches to guest functions")
    for kind, texts in problems.items():
        print(f"{kind}: {len(texts)}")
        for text in texts[:SHOWN]:
            print(f"  {text}")
    return 1 if problems else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("library")
    parser.add_argument("--nm", default="llvm-nm")
    parser.add_argument("--objdump", default="llvm-objdump")
    parser.add_argument("--map", default=None)
    args = parser.parse_args()
    try:
        return check(args.library, args.nm, args.objdump, args.map)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"check_guest_call_binding: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
