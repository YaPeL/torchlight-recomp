#!/bin/sh
# Makes the AppImage from a `cmake --install` tree (docs/release-pipeline.md, REL.6): the tree in
# AppDir/usr as installed (bin/: the executable, GPU plugins, data/ui, ogre/; lib/: the SDK runtime
# and OGRE, RUNPATH $ORIGIN/../lib), the libraries they need that hosts do not have (linuxdeploy,
# with AppImage's exclusion list, and never libwayland-*), the build's libstdc++/libgcc_s in
# usr/lib/compat for AppRun's check, our desktop file and icon. appimagetool makes the image with
# the pinned static runtime get_tools.sh puts next to it (runtime-x86_64).
# Usage: make_appimage.sh INSTALL_DIR OUTPUT.AppImage [EXECUTABLE]
#   EXECUTABLE  what AppRun starts (default torchlight; replay for CI's package-check)
# Needs linuxdeploy and appimagetool in PATH with runtime-x86_64 next to appimagetool (CI downloads
# pinned versions with get_tools.sh), objcopy, and the
# compiler the tree was built with (for its libstdc++).
set -eu
[ $# -ge 2 ] || { echo "usage: $0 INSTALL_DIR OUTPUT.AppImage [EXECUTABLE]" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
install_dir=$(cd "$1" && pwd)
output=$2
executable=${3:-torchlight}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
appdir="$work/AppDir"
mkdir -p "$appdir/usr"
cp -a "$install_dir/." "$appdir/usr/"
[ -x "$appdir/usr/bin/$executable" ] || { echo "no usr/bin/$executable in $1" >&2; exit 1; }

mkdir -p "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/scalable/apps"
sed "s/^Exec=.*/Exec=$executable/" "$here/torchlight-recomp.desktop" \
  > "$appdir/usr/share/applications/torchlight-recomp.desktop"
cp "$here/torchlight-recomp.svg" "$appdir/usr/share/icons/hicolor/scalable/apps/"

# The libraries the executable and the plugins it loads need, from the build image. Besides
# AppImage's exclusion list (pkg2appimage's excludelist, which linuxdeploy applies), libraries that
# only excluded libraries need are left to the system too, so they come with them: libXau and
# libXdmcp (needed by libxcb.so.1), libbsd (by libXdmcp), libmd (by libbsd), libffi (by
# libwayland-client.so.0). No binary of ours needs them directly.
deploy_only=""
for plugin in "$appdir"/usr/bin/librexgpu-*.so "$appdir"/usr/bin/ogre/plugins/*.so* \
              "$appdir"/usr/bin/ogre/plugins/wayland/*.so*; do
  [ -f "$plugin" ] && deploy_only="$deploy_only --deploy-deps-only=$plugin"
done
# shellcheck disable=SC2086
APPIMAGE_EXTRACT_AND_RUN=1 linuxdeploy --appdir "$appdir" \
  --executable "$appdir/usr/bin/$executable" $deploy_only \
  --desktop-file "$appdir/usr/share/applications/torchlight-recomp.desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/torchlight-recomp.svg" \
  --exclude-library 'libwayland-*' --exclude-library 'libXau.so.*' \
  --exclude-library 'libXdmcp.so.*' --exclude-library 'libbsd.so.*' \
  --exclude-library 'libmd.so.*' --exclude-library 'libffi.so.*'

# libstdc++ and libgcc_s of the build, for hosts older than the build image (AppRun decides).
mkdir -p "$appdir/usr/lib/compat"
for library in libstdc++.so.6 libgcc_s.so.1; do
  cp -L "$(${CXX:-c++} -print-file-name=$library)" "$appdir/usr/lib/compat/$library"
done
# linuxdeploy leaves AppRun as a link to the executable: writing through it would replace the
# executable with the script.
rm -f "$appdir/AppRun"
sed "s/@EXECUTABLE@/$executable/" "$here/AppRun.in" > "$appdir/AppRun"
chmod +x "$appdir/AppRun"

runtime="$(dirname "$(command -v appimagetool)")/runtime-x86_64"
[ -f "$runtime" ] || { echo "make_appimage: no AppImage runtime at $runtime (get_tools.sh)" >&2; exit 1; }
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 appimagetool --no-appstream --runtime-file "$runtime" \
  "$appdir" "$output"
echo "AppImage: $output"
