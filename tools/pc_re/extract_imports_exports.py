#!/usr/bin/env python3
"""Dumps the exports and imports (with a basic MSVC demangling) of PE binaries.

Usage: extract_imports_exports.py OUT_DIR BINARY [BINARY ...]
Writes OUT_DIR/<name>_imports_exports.txt per binary and OUT_DIR/pe_summary.json.
"""
import argparse
import json
import os
import re
from pe_utils import PE

def demangle_msvc_name(sym):
    """
    Basic MSVC symbol extractor for class and member name.
    """
    if not sym.startswith("?"):
        return sym
    
    prefix_map = {
        "??0": "ctor ",
        "??1": "dtor ",
        "??_7": "vtable ",
        "??_R0": "RTTI_TypeDescriptor ",
        "??_R1": "RTTI_BaseClassDescriptor ",
        "??_R2": "RTTI_BaseClassArray ",
        "??_R3": "RTTI_ClassHierarchyDescriptor ",
        "??_R4": "RTTI_CompleteObjectLocator ",
    }
    
    kind = ""
    rest = sym
    for p, desc in prefix_map.items():
        if sym.startswith(p):
            kind = desc
            rest = sym[len(p):]
            break
            
    if rest.startswith("?"):
        rest = rest[1:]
        
    parts = rest.split("@")
    clean_parts = []
    for p in parts:
        if not p:
            break
        # Stop at type codes
        if any(p.startswith(c) for c in ["QAE", "UAE", "QBE", "UBE", "YAH", "YAP", "6B"]):
            break
        clean_parts.append(p)
        
    if clean_parts:
        clean_parts.reverse()
        return kind + "::".join(clean_parts)
    return sym

def process_file(rel_path):
    pe = PE(rel_path)
    filename = os.path.basename(rel_path)
    
    exports = pe.get_exports()
    imports = pe.get_imports()
    
    # Machine-readable dictionary
    info = {
        "file": filename,
        "image_base": hex(pe.image_base),
        "entry_point": hex(pe.entry_point),
        "num_sections": pe.num_sections,
        "exports_count": len(exports),
        "exports": [],
        "imports": {}
    }
    
    for exp in exports:
        info["exports"].append({
            "ordinal": exp["ordinal"],
            "rva": hex(exp["rva"]),
            "va": hex(pe.image_base + exp["rva"]),
            "mangled": exp["name"],
            "demangled": demangle_msvc_name(exp["name"])
        })
        
    for mod, funcs in imports.items():
        info["imports"][mod] = []
        for f in funcs:
            info["imports"][mod].append({
                "name": f,
                "demangled": demangle_msvc_name(f)
            })
            
    return info

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("out_dir")
    parser.add_argument("binaries", nargs="+")
    args = parser.parse_args()
    raw_dir = args.out_dir
    os.makedirs(raw_dir, exist_ok=True)

    summary = {}

    for full_path in args.binaries:
        b = os.path.basename(full_path)
        print(f"Processing {b}...")
        data = process_file(full_path)
        summary[b] = {
            "image_base": data["image_base"],
            "entry_point": data["entry_point"],
            "exports_count": data["exports_count"],
            "imported_modules": {mod: len(funcs) for mod, funcs in data["imports"].items()}
        }
        
        # Write individual text file
        out_txt = os.path.join(raw_dir, f"{os.path.splitext(b)[0]}_imports_exports.txt")
        with open(out_txt, "w") as f:
            f.write(f"Binary: {b}\n")
            f.write(f"ImageBase: {data['image_base']}\n")
            f.write(f"EntryPoint: {data['entry_point']}\n")
            f.write(f"Total Exports: {data['exports_count']}\n")
            f.write("\n================ EXPORTS ================\n")
            for exp in data["exports"]:
                f.write(f"[{exp['ordinal']:4d}] RVA: {exp['rva']:>10}  VA: {exp['va']:>10}  {exp['demangled']}  ({exp['mangled']})\n")
                
            f.write("\n================ IMPORTS ================\n")
            for mod, funcs in data["imports"].items():
                f.write(f"\n--- Module: {mod} ({len(funcs)} imports) ---\n")
                for fn in funcs:
                    f.write(f"  {fn['demangled']}  ({fn['name']})\n")
                    
        print(f"  Wrote {out_txt}")
        
    # Write summary json
    summary_path = os.path.join(raw_dir, "pe_summary.json")
    with open(summary_path, "w") as f:
        json.dump(summary, f, indent=2)
    print(f"Wrote summary to {summary_path}")

if __name__ == "__main__":
    main()
