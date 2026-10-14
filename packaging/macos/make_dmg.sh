#!/bin/sh
# Makes the release's disk image from "Torchlight Recomp.app" (make_app.sh): the app and a link to
# /Applications to drag it to, compressed (UDZO). hdiutil is part of macOS.
# Usage: make_dmg.sh APP OUTPUT.dmg
set -eu
[ $# -eq 2 ] || { echo "usage: $0 APP OUTPUT.dmg" >&2; exit 2; }
[ -d "$1/Contents/MacOS" ] || { echo "$1 is not an app bundle" >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -R "$1" "$work/"
ln -s /Applications "$work/Applications"
rm -f "$2"
hdiutil create -quiet -volname "Torchlight Recomp" -srcfolder "$work" -fs APFS -format UDZO "$2"
echo "dmg: $2"
