#!/bin/sh
# Checks "Torchlight Recomp.app" (make_app.sh) as check_zip.ps1 does the Windows zip: every library
# each Mach-O loads is the system's (/usr/lib, /System/Library) or found inside the bundle through
# that file's own rpaths; no rpath points outside the bundle; Contents/MacOS holds only code; the
# signature verifies.
# Usage: check_app.sh APP
set -eu
[ $# -eq 1 ] || { echo "usage: $0 APP" >&2; exit 2; }
app=$(cd "$1" && pwd -P)
contents="$app/Contents"
[ -x "$contents/MacOS/torchlight" ] || { echo "no Contents/MacOS/torchlight in $1" >&2; exit 1; }
failures=$(mktemp)
trap 'rm -f "$failures"' EXIT
fail() { echo "check_app: $*" | tee -a "$failures" >&2; }

find "$contents" -type f | while read -r file; do
  file -b "$file" | grep -q '^Mach-O' || {
    case "$file" in "$contents/MacOS/"*) fail "not code in Contents/MacOS: ${file#"$app"/}" ;; esac
    continue
  }
  relative=${file#"$app"/}
  dir=$(dirname "$file")
  # The file's rpaths, with @loader_path and @executable_path made absolute.
  rpaths=$(otool -l "$file" | awk '/cmd LC_RPATH/ {getline; getline; print $2}' |
           sed -e "s#^@loader_path#$dir#" -e "s#^@executable_path#$contents/MacOS#")
  echo "$rpaths" | while read -r rpath; do
    [ -n "$rpath" ] || continue
    case "$(cd "$rpath" 2>/dev/null && pwd -P)/" in
      "$app"/*) ;;
      /) fail "$relative: rpath $rpath does not exist" ;;
      *) fail "$relative: rpath $rpath is outside the bundle" ;;
    esac
  done
  # The libraries it loads (a dylib's first line is its own name).
  otool -L "$file" | tail -n +2 | awk '{print $1}' | while read -r library; do
    [ "$library" = "$(otool -D "$file" | tail -n +2)" ] && continue
    case "$library" in
      /usr/lib/*|/System/Library/*) continue ;;
      @rpath/*)
        name=${library#@rpath/}
        echo "$rpaths" | while read -r rpath; do
          [ -n "$rpath" ] && [ -f "$rpath/$name" ] && echo found
        done | grep -q found || fail "$relative: $library not found in the bundle" ;;
      @loader_path/*)
        [ -f "$dir/${library#@loader_path/}" ] || fail "$relative: $library not found" ;;
      @executable_path/*)
        [ -f "$contents/MacOS/${library#@executable_path/}" ] ||
          fail "$relative: $library not found" ;;
      *) fail "$relative: loads $library, outside the bundle and the system" ;;
    esac
  done
done
codesign --verify --deep --strict "$app" || fail "the signature does not verify"
if [ -s "$failures" ]; then
  echo "check_app: $(wc -l < "$failures" | tr -d ' ') problem(s)" >&2
  exit 1
fi
echo "check_app: $app is complete"
