#!/bin/sh
# Builds and installs OGRE 14.6.0 the way the backend uses it: GL3+ (EGL), the RTSS with its
# shaders, the STBI codec; and the GL3+ plugin a second time for Wayland windows, in
# lib/OGRE/wayland/ (OGRE builds its GL window support for X11 or for Wayland). The libraries find
# each other through RUNPATH $ORIGIN, so the install can move; linked --as-needed, so a library
# is a dependency only if its symbols are used (OGRE links all of X11_LIBRARIES: without it the
# GL3+ plugin asked for libSM, libICE and libXext and used none). One recipe for CI and developers
# (README; docs/release-pipeline.md, REL.5).
#
# Usage: tools/deps/build_ogre.sh PREFIX [WORK_DIR] [BUILD_TYPE]
#   BUILD_TYPE  Release (default; what CI and the published game use) or RelWithDebInfo
# Needs: git, cmake >= 3.25, ninja, clang/clang++ (CC/CXX pick others), libX11/libXrandr, EGL,
# Wayland and zlib development packages.
set -eu

OGRE_REPOSITORY=https://github.com/OGRECave/ogre.git
OGRE_TAG=v14.6.0

[ $# -ge 1 ] || { echo "usage: $0 PREFIX [WORK_DIR] [BUILD_TYPE]" >&2; exit 2; }
prefix=$(mkdir -p "$1" && cd "$1" && pwd)
work=${2:-}
build_type=${3:-Release}
if [ -z "$work" ]; then
  work=$(mktemp -d)
  trap 'rm -rf "$work"' EXIT
fi
mkdir -p "$work"
src="$work/ogre"
if [ ! -d "$src/.git" ]; then
  git clone --quiet --depth 1 --branch "$OGRE_TAG" "$OGRE_REPOSITORY" "$src"
fi

configure() {  # BUILD_DIR [extra options...]
  dir=$1
  shift
  cmake -S "$src" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$build_type" \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_INSTALL_RPATH='$ORIGIN;$ORIGIN/..;$ORIGIN/../..' \
    -DCMAKE_SHARED_LINKER_FLAGS=-Wl,--as-needed -DCMAKE_MODULE_LINKER_FLAGS=-Wl,--as-needed \
    -DOGRE_BUILD_DEPENDENCIES=OFF \
    -DOGRE_BUILD_RENDERSYSTEM_GL=OFF -DOGRE_BUILD_RENDERSYSTEM_GL3PLUS=ON \
    -DOGRE_BUILD_RENDERSYSTEM_GLES2=OFF -DOGRE_BUILD_RENDERSYSTEM_VULKAN=OFF \
    -DOGRE_BUILD_RENDERSYSTEM_TINY=OFF -DOGRE_BUILD_PLUGIN_STBI=ON -DOGRE_BUILD_PLUGIN_ASSIMP=OFF \
    -DOGRE_BUILD_PLUGIN_BSP=OFF -DOGRE_BUILD_PLUGIN_OCTREE=OFF -DOGRE_BUILD_PLUGIN_PFX=OFF \
    -DOGRE_BUILD_PLUGIN_DOT_SCENE=OFF -DOGRE_BUILD_PLUGIN_PCZ=OFF -DOGRE_BUILD_PLUGIN_CG=OFF \
    -DOGRE_BUILD_PLUGIN_FREEIMAGE=OFF -DOGRE_BUILD_PLUGIN_EXRCODEC=OFF \
    -DOGRE_BUILD_PLUGIN_GLSLANG=OFF -DOGRE_BUILD_PLUGIN_RSIMAGE=OFF \
    -DOGRE_BUILD_COMPONENT_RTSHADERSYSTEM=ON -DOGRE_BUILD_RTSHADERSYSTEM_SHADERS=ON \
    -DOGRE_BUILD_COMPONENT_OVERLAY=OFF -DOGRE_BUILD_COMPONENT_BITES=OFF \
    -DOGRE_BUILD_COMPONENT_PAGING=OFF -DOGRE_BUILD_COMPONENT_MESHLODGENERATOR=OFF \
    -DOGRE_BUILD_COMPONENT_TERRAIN=OFF -DOGRE_BUILD_COMPONENT_VOLUME=OFF \
    -DOGRE_BUILD_COMPONENT_PROPERTY=OFF -DOGRE_BUILD_COMPONENT_BULLET=OFF \
    -DOGRE_BUILD_COMPONENT_PYTHON=OFF -DOGRE_BUILD_COMPONENT_JAVA=OFF \
    -DOGRE_BUILD_COMPONENT_CSHARP=OFF -DOGRE_BUILD_SAMPLES=OFF -DOGRE_BUILD_TESTS=OFF \
    -DOGRE_BUILD_TOOLS=OFF -DOGRE_INSTALL_DOCS=OFF -DOGRE_INSTALL_SAMPLES=OFF \
    -DOGRE_INSTALL_TOOLS=OFF -DOGRE_CONFIG_ENABLE_ZIP=OFF "$@"
}

configure "$work/build"
cmake --build "$work/build" --parallel
cmake --install "$work/build"
# The GL3+ plugin for Wayland windows; the rest of OGRE does not change with OGRE_USE_WAYLAND.
# Built with its install RUNPATH, since it is copied out of the build tree.
configure "$work/build-wayland" -DOGRE_USE_WAYLAND=ON -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
cmake --build "$work/build-wayland" --parallel --target RenderSystem_GL3Plus
mkdir -p "$prefix/lib/OGRE/wayland"
cp -P "$work"/build-wayland/lib/RenderSystem_GL3Plus.so* "$prefix/lib/OGRE/wayland/"
echo "OGRE installed in $prefix"
