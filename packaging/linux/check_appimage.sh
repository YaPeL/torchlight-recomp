#!/bin/sh
# Checks an AppImage on this machine (CI runs it in clean ubuntu:22.04 and archlinux containers):
# every ELF file in it resolves its libraries (ldd, as AppRun runs it: the plugins the executable
# loads see the libraries it already loaded, usr/lib; and the bundled libstdc++ when AppRun would
# choose it, a host older than the build image); and with a replay image and a capture, the replay
# reproduces the capture (synthetic_capture's: an exact match).
# Usage: check_appimage.sh IMAGE.AppImage [CAPTURE.tlcap]
set -eu
[ $# -ge 1 ] || { echo "usage: $0 IMAGE.AppImage [CAPTURE.tlcap]" >&2; exit 2; }
# Both paths resolved before the cd below (a relative one, as CI passes them, would point into the
# work folder).
image=$(readlink -f "$1")
capture=
if [ $# -ge 2 ]; then
  capture=$(readlink -f "$2")
  [ -r "$capture" ] || { echo "check_appimage: cannot read the capture $2" >&2; exit 2; }
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
"$image" --appimage-extract > /dev/null
root="$work/squashfs-root"
# AppRun is our script and starts an ELF executable.
target=$(sed -n 's|^exec "$here/\(usr/bin/[^"]*\)".*|\1|p' "$root/AppRun")
[ -n "$target" ] && head -c 4 "$root/$target" | grep -q ELF || {
  echo "check_appimage: AppRun does not start an executable (${target:-none})" >&2; exit 1; }
# AppRun's choice of libstdc++ (AppRun.in).
glibcxx() {
  [ -r "$1" ] || { echo 0; return; }
  grep -ao 'GLIBCXX_3\.4\.[0-9]*' "$1" | sed 's/GLIBCXX_3\.4\.//' | sort -n | tail -1
}
host_lib=$(ldconfig -p 2>/dev/null | awk '/libstdc\+\+\.so\.6 .*x86-64/ { print $NF; exit }')
library_path="$root/usr/lib"
if [ "$(glibcxx "$root/usr/lib/compat/libstdc++.so.6")" -gt "$(glibcxx "${host_lib:-/nonexistent}")" ]; then
  library_path="$root/usr/lib/compat:$library_path"
  echo "check_appimage: the bundled libstdc++ is newer than the host's (AppRun uses it)"
fi
find "$root/usr" -type f | while read -r file; do
  head -c 4 "$file" | grep -q 'ELF' || continue
  out=$(LD_LIBRARY_PATH="$library_path" ldd "$file" 2>&1 || true)
  if echo "$out" | grep -qE 'not found|version .* not found'; then
    echo "MISSING in ${file#"$root"/}:"
    echo "$out" | grep 'not found'
    echo 1 > "$work/failed"
  fi
done
[ -f "$work/failed" ] && { echo "check_appimage: unresolved libraries" >&2; exit 1; }
echo "check_appimage: every library resolves"
if [ -n "$capture" ]; then
  "$root/AppRun" "$capture" --out "$work/replay" > "$work/replay.log" 2>&1 || {
    cat "$work/replay.log"; exit 1; }
  grep -q 'mean abs error 0.00' "$work/replay.log" || {
    grep -E 'draws|PSNR|error' "$work/replay.log"; echo "check_appimage: replay differs" >&2
    exit 1; }
  echo "check_appimage: replay matches the capture"
fi
