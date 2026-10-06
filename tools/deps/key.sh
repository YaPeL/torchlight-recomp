#!/bin/sh
# The key of the prebuilt SDK and OGRE of a platform (linux, the default, or windows): what decides
# their contents (the system they are built on, the build scripts, the patches, the toolchain
# script). CI publishes and looks them up as release deps-<key> on Linux and deps-windows-<key> on
# Windows (REL.5), and caches them under it.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
case "${1:-linux}" in
  # The system: the container the Linux jobs build in, the runner image the Windows ones build on;
  # the workflows use the same names (.github/workflows/ci.yml, release.yml).
  linux)
    image="container ubuntu:22.04"
    scripts="tools/deps/build_sdk.sh tools/deps/build_ogre.sh tools/deps/ubuntu_toolchain.sh" ;;
  # build_sdk.sh too: windows.ps1 reads the SDK's repository and commit from it.
  windows)
    image="runner windows-2022"
    scripts="tools/deps/build_sdk.sh tools/build-deps/windows.ps1 tools/build-deps/windows_toolchain.ps1" ;;
  *) echo "usage: $0 [linux|windows]" >&2; exit 2 ;;
esac
# shellcheck disable=SC2086
{ printf '%s\n' "$image"; cat $scripts patches/series patches/*.patch; } | sha256sum | cut -c1-16
