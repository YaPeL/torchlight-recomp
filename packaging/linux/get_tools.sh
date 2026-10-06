#!/bin/sh
# Downloads the AppImage tools at pinned versions, checked by SHA-256, into DIR as `linuxdeploy`
# and `appimagetool` (put DIR in PATH for make_appimage.sh), and the AppImage runtime appimagetool
# puts at the start of the image as `runtime-x86_64` next to them (make_appimage.sh passes it with
# --runtime-file: without it appimagetool would download whatever type2-runtime's continuous
# release is at that moment).
# Usage: get_tools.sh DIR
set -eu
[ $# -eq 1 ] || { echo "usage: $0 DIR" >&2; exit 2; }
mkdir -p "$1"
cd "$1"
get() {  # NAME URL SHA256
  curl -fsSL -o "$1" "$2"
  echo "$3  $1" | sha256sum -c - > /dev/null || { echo "get_tools: $1 does not match" >&2; exit 1; }
  chmod +x "$1"
}
get linuxdeploy \
  https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
  c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
get appimagetool \
  https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage \
  ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
# type2-runtime 8f39b89 (2026-09-28), the version THIRD_PARTY_NOTICES.md lists. It is only
# published as the continuous release, which upstream replaces with each new build: when it moves
# on, the check fails and the build stops until the pin (and the notices) are updated.
get runtime-x86_64 \
  https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 \
  156f4bdbde9c52d01814600013e0a273f0118dc2de98975f3c8c63427ec79074
