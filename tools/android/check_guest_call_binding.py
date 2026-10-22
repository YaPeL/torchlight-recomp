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

Usage: tools/android/check_guest_call_binding.py LIBRARY [--nm LLVM_NM] [--objdump LLVM_OBJDUMP]
Exit status: 0 when every call is bound as expected, 1 when not (problems printed, at most 20
of each kind), 2 on bad input.
"""

import argparse
import re
import subprocess
import sys

LABEL = re.compile(r"^([0-9a-f]+) <(.+)>:$")
# "  39e6770:      \tbl\t0x3acef18 <_Unwind_Resume>": every direct branch (bl, b, b.cond,
# cbz/cbnz, tbz/tbnz) names its target this way.
BRANCH = re.compile(r"^\s*([0-9a-f]+):\s+(bl|b|b\.\w+|cbn?z|tbn?z)\t(?:.*, )?0x([0-9a-f]+) <(.+)>$")
# lld's range extension thunks: __AArch64ADRPThunk_<target>, __AArch64AbsLongThunk_<target>.
THUNK = re.compile(r"__AArch64\w*Thunk_(.+)$")
# Code the compiler or linker split off or made, which may belong to a generated function.
PIECE = re.compile(r"OUTLINED_FUNCTION_|\.cold|\.llvm\.|__AArch64\w*Thunk_")
SHOWN = 20


def symbols(nm, library):
    """{name: (type, address)} of the library's guest functions: each __imp__X and its X."""
    out = subprocess.run([nm, "--defined-only", library], check=True, capture_output=True,
                         text=True).stdout
    defined = {}
    for line in out.splitlines():
        fields = line.split()
        if len(fields) == 3:
            defined[fields[2]] = (fields[1], int(fields[0], 16))
    found = {}
    for name, value in defined.items():
        if name.startswith("__imp__"):
            found[name] = value
            guest = name[len("__imp__"):]
            if guest in defined:
                found[guest] = defined[guest]
    return found


def check(library, nm, objdump):
    syms = symbols(nm, library)
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
            problem("missing", f"{guest}: __imp__{guest} has no {guest}")
            continue
        alias_kind, alias_address = syms[guest]
        if alias_kind == "W":
            if alias_address != address:
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
                continue
            branch = BRANCH.match(line)
            if not branch:
                continue
            target_name, target = branch.group(4), int(branch.group(3), 16)
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
    args = parser.parse_args()
    try:
        return check(args.library, args.nm, args.objdump)
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"check_guest_call_binding: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
