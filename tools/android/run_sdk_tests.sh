#!/bin/bash
# Runs the ReXGlue SDK's test binaries (unit_tests, ppc_tests) for Android arm64 on a device or an
# emulator through adb, and keeps their output with the device's page size next to it. The same
# for a phone, a tablet or an arm64 emulator image (4 KB or 16 KB pages): adb is the only link.
# Patch 37 (patches/README.md) is what the page size tests; run it once on each kind.
#
# Usage: tools/android/run_sdk_tests.sh [-s SERIAL] [-n NDK] [-o OUT_DIR] BIN_DIR [-- CATCH_ARGS]
#   BIN_DIR     where the Android build put unit_tests, ppc_tests and the SDK's .so files
#               (<SDK checkout>/out/android-arm64/Release)
#   -s SERIAL   the device or emulator, as `adb devices` lists it (default: the only one attached)
#   -n NDK      the NDK, for libc++_shared.so (default: ~/android-sdk/ndk/30.0.16248370, the one
#               cmake/android-arm64.toolchain.cmake uses)
#   -o OUT_DIR  where the logs go (default: ./android-sdk-tests-<serial>-<page size>)
#   CATCH_ARGS  passed to both binaries (a test name or tags, e.g. "[memory]")
# Needs: adb, an arm64-v8a device or image with USB or emulator debugging on, Android 11 (API 30)
# or later. The binaries are built with the SDK's tests on (docs/rexglue-upstream.md, "Android: the
# SDK's tests on a device"). Exit status: 0 when both binaries pass.
set -eu

serial="" ndk="$HOME/android-sdk/ndk/30.0.16248370" out=""
while getopts "s:n:o:" opt; do
  case "$opt" in
    s) serial=$OPTARG ;;
    n) ndk=$OPTARG ;;
    o) out=$OPTARG ;;
    *) sed -n '7,14p' "$0" >&2; exit 2 ;;
  esac
done
shift $((OPTIND - 1))
[ $# -ge 1 ] || { sed -n '7,14p' "$0" >&2; exit 2; }
bin=$1; shift
[ "${1:-}" = "--" ] && shift
for f in unit_tests ppc_tests librexruntime.so; do
  [ -f "$bin/$f" ] || { echo "no $f in $bin" >&2; exit 2; }
done

adb_() { if [ -n "$serial" ]; then adb -s "$serial" "$@"; else adb "$@"; fi; }
libcxx=$(ls "$ndk"/toolchains/llvm/prebuilt/*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so \
  2>/dev/null | head -n 1)
[ -n "$libcxx" ] || { echo "no libc++_shared.so for aarch64 under $ndk" >&2; exit 2; }

adb_ wait-for-device
[ -n "$serial" ] || serial=$(adb get-serialno)
abi=$(adb_ shell getprop ro.product.cpu.abilist | tr -d '\r')
case ",$abi," in
  *,arm64-v8a,*) ;;
  *) echo "$serial is not arm64-v8a (ABIs: $abi)" >&2; exit 2 ;;
esac
page=$(adb_ shell getconf PAGE_SIZE | tr -d '\r')
api=$(adb_ shell getprop ro.build.version.sdk | tr -d '\r')
model=$(adb_ shell getprop ro.product.model | tr -d '\r')
[ -n "$out" ] || out="android-sdk-tests-$(echo "$serial" | tr -c 'A-Za-z0-9.\n-' _)-${page}"
mkdir -p "$out"
{
  echo "device: $model ($serial), API $api, ABIs $abi"
  echo "page size: $page bytes"
  echo "binaries: $bin"
  date -u +"run: %Y-%m-%dT%H:%M:%SZ"
} | tee "$out/device.txt"

# Everything in one directory the shell user can write: the binaries find their libraries through
# LD_LIBRARY_PATH, and the tests' temporary files go to TMPDIR (Android has no /tmp).
dir=/data/local/tmp/rexglue-tests
adb_ shell "rm -rf $dir && mkdir -p $dir/tmp"
adb_ push "$bin"/unit_tests "$bin"/ppc_tests "$bin"/*.so "$libcxx" "$dir/" > /dev/null
adb_ shell "chmod 755 $dir/unit_tests $dir/ppc_tests"

quoted=""
for arg in "$@"; do quoted="$quoted '$(printf %s "$arg" | sed "s/'/'\\\\''/g")'"; done
status=0
for test in unit_tests ppc_tests; do
  echo "== $test"
  # adb shell returns the remote status (Android 7 and later); the marker line is kept as well.
  adb_ shell "cd $dir && TMPDIR=$dir/tmp LD_LIBRARY_PATH=$dir ./$test$quoted; echo \"exit: \$?\"" \
    2>&1 | tr -d '\r' | tee "$out/$test.log" | tail -n 4
  code=$(sed -n 's/^exit: //p' "$out/$test.log" | tail -n 1)
  [ "$code" = 0 ] || status=1
done
adb_ shell "rm -rf $dir"
echo "logs: $out (page size $page)"
exit $status
