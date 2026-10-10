#!/bin/sh
# Makes "Torchlight Recomp.app" from a `cmake --install` tree (the top CMakeLists.txt's macOS rules:
# MacOS/, Frameworks/, PlugIns/ogre/, Resources/), as make_appimage.sh does for Linux: the tree as
# the bundle's Contents/, the rpaths that find Frameworks/ from the plugins, Info.plist, the icon
# (rendered from the project's SVG) and an ad-hoc signature on every Mach-O and on the bundle
# (Apple Silicon runs only signed code; the beta is not signed with a Developer ID,
# docs/macos-port.md section 6). Run split_symbols.sh on the tree first: signing comes last.
# Usage: make_app.sh INSTALL_DIR OUTPUT_DIR VERSION
#   VERSION  e.g. 0.3.0 (the tag without its "v"), Info.plist's version
# Needs the Command Line Tools (install_name_tool, codesign, iconutil, swift).
set -eu
[ $# -eq 3 ] || { echo "usage: $0 INSTALL_DIR OUTPUT_DIR VERSION" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
install_dir=$(cd "$1" && pwd)
mkdir -p "$2"
app="$(cd "$2" && pwd)/Torchlight Recomp.app"
version=$3
[ -x "$install_dir/MacOS/torchlight" ] || { echo "no MacOS/torchlight in $1" >&2; exit 1; }

rm -rf "$app"
mkdir -p "$app/Contents"
cp -R "$install_dir/." "$app/Contents/"
contents="$app/Contents"

# The SDK's GPU plugin (next to the executable) and OGRE's plugins find the libraries in
# Frameworks/; the executable has its rpath from the install (@executable_path/../Frameworks). The
# SDK's libraries come with @loader_path/../lib (its install's layout), which is nothing here.
for library in "$contents"/MacOS/librexgpu-*.dylib "$contents"/Frameworks/librex*.dylib; do
  install_name_tool -delete_rpath @loader_path/../lib "$library"
done
for plugin in "$contents"/MacOS/librexgpu-*.dylib; do
  install_name_tool -add_rpath @loader_path/../Frameworks "$plugin"
done
for plugin in "$contents"/PlugIns/ogre/*.dylib; do
  install_name_tool -add_rpath @loader_path/../../Frameworks "$plugin"
done

sed "s/@VERSION@/$version/g" "$here/Info.plist.in" > "$contents/Info.plist"
plutil -lint "$contents/Info.plist" >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
swift "$here/make_icon.swift" "$here/../linux/torchlight-recomp.svg" "$work/icon.iconset"
iconutil -c icns "$work/icon.iconset" -o "$contents/Resources/torchlight-recomp.icns"

# Inside out: every library and plugin, then the bundle (its executable and the resources' seal).
find "$contents/Frameworks" "$contents/PlugIns" "$contents/MacOS" -type f -name '*.dylib' |
  while read -r library; do
    codesign --force --sign - --timestamp=none "$library"
  done
codesign --force --sign - --timestamp=none "$app"
codesign --verify --deep --strict "$app"
echo "app: $app"
