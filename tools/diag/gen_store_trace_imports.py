#!/usr/bin/env python3
"""Writes the store trace's kernel import wrappers (diag/store_trace.cpp) for a generated tree.

Every kernel export the generated code calls (`__imp__Name(ctx, base)`, not a guest function's
`__imp__sub_...`) gets a `__wrap___imp__Name` that hands the call to
torchlight::diag::store_trace::Import(ctx, base, "Name", __real___imp__Name), which records it and
what it wrote around its pointer arguments while a traced call runs; the link redirects the
generated code's references with `--wrap=__imp__Name` (GNU ld and lld: Linux and Android, where the
diagnostics builds run). Nothing generated is edited.

Usage: gen_store_trace_imports.py GENERATED_DIR OUTPUT_CPP OUTPUT_LINK_RESPONSE_FILE
"""

import glob
import os
import re
import sys

CALL = re.compile(r"\b__imp__([A-Za-z_][A-Za-z0-9_]*)\(ctx, base\)")


def write_if_changed(path, text):
    old = open(path).read() if os.path.exists(path) else None
    if old != text:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w") as f:
            f.write(text)


def main():
    generated, output, response = sys.argv[1], sys.argv[2], sys.argv[3]
    names = set()
    for path in glob.glob(os.path.join(generated, "torchlight_recomp.*.cpp")):
        with open(path, encoding="utf-8", errors="replace") as f:
            for m in CALL.finditer(f.read()):
                if not m.group(1).startswith("sub_"):
                    names.add(m.group(1))
    lines = ["// Written by tools/diag/gen_store_trace_imports.py: the store trace's import wrappers.",
             "#include <cstdint>", "#include <rex/ppc/context.h>", "",
             "namespace torchlight::diag::store_trace {",
             "void Import(PPCContext& ctx, uint8_t* base, const char* name,",
             "            void (*real)(PPCContext&, uint8_t*));",
             "}", "", 'extern "C" {']
    for name in sorted(names):
        lines.append(f"void __real___imp__{name}(PPCContext&, uint8_t*);")
        lines.append(f"void __wrap___imp__{name}(PPCContext& ctx, uint8_t* base) {{ "
                     f"torchlight::diag::store_trace::Import(ctx, base, \"{name}\", "
                     f"__real___imp__{name}); }}")
    lines.append("}")
    write_if_changed(output, "\n".join(lines) + "\n")
    write_if_changed(response, "".join(f"-Wl,--wrap=__imp__{n}\n" for n in sorted(names)))
    print(f"store trace imports: {len(names)}")


if __name__ == "__main__":
    main()
