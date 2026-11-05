#!/usr/bin/env python3
"""Checks the generated sub_822DC6B0 against what the host blend loop relies on.

src/hooks/wardrobe_blend_hooks.cpp replaces the wardrobe build's blend loop (0x822DD220..0x822DD398)
through a midasm hook and leaves the rest of the function's state as the loop would. That holds
for the generated code it was checked on; this script checks it again on every build of the game
and fails (exit 1) when any of it changed:

  1. The function's code is the one checked: a SHA-256 of its text without the hook's lines. A
     change (new codegen, other ReXGlue, other game build) needs the analysis below reviewed and
     docs/wardrobe-texture.md's evidence checked again before the digest is updated here.
  2. The hook is there, right after `rotlwi r26,r10,0` (0x822DD21C), with the arguments and the jump
     the host side expects.
  3. Reaching definitions at the loop: f30, f31 and r17 each have a single definition on every
     path from the function's entry, the constant-pool loads and `li r17,255` guest_abi
     wardrobe::blend cites (and r5, r10 at those loads); the base's pixel data is loaded from
     [r1+164]+24 right before the item's getPixelBox call, where the host reads it.
  4. What the loop writes: the registers and the stack bytes, against the host loop's list
     (guest_abi wardrobe::blend::stack).
  5. Liveness from the loop's exit (0x822DD39C): which of those the rest of the function may read
     before writing them again. Allowed: r26 (the host loop sets it), the stack temporaries (the
     host loop writes them as the guest does), the volatile registers only as call arguments (the
     loop's exit follows a call; the PowerPC ABI), and xer only for its never-set summary
     overflow bit.

The analysis is a search over the generated C++'s statements and gotos (labels give each
instruction's address; every instruction comment is 4 bytes): a call reads every argument register
and writes every volatile one; unpackColour's calls in the loop write their four output bytes
through their pointer arguments. Outside the model: lr (read only by the prologue's mflr here),
the vector registers (the loop uses none), memory reached through pointers passed to calls (some temporaries'
addresses, r1+192 and others, are passed to calls after the loop: every temporary is left as the
guest leaves it, so such a call reads the same bytes), and calls that keep a
volatile register (the register save/restore helpers, which the codegen inlines as no calls).

Usage:
  check_generated.py <generated dir>                 exit 1 on any failure (the development test,
                                                     ctest wardrobe_blend_generated)
  check_generated.py <generated dir> --header <file>  writes the result as a C++ header for
                                                     src/hooks/wardrobe_blend_hooks.cpp and exits
                                                     0 (the game build): when the check fails, the
                                                     game keeps the recompiled loop and logs why

The stack offsets and the hook's addresses come from src/guest_abi/wardrobe.h (wardrobe::blend),
the only place they are written.
"""

import glob
import hashlib
import json
import os
import re
import sys

FUNCTION = "sub_822DC6B0"
# The function's text without the hook's lines, as checked (2026-10-26). The same with the SDK CI
# builds (tools/deps, key 20f9f9ec2fa17ada, Linux) and with the development one.
EXPECTED_SHA256 = "95b0136086adef50dcf629cae6cf263df8f2303c3cd966211c90eccf20a40efd"

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def guest_abi_blend():
    """The integer constants of src/guest_abi/wardrobe.h's namespace blend, by name. Accepted form,
    one or more per declaration: `inline constexpr uint8_t|uint16_t|uint32_t kName = <integer or
    kOther [+ ...]>[, kName = ...];`, decimal or hex, an optional u suffix."""
    path = os.path.join(ROOT, "src", "guest_abi", "wardrobe.h")
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        text = text[text.index("namespace blend {"):text.index("}  // namespace blend")]
        text = re.sub(r"//[^\n]*", "", text)
        values = {}
        for m in re.finditer(r"inline constexpr uint(?:8|16|32)_t ([^;]+);", text):
            for part in m.group(1).split(","):
                name, expr = (x.strip() for x in part.split("="))
                expr = re.sub(r"\b(0[xX][0-9A-Fa-f]+|\d+)[uU]\b", r"\1", expr)
                expr = re.sub(r"\bk\w+", lambda k: str(values[k.group(0)]), expr)
                if not re.fullmatch(r"[0-9A-Fa-fxX +\-]+", expr):
                    raise ValueError("%s = %s" % (name, expr))
                values[name] = eval(expr, {"__builtins__": {}})
    except (OSError, ValueError, KeyError) as e:
        fail("src/guest_abi/wardrobe.h's namespace blend could not be read (%s): see guest_abi_blend()'s "
             "accepted form" % e)
    return values


def constants():
    """What the checks compare with, from guest_abi wardrobe::blend (and the evidence addresses)."""
    global ABI, HOOK, LOOP_START, LOOP_EXIT, UNPACK_OUTPUTS, SLOTS, REACHING, POOL, LOADS
    ABI = guest_abi_blend()
    a = ABI
    HOOK = ("\tif (WardrobeBlendLoop(ctx.r1, r19, r26, ctx.fpscr)) {\n"
            "\t\tgoto loc_%X;\n"
            "\t}\n"
            "\telse {\n"
            "\t}\n") % a["kLoopExit"]
    LOOP_START, LOOP_EXIT = "loc_%X" % (a["kHookAddress"] + 4), "loc_%X" % a["kLoopExit"]
    # The loop's stack bytes (offsets from r1), as hooks/wardrobe_blend.h writes them.
    UNPACK_OUTPUTS = tuple(a[k] for k in ("kItemR", "kItemG", "kItemB", "kItemA",
                                           "kBaseR", "kBaseG", "kBaseB", "kBaseA"))
    SLOTS = set(UNPACK_OUTPUTS)
    for name in ("kItemGDouble", "kItemADouble", "kItemBDouble", "kItemRDouble",
                 "kBaseGDouble", "kBaseRDouble", "kBaseBDouble", "kTruncated"):
        SLOTS |= set(range(a[name], a[name] + 8))
    # The definitions guest_abi wardrobe::blend cites, by address and instruction.
    REACHING = {
        "f31": (0x822DCAF8, "lfs f31,%d(r5)" % (a["kAlphaScale"] - a["kConstantPool"])),
        "f30": (0x822DCB00, "lfs f30,%d(r5)" % (a["kOne"] - a["kConstantPool"])),
        "r17": (0x822DCB44, "li r17,%d" % a["kBlendedItemAlpha"]),
    }
    # r5 = kConstantPool at both loads: (register, at, its only definition there, that instruction).
    high = (a["kConstantPool"] + 0x8000) >> 16
    low = a["kConstantPool"] - (high << 16)
    high = high - 0x10000 if high >= 0x8000 else high
    POOL = [
        ("ctx.r5", 0x822DCAF8, 0x822DCAE0, "addi r5,r10,%d" % low),
        ("ctx.r5", 0x822DCB00, 0x822DCAE0, "addi r5,r10,%d" % low),
        ("ctx.r10", 0x822DCAE0, 0x822DCAD8, "lis r10,%d" % high),
    ]
    # The loads of the loop's inputs the host repeats, by address.
    LOADS = [
        (0x822DD1EC, "lwz r11,%d(r1)" % a["kBasePixelBox"]),
        (0x822DD200, "lwz r31,%d(r11)" % a["kPixelBoxData"]),
        (0x822DD204, "bl 0x82472280"),  # the item's getPixelBox, writing only its box at r1+432
        (0x822DD208, "lwz r10,%d(r1)" % a["kRows"]),
        (0x822DD214, "lwz r29,%d(r1)" % a["kItemData"]),
        (a["kHookAddress"] + 4, "lwz r11,%d(r1)" % a["kColumns"]),
    ]


HOOK_AFTER = "\tr26.u64 = __builtin_rotateleft32(ctx.r10.u32, 0);\n"  # rotlwi r26,r10,0 @kHookAddress

VOLATILE = {"ctx.r0"} | {"ctx.r%d" % i for i in range(3, 13)} | {"ctx.f%d" % i for i in range(0, 14)}
ARGUMENTS = {"ctx.r%d" % i for i in range(3, 11)} | {"ctx.f%d" % i for i in range(1, 14)}
CALL_READS = ARGUMENTS | {"ctx.r1", "ctx.r2", "ctx.r13"}

REG = re.compile(r"(?<![\w.])(ctx\.(?:r|f)\d+|r\d+|f\d+|cr\d|xer|ctr|temp|ea)(?![\w])")
LOAD = re.compile(r"REX_LOAD_U(8|16|32|64)\(ctx\.r1\.u32 \+ (-?\d+)\)")
STORE = re.compile(r"^REX_STORE_U(8|16|32|64)\(ctx\.r1\.u32 \+ (-?\d+),")
ASSIGN = re.compile(r"^(ctx\.(?:r|f)\d+|r\d+|f\d+|cr\d|xer|ctr|temp|ea)\.(\w+)(\.\w+)?\s*=\s*(.*);$")
CALL = re.compile(r"^(sub_[0-9A-F]+\(ctx, base\)|REX_CALL_INDIRECT_FUNC\(.*\));")


class Failed(Exception):
    pass


def fail(message):
    raise Failed(message)


def find_function(directory):
    for path in sorted(glob.glob(os.path.join(directory, "torchlight_recomp.*.cpp"))):
        with open(path, encoding="utf-8", errors="replace") as f:
            text = f.read()
        start = text.find("DEFINE_REX_FUNC(%s)" % FUNCTION)
        if start < 0:
            continue
        end = text.find("\n}\n", start)
        return text[start:end + 3]
    fail("%s not found in the generated code (%s)" % (FUNCTION, os.path.basename(os.path.normpath(directory))))


class Function:
    def __init__(self, text):
        self.lines = text.split("\n")
        self.labels = {}
        self.address = []  # the instruction each line belongs to
        address = int(FUNCTION[4:], 16) - 4
        for i, line in enumerate(self.lines):
            m = re.match(r"^(loc_([0-9A-F]+)):", line)
            if m:
                self.labels[m.group(1)] = i
                address = int(m.group(2), 16) - 4
            elif re.match(r"^\t// \S", line):
                address += 4
            self.address.append(address)
        self.info = [self.statement(i) for i in range(len(self.lines))]

    def instruction(self, address):
        for i, line in enumerate(self.lines):
            if self.address[i] == address and re.match(r"^\t// \S", line):
                return line.strip()[3:]
        return None

    def statement(self, i):
        """(reads, writes, successors) of line i."""
        line = self.lines[i].strip()
        nxt = [i + 1]
        if (not line or line.startswith("//") or line.startswith("loc_") or line.startswith("PPC")
                or line.startswith("uint32_t ea") or line.startswith("DEFINE") or line == "}"
                or line.startswith("REX_FUNC_PROLOGUE")):
            return set(), set(), nxt
        m = re.match(r"^if \((.*)\) goto (loc_[0-9A-F]+);", line)
        if m:
            return set(REG.findall(m.group(1))), set(), [i + 1, self.labels[m.group(2)]]
        m = re.match(r"^goto (loc_[0-9A-F]+);", line)
        if m:
            return set(), set(), [self.labels[m.group(1)]]
        if line == "return;":
            return {"ctx.r3", "ctx.f1", "ctx.r1"}, set(), []
        reads, writes = set(), set()
        for m in LOAD.finditer(line):
            offset = int(m.group(2))
            reads |= {("S", offset + k) for k in range(int(m.group(1)) // 8)}
        if CALL.match(line):
            reads |= CALL_READS | set(REG.findall(line))
            writes |= VOLATILE
            return reads, writes, nxt
        m = STORE.match(line)
        if m:
            offset = int(m.group(2))
            writes |= {("S", offset + k) for k in range(int(m.group(1)) // 8)}
            reads |= set(REG.findall(line[m.end():]))
            return reads, writes, nxt
        m = ASSIGN.match(line)
        if m:
            target, field = m.group(1), m.group(2)
            reads |= set(REG.findall(m.group(4)))
            if field in ("u64", "s64", "f64") and not m.group(3):
                writes.add(target)
            else:  # a partial write keeps the rest
                reads.add(target)
                writes.add(target)
            return reads, writes, nxt
        tokens = set(REG.findall(line))
        m = re.match(r"^(cr\d|xer)\.", line)
        if m:
            writes.add(m.group(1))
            tokens.discard(m.group(1))
        return reads | tokens, writes, nxt


def check(directory):
    """The digest's first 16 hex digits; raises Failed with what does not hold."""
    constants()
    text = find_function(directory)

    # 2. The hook (text only: nothing parsed before the digest).
    if text.count(HOOK_AFTER + HOOK) != 1:
        fail("the WardrobeBlendLoop hook is not right after `rotlwi r26,r10,0` as "
             "src/hooks/wardrobe_blend_hooks.cpp expects (config/torchlight_hooks.toml)")
    plain = text.replace(HOOK_AFTER + HOOK, HOOK_AFTER)

    # 1. The code checked.
    digest = hashlib.sha256(plain.encode()).hexdigest()
    if digest != EXPECTED_SHA256:
        fail("%s changed (sha256 %s, checked %s): review the host blend loop's assumptions "
             "(this script's checks, guest_abi wardrobe::blend's evidence) before updating the digest"
             % (FUNCTION, digest, EXPECTED_SHA256))

    f = Function(plain)
    if f.instruction(ABI["kHookAddress"]) != "rotlwi r26,r10,0":
        fail("@%08X is `%s`, not the hook's `rotlwi r26,r10,0`"
             % (ABI["kHookAddress"], f.instruction(ABI["kHookAddress"])))
    start, exit_line = f.labels[LOOP_START], f.labels[LOOP_EXIT]
    loop = range(start, exit_line)

    # 3. Reaching definitions at the loop's start, and of the pool's address at its loads.
    predecessors = {i: [] for i in range(len(f.lines))}
    for i, (_, _, successors) in enumerate(f.info):
        for j in successors:
            if j < len(f.lines):
                predecessors[j].append(i)

    def reaching(reg, line):
        definitions, seen, stack = set(), set(), list(predecessors[line])
        while stack:
            i = stack.pop()
            if i in seen:
                continue
            seen.add(i)
            if i == 0:
                definitions.add("entry")  # a path with no definition at all
                continue
            if reg in f.info[i][1]:
                definitions.add(f.address[i])
                continue
            stack += predecessors[i]
        return definitions

    def first_line(address):
        return next(i for i in range(len(f.lines)) if f.address[i] == address and not f.lines[i].startswith("\t//")
                    and not f.lines[i].startswith("loc_"))

    hook_line = next(i for i in range(start) if f.lines[i] == HOOK_AFTER.rstrip("\n"))
    checks = [(reg, hook_line, address, instruction) for reg, (address, instruction) in REACHING.items()]
    checks += [(reg, first_line(at), address, instruction) for reg, at, address, instruction in POOL]
    for reg, line, address, instruction in checks:
        definitions = reaching(reg, line)
        if definitions != {address} or f.instruction(address) != instruction:
            fail("%s @%08X: definitions %s, expected only `%s` @%08X"
                 % (reg, f.address[line], sorted(d if isinstance(d, str) else "%08X" % d for d in definitions),
                    instruction, address))
    # The loads of the loop's inputs: the host reads the same memory (the base's pixel data just
    # after the item's getPixelBox call, which writes only the item's box).
    for address, instruction in LOADS:
        if f.instruction(address) != instruction:
            fail("@%08X: `%s`, expected `%s`" % (address, f.instruction(address), instruction))

    # 4. What the loop writes.
    written = set()
    for i in loop:
        written |= f.info[i][1]
    written |= {("S", o) for o in UNPACK_OUTPUTS}
    stack_written = {w[1] for w in written if isinstance(w, tuple)}
    if stack_written != SLOTS:
        fail("the loop writes the stack bytes %s, the host loop %s"
             % (sorted(stack_written - SLOTS), sorted(SLOTS - stack_written)))
    registers = {w for w in written if not isinstance(w, tuple)}
    expected_registers = VOLATILE | {"cr0", "cr6", "xer", "r26", "r28", "r29", "r30", "r31"}
    if not registers <= expected_registers:
        fail("the loop writes %s, not in the analysed set" % sorted(registers - expected_registers))

    # 5. Liveness from the exit. Paths re-entering the loop see its unpack calls write their outputs.
    unpack_lines = [i for i in loop if f.lines[i].strip() == "sub_82474560(ctx, base);"]
    if len(unpack_lines) != 2:
        fail("the loop's unpackColour calls: %d, expected 2" % len(unpack_lines))
    item_unpack, base_unpack = unpack_lines
    kills = {item_unpack: {("S", o) for o in UNPACK_OUTPUTS[:4]},
             base_unpack: {("S", o) for o in UNPACK_OUTPUTS[4:]}}
    # Every reader on every path, each judged on its own (a path stops where the resource is
    # written again, not at a reader).
    readers = {}
    for resource in written:
        seen, stack = set(), [exit_line]
        while stack:
            i = stack.pop()
            if i in seen or i >= len(f.lines):
                continue
            seen.add(i)
            reads, writes, successors = f.info[i]
            if resource in reads:
                readers.setdefault(resource, set()).add(i)
            if resource in writes or resource in kills.get(i, ()):
                continue
            stack += successors

    def allowed(resource, i):
        line = f.lines[i].strip()
        if isinstance(resource, tuple):
            return resource[1] in SLOTS  # written by the host loop as the guest leaves it
        if resource == "r26":
            return True  # set to 0 by the host loop
        if resource == "xer":
            # compare<...>(..., xer) reads only xer.so, never set here; `xer.ca = ...` sets the
            # carry (a partial write, so counted as a read too) and reads nothing of xer.
            return "compare" in line or bool(re.match(r"^xer\.ca\s*=", line))
        if resource in VOLATILE:
            # A call's argument registers: by the ABI none carries the loop's values.
            return bool(CALL.match(line)) and resource in ARGUMENTS
        return False

    problems = []
    for resource, lines in sorted(readers.items(), key=str):
        for i in sorted(lines):
            if not allowed(resource, i):
                problems.append("%s read @%08X" % (resource if isinstance(resource, str) else "r1+%d" % resource[1],
                                                   f.address[i]))
    if any(re.search(r"xer\.so\s*=", line) for line in f.lines):
        problems.append("xer.so is set in the function")
    if any("xer.ca" in line and not re.match(r"^\s*xer\.ca\s*=", line) for line in f.lines):
        problems.append("xer.ca is read in the function")
    if problems:
        fail("after the loop: " + "; ".join(problems))
    return digest[:16]


def header(path, problem):
    text = ("// Written by tools/wardrobe_blend/check_generated.py at build time: whether the generated %s is\n"
            "// the one the host blend loop was checked on.\n#pragma once\n"
            "namespace torchlight::hooks::wardrobe_blend_generated {\n"
            "inline constexpr bool kChecked = %s;\n"
            "inline constexpr const char* kProblem = %s;\n"
            "}  // namespace torchlight::hooks::wardrobe_blend_generated\n"
            % (FUNCTION, "false" if problem else "true", json.dumps(problem or "")))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
    if old != text:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)


def main():
    args = sys.argv[1:]
    if len(args) not in (1, 3) or (len(args) == 3 and args[1] != "--header"):
        print("usage: check_generated.py <generated dir> [--header <file>]", file=sys.stderr)
        sys.exit(2)
    try:
        digest = check(args[0])
        problem = None
        print("check_generated: %s as checked (%s); the host blend loop's assumptions hold" % (FUNCTION, digest))
    except Failed as e:
        problem = str(e)
        print("check_generated: " + problem, file=sys.stderr)
    except Exception as e:  # noqa: BLE001 -- any other failure of the check is a failed check too
        problem = "the check itself failed: %s: %s" % (type(e).__name__, e)
        print("check_generated: " + problem, file=sys.stderr)
    if len(args) == 3:
        header(args[2], problem)
        if problem:
            print("check_generated: WARNING: the game keeps the recompiled wardrobe blend loop", file=sys.stderr)
        sys.exit(0)
    sys.exit(1 if problem else 0)


if __name__ == "__main__":
    main()
