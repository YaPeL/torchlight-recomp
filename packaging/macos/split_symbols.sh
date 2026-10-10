#!/bin/sh
# Moves the debug information of every Mach-O file below DIR (a `cmake --install` tree, before
# make_app.sh signs it) into SYMBOLS_DIR as a .dSYM (same relative path, ".dSYM" added), and strips
# the file's local symbols and debug map. The UUID stays in both, which is how a crash report is
# matched to its symbols (docs/release-pipeline.md, decision 4: the symbols are private, never
# published), as split_debug.sh does on Linux. dsymutil reads the debug information from the
# build's object files: run it on the machine that built the tree.
# Usage: split_symbols.sh DIR SYMBOLS_DIR
set -eu
[ $# -eq 2 ] || { echo "usage: $0 DIR SYMBOLS_DIR" >&2; exit 2; }
dir=$(cd "$1" && pwd)
mkdir -p "$2"
symbols=$(cd "$2" && pwd)
find "$dir" -type f | while read -r file; do
  file -b "$file" | grep -q '^Mach-O' || continue
  relative=${file#"$dir"/}
  mkdir -p "$symbols/$(dirname "$relative")"
  dsymutil "$file" -o "$symbols/$relative.dSYM"
  strip -S -x "$file"
done
