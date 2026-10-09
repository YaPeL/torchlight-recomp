# ReXGlue PPC instruction test data

The `.bin` and `.map` files the ReXGlue SDK's `ppc_tests` is generated from, assembled from the
SDK's own `tests/ppc/asm/*.s` at `bd833a2` (development, 2026-10-01) with the series through patch
27 (patches 23-25 add `instr_fctix_rounding`, `instr_mffs_rounding` and `instr_mtfsf_fields`), with
the SDK's bundled PowerPC binutils (`tools/binutils`, with VMX128). They are test data of the SDK
(BSD-3-Clause, its `LICENSE`); nothing of the game is in them.

For hosts without those binutils (macOS), with patch 22 of `patches/` (in `develop`):

    cmake ... -DREXGLUE_BUILD_TESTS=ON -DREXGLUE_PPC_TEST_BIN_DIR=<this branch>/bin

`bin/sources.sha256` holds the SHA-256 of each `.s` they were assembled from. Patch 22 checks every
source against it at configure time and stops if one changed, so a test cannot run against stale
binaries.

They were made with `tools/deps/build_ppc_test_data.sh <sdk checkout> bin` on Linux x86-64, and
are byte identical to the ones the SDK's build assembles there. They must be remade when the SDK
commit or its `tests/ppc/asm` changes.
