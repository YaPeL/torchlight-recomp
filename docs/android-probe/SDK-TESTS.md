# The SDK's tests on Android (patches 34-37), 2026-10-17

Following docs/rexglue-upstream.md, section 8, "Android: the SDK's tests on a device".

## Setup

- **The series:** `develop` 935e080 (series up to 37) on `bd833a2`.
- **The host SDK:** the same series, built apart with `tools/deps/build_sdk.sh` into its own prefix.
  - Its `rexglue` is 0.10.0.24-dev.gbd833a2; it ran the codegen through `REXGLUE_HOST_TOOL`.
  - The registry entry its install added was deleted, as section 6 says.
  - The shared desktop SDK was not touched.
- **The Android checkout:** the same series, configured with section 8's command plus
  `-DREXGLUE_PPC_TEST_BIN_DIR` from `sdk/ppc-test-data` 0ffbb64. macOS has no PowerPC binutils.
- **Build:** NDK r30 (30.0.16248370), configure 21 s, build 1:51, no errors.
- **The runs:** `tools/android/run_sdk_tests.sh -s <serial> <checkout>/out/android-arm64/Release`.
  The full logs are in `sdk-tests/`, with the checkout's path replaced by `<sdk checkout>`.

## Results

| Device | Page size | API | `ppc_tests` | `unit_tests` |
|---|---|---|---|---|
| Emulator `sdk_gphone16k_arm64` (`system-images;android-36;google_apis_ps16k;arm64-v8a`, on the M2) | **16384** | 36 | **1492 of 1492** cases (5815 assertions) | 266 passed, 4 failed, 4 skipped, of 274 (1484 of 1490 assertions) |
| POCO X7 5G (24095PCADG) | **4096** | 36 | **1492 of 1492** cases (5815 assertions) | 266 passed, 4 failed, 4 skipped, of 274 (1484 of 1490 assertions) |

The same cases fail on both. These pass on both page sizes:

- every `[memory]` case: `ASharedMemory`, and patch 37's offset at 4 and at 16 KB;
- the fibers and Bionic's threads;
- `ppc_tests` whole;
- every `chrono_test` case (Linux x86-64 fails four of them, D29).

## The failures

1. **`codegen/output_stamp_test.cpp:227-228`:** known, the same on every platform.
2. **`core/unclaimed_fault_test.cpp:100` and `:106`:** `REQUIRE(WIFEXITED(status))`, in "An
   unclaimed fault reaches the handler installed before the SDK's" and "A fault a handler claims
   still retries and succeeds".
3. **`system/guest_fatal_test.cpp:124` and `:129`:** "A fatal guest error aborts the process",
   expecting SIGABRT.
4. **Skipped:** `stream_test.cpp:297/310/322/336`, the SDK's own skips ("BitStream::Write is
   broken"), the same as on desktop.

**Cause of 2 and 3: the test harness, not the exception handler or the fatal hook.**

- `RunTestInChild` (`tests/unit/child_process.h`) re-runs the test executable from
  `rex::filesystem::GetExecutablePath()`.
- On Android, patch 34 makes that function return the library that contains it
  (`src/core/filesystem_posix.cpp:97-105`, `dladdr`): right for an app, whose process is
  `app_process`, but here it returns `librexruntime.so`.
- So the child `execl`s `librexruntime.so`, which dies with SIGSEGV. Run by hand on the emulator,
  `./librexruntime.so` exits with 139.
- Run directly (`./unit_tests "<child case>"`, on the emulator), every child case gives the
  expected result:

  | Child case | Exit status | Meaning |
  |---|---|---|
  | `child: a claimed fault` | 0 | |
  | `child: an unclaimed fault with a previous handler` | 42 | the SDK saw it once, then the previous handler |
  | `child: an unclaimed fault` | 139 | SIGSEGV, as its test wants |
  | `child: a fatal guest error with no handler` | 134 | SIGABRT |
  | `child: a fatal guest error whose handler returns` | 134 | SIGABRT |

- "An unclaimed fault ends the process with its signal" passes only because the wrong `exec` also
  dies with SIGSEGV.
- **Suggested fix (the ReXGlue agent's call),** either of:
  - on Android, `GetExecutablePath` reads `/proc/self/exe` first and uses `dladdr` only when that is
    `app_process` or `app_process64`;
  - or the test helper uses `/proc/self/exe` itself.

## Cleanup

`run_sdk_tests.sh` deleted `/data/local/tmp/rexglue-tests` on both. The by-hand check on the
emulator was deleted as well, and the emulator was shut down.
