#!/bin/sh
# Downloads the AppImage tools at pinned versions, checked by SHA-256, into DIR as `linuxdeploy`
# and `appimagetool` (put DIR in PATH for make_appimage.sh).
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
