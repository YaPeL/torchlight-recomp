#!/bin/sh
# Builds and installs the ReXGlue SDK with this project's patches (patches/series): Release only by
# default, what CI and the published game use (docs/release-pipeline.md, REL.5); developers build
# every configuration (docs/BUILDING.md). Nothing of the game is involved.
#
# Usage: tools/deps/build_sdk.sh [--tests [--ppc-test-data DIR]]
#                               PREFIX [WORK_DIR] [CONFIGS] [TARGET]
#   --tests   also builds the SDK's own tests (unit_tests, ppc_tests) and runs them with ctest;
#             fails if one fails, is skipped or does not run (the sdk-tests workflow; host only)
#   --ppc-test-data DIR  the PPC tests' .bin and .map files assembled elsewhere
#             (tools/deps/build_ppc_test_data.sh), for hosts without the PowerPC binutils (macOS)
#   PREFIX    where the SDK is installed (its lib/cmake/rexglue is what -DCMAKE_PREFIX_PATH finds)
#   WORK_DIR  the checkout and build directory (default, or empty: a temporary one, removed after)
#   CONFIGS   Release (default) or all (Debug, Release and RelWithDebInfo, for the dev presets)
#   TARGET    host (default) or android-arm64: the NDK's arm64-v8a, API 30, through
#             cmake/android-arm64.toolchain.cmake and tools/deps/android/CMakeUserPresets.json
#             (docs/android-port.md; built by the android workflow, compile only)
# Needs: git, cmake >= 3.25, ninja, clang/clang++ (CC/CXX pick others), the SDK's system packages.
# Linux (x86-64) builds the SDK's linux-amd64 preset; macOS on Apple Silicon its mac-arm64 preset,
# with Apple's clang and the Command Line Tools (docs/macos-port.md, MAC.1).
set -eu

SDK_REPOSITORY=https://github.com/rexglue/rexglue-sdk.git
SDK_COMMIT=bd833a2

tests=false ppc_test_data=""
while [ $# -gt 0 ]; do
  case "$1" in
    --tests) tests=true; shift ;;
    --ppc-test-data) [ $# -ge 2 ] || { echo "--ppc-test-data needs a folder" >&2; exit 2; }
      ppc_test_data=$(cd "$2" && pwd); shift 2 ;;
    *) break ;;
  esac
done
[ $# -ge 1 ] || { echo "usage: $0 [--tests [--ppc-test-data DIR]] PREFIX [WORK_DIR]" >&2; exit 2; }
root=$(cd "$(dirname "$0")/../.." && pwd)
prefix=$(mkdir -p "$1" && cd "$1" && pwd)
work=${2:-}
case "${3:-Release}" in
  Release) configs=Release ;;
  all) configs="Debug;Release;RelWithDebInfo" ;;
  *) echo "CONFIGS must be Release or all" >&2; exit 2 ;;
esac
target=${4:-host}
case "$target" in
  host|android-arm64) ;;
  *) echo "TARGET must be host or android-arm64" >&2; exit 2 ;;
esac
if [ -n "$ppc_test_data" ] && [ "$tests" != true ]; then
  echo "--ppc-test-data only goes with --tests" >&2
  exit 2
fi
if [ "$tests" = true ] && [ "$target" != host ]; then
  echo "--tests runs the tests here, so only for the host (tools/android/run_sdk_tests.sh runs" \
    "them on a device)" >&2
  exit 2
fi
if [ -z "$work" ]; then
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
fi
mkdir -p "$work"
src="$work/rexglue-sdk"

if [ ! -d "$src/.git" ]; then
  git clone --quiet "$SDK_REPOSITORY" "$src"
fi
git -C "$src" checkout --quiet --force "$SDK_COMMIT"
git -C "$src" clean -fdxq -e out
# --force: a reused WORK_DIR keeps what patches changed inside submodules (thirdparty/sdl3), which
# a plain update leaves in place; the patches below would then not apply again.
git -C "$src" submodule update --init --recursive --force --quiet

# The patches, in order; the Windows-only ones are skipped.
grep -vE '^(#|$)' "$root/patches/series" | while read -r patch kind; do
  [ "${kind:-}" = windows ] && continue
  echo "patch: $patch"
  git -C "$src" apply "$root/patches/$patch"
done

# The SDK's preset for this host. macOS: CMake 4 no longer passes the macOS SDK to the compiler by
# itself (CMAKE_OSX_SYSROOT), and 13.3 is the deployment target the SDK's own CI builds for.
# Android: the SDK has no preset for it; ours is copied into the checkout as its user presets.
case "$target-$(uname -s)" in
  android-arm64-*) preset=android-arm64
    cp "$root/tools/deps/android/CMakeUserPresets.json" "$src/CMakeUserPresets.json"
    platform_options="-DCMAKE_TOOLCHAIN_FILE=$root/cmake/android-arm64.toolchain.cmake" ;;
  host-Linux) preset=linux-amd64; platform_options="" ;;
  host-Darwin) preset=mac-arm64
    platform_options="-DCMAKE_OSX_SYSROOT=macosx -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3" ;;
  *) echo "unsupported host: $(uname -s)" >&2; exit 2 ;;
esac

tests_on=""
[ "$tests" = true ] && tests_on="-DREXGLUE_BUILD_TESTS=ON"

cd "$src"
compilers="-DCMAKE_C_COMPILER=${CC:-clang} -DCMAKE_CXX_COMPILER=${CXX:-clang++}"
[ "$target" = host ] || compilers=""  # the toolchain file picks the NDK's clang
# shellcheck disable=SC2086
cmake --preset "$preset" $platform_options $compilers $tests_on \
  ${ppc_test_data:+"-DREXGLUE_PPC_TEST_BIN_DIR=$ppc_test_data"} \
  -DCMAKE_CONFIGURATION_TYPES="$configs" -DCMAKE_DEFAULT_BUILD_TYPE=Release \
  -DCMAKE_CROSS_CONFIGS="$configs" -DCMAKE_DEFAULT_CONFIGS="$configs" \
  -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "out/build/$preset" --target install --parallel
echo "SDK installed in $prefix"

if [ "$tests" = true ]; then
  # Every test has to run and pass, and the only skips are the platform's in
  # tools/deps/sdk_test_skips.txt: checked from ctest's JUnit file (check_sdk_tests.py), not from
  # its text.
  # One at a time: the SDK's tests are not written for parallel processes (fixed temporary
  # folder names, a guest memory named by the clock's tick), and in parallel they collided on
  # macOS.
  case "$(uname -s)" in Darwin) platform=macos ;; *) platform=linux ;; esac
  rm -f "$work/sdk-tests.xml"  # a reused WORK_DIR's from an earlier run cannot pass the check
  ctest --test-dir "out/build/$preset" -C Release --output-on-failure --no-tests=error \
    --output-junit "$work/sdk-tests.xml" || true
  python3 "$root/tools/deps/check_sdk_tests.py" "$work/sdk-tests.xml" "$platform"
fi
