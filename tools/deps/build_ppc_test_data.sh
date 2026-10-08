#!/bin/sh
# Assembles the ReXGlue SDK's PPC instruction tests (tests/ppc/asm/*.s) into the .bin and .map files
# ppc_tests is generated from, with the PowerPC binutils bundled in the SDK (Linux and Windows
# only). Hosts without them (macOS) build the tests with -DREXGLUE_PPC_TEST_BIN_DIR=<OUT_DIR>
# (patches/README.md, patch 21). Same commands as the SDK's cmake/ppc_test_pipeline.cmake. Test data
# of the SDK only; nothing of the game is involved.
#
# Usage: tools/deps/build_ppc_test_data.sh SDK_CHECKOUT OUT_DIR
set -eu

[ $# -eq 2 ] || { echo "usage: $0 SDK_CHECKOUT OUT_DIR" >&2; exit 2; }
sdk=$(cd "$1" && pwd)
out=$2
tools="$sdk/tools/binutils"
obj=$(mktemp -d)
trap 'rm -rf "$obj"' EXIT
mkdir -p "$out"

for asm in "$sdk"/tests/ppc/asm/*.s; do
  name=$(basename "$asm" .s)
  "$tools/powerpc-none-elf-as" -a32 -be -mregnames -mpower7 -maltivec -mvsx -mvmx128 -R \
    -o "$obj/$name.o" "$asm"
  "$tools/powerpc-none-elf-ld" -A powerpc:common32 -melf32ppc -EB -nostdlib \
    --oformat=binary -Ttext=0x82010000 -e 0x82010000 -o "$out/$name.bin" "$obj/$name.o"
  "$tools/powerpc-none-elf-nm" --numeric-sort "$obj/$name.o" > "$out/$name.map"
done
echo "$(ls "$out"/*.bin | wc -l) test binaries in $out"
