#!/bin/sh
# Builds and installs the ReXGlue SDK with this project's patches (patches/series): Release only by
# default, what CI and the published game use (docs/release-pipeline.md, REL.5); developers build
# every configuration (docs/BUILDING.md). Nothing of the game is involved.
#
# Usage: tools/deps/build_sdk.sh PREFIX [WORK_DIR] [CONFIGS]
#   PREFIX    where the SDK is installed (its lib/cmake/rexglue is what -DCMAKE_PREFIX_PATH finds)
#   WORK_DIR  the checkout and build directory (default, or empty: a temporary one, removed after)
#   CONFIGS   Release (default) or all (Debug, Release and RelWithDebInfo, for the dev presets)
# Needs: git, cmake >= 3.25, ninja, clang/clang++ (CC/CXX pick others), the SDK's system packages.
# Linux (x86-64) builds the SDK's linux-amd64 preset; macOS on Apple Silicon its mac-arm64 preset,
# with Apple's clang and the Command Line Tools (docs/macos-port.md, MAC.1).
set -eu

SDK_REPOSITORY=https://github.com/rexglue/rexglue-sdk.git
SDK_COMMIT=0c7b01a

[ $# -ge 1 ] || { echo "usage: $0 PREFIX [WORK_DIR]" >&2; exit 2; }
root=$(cd "$(dirname "$0")/../.." && pwd)
prefix=$(mkdir -p "$1" && cd "$1" && pwd)
work=${2:-}
case "${3:-Release}" in
  Release) configs=Release ;;
  all) configs="Debug;Release;RelWithDebInfo" ;;
  *) echo "CONFIGS must be Release or all" >&2; exit 2 ;;
esac
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
git -C "$src" submodule update --init --recursive --quiet

# The patches, in order; the Windows-only ones are skipped.
grep -vE '^(#|$)' "$root/patches/series" | while read -r patch kind; do
  [ "${kind:-}" = windows ] && continue
  echo "patch: $patch"
  git -C "$src" apply "$root/patches/$patch"
done

# The SDK's preset for this host. macOS: CMake 4 no longer passes the macOS SDK to the compiler by
# itself (CMAKE_OSX_SYSROOT), and 13.3 is the deployment target the SDK's own CI builds for.
case "$(uname -s)" in
  Linux) preset=linux-amd64; platform_options="" ;;
  Darwin) preset=mac-arm64
    platform_options="-DCMAKE_OSX_SYSROOT=macosx -DCMAKE_OSX_DEPLOYMENT_TARGET=13.3" ;;
  *) echo "unsupported host: $(uname -s)" >&2; exit 2 ;;
esac

cd "$src"
# shellcheck disable=SC2086
cmake --preset "$preset" $platform_options \
  -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
  -DCMAKE_CONFIGURATION_TYPES="$configs" -DCMAKE_DEFAULT_BUILD_TYPE=Release \
  -DCMAKE_CROSS_CONFIGS="$configs" -DCMAKE_DEFAULT_CONFIGS="$configs" \
  -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "out/build/$preset" --target install --parallel
echo "SDK installed in $prefix"
