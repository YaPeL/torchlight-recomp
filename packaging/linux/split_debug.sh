#!/bin/sh
# Moves the debug information of every ELF file below DIR into SYMBOLS_DIR (same relative path,
# ".debug" added), leaving each file stripped with a .gnu_debuglink to it; the build ID stays in
# both, which is how a crash is matched to its symbols (docs/release-pipeline.md, decision 4: the
# symbols are private, never published).
# Usage: split_debug.sh DIR SYMBOLS_DIR
set -eu
[ $# -eq 2 ] || { echo "usage: $0 DIR SYMBOLS_DIR" >&2; exit 2; }
dir=$(cd "$1" && pwd)
mkdir -p "$2"
symbols=$(cd "$2" && pwd)
find "$dir" -type f | while read -r file; do
  head -c 4 "$file" | grep -q 'ELF' || continue
  relative=${file#"$dir"/}
  mkdir -p "$symbols/$(dirname "$relative")"
  objcopy --only-keep-debug "$file" "$symbols/$relative.debug"
  objcopy --strip-debug --strip-unneeded --add-gnu-debuglink="$symbols/$relative.debug" "$file"
done
