#!/bin/sh
# The key of the prebuilt SDK and OGRE of a platform (linux, the default, windows or macos): what
# decides their contents (the system they are built on, the build scripts, the patches, the
# toolchain script, the configurations built). CI publishes and looks them up under it in the
# dependency store (tools/deps/store.sh), and caches them under it.
#
# `key.sh windows-configs` prints the configurations CI builds for Windows, OGRE's then the SDK's
# (windows.ps1 -Configs, -SdkConfigs): ci.yml reads them from here, so a change of configuration
# changes the key. (On Linux the configurations are the build scripts' own, already hashed.)
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
windows_ogre_configs="RelWithDebInfo"
windows_sdk_configs="Release"
case "${1:-linux}" in
  # The system: the container the Linux jobs build in, the runner image the Windows ones build on;
  # the workflows use the same names (.github/workflows/ci.yml, release.yml).
  linux)
    system="container ubuntu:22.04"
    scripts="tools/deps/build_sdk.sh tools/deps/build_ogre.sh tools/deps/ubuntu_toolchain.sh" ;;
  # build_sdk.sh too: windows.ps1 reads the SDK's repository and commit from it.
  windows)
    system="runner windows-2022
ogre configs $windows_ogre_configs
sdk configs $windows_sdk_configs"
    scripts="tools/deps/build_sdk.sh tools/build-deps/windows.ps1 tools/build-deps/windows_toolchain.ps1" ;;
  # macOS on Apple Silicon: the runner image; the Command Line Tools come with it.
  macos)
    system="runner macos-26"
    scripts="tools/deps/build_sdk.sh tools/deps/build_ogre.sh" ;;
  windows-configs)
    echo "$windows_ogre_configs $windows_sdk_configs"
    exit 0 ;;
  *) echo "usage: $0 [linux|windows|macos|windows-configs]" >&2; exit 2 ;;
esac
# macOS before 26 has no sha256sum, only shasum; both print the digest first.
if command -v sha256sum > /dev/null; then
  sha256() { sha256sum; }
else
  sha256() { shasum -a 256; }
fi
# shellcheck disable=SC2086
{ printf '%s\n' "$system"; cat $scripts patches/series patches/*.patch; } | sha256 | cut -c1-16
