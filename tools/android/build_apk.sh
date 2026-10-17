#!/bin/bash
# Builds the Android app (APK) without Gradle, from the native libraries a build made: the
# Android SDK's build tools compile packaging/android/ (manifest, resources, our activity) and
# SDL's Java sources (third_party/sdl3-android), and the APK carries the main library (the game's,
# or the replay's in a development build), the SDK's librexruntime.so and its null GPU plugin, and
# the NDK's libc++_shared.so. Android extracts them at install (extractNativeLibs, the manifest:
# the runtime loads its GPU plugin as a file next to librexruntime.so). They are still stored
# uncompressed and 16 KB aligned, which the device reads in place once that plugin is loaded by
# name (the ReXGlue agent's patch 42) and extraction is turned off again; the libraries themselves
# are linked for 16 KB pages.
#
# Usage: tools/android/build_apk.sh [options] MAIN_LIBRARY SDK_PREFIX OUT.apk
#   MAIN_LIBRARY  the .so whose SDL_main the activity calls (out/build/.../libtorchlight.so, or
#                 libreplay.so from the replay's Android build)
#   SDK_PREFIX    the ReXGlue SDK's Android install (lib/librexruntime.so, include/SDL3)
#   --version-code N, --version-name S   (default 1 and 0.0.0-dev)
#   --keep-debug  keep the libraries' debug information (default: stripped, as a release)
#   --android-sdk DIR  (default ~/android-sdk: platforms/android-36, build-tools/36.1.0, jdk/)
#   --ndk DIR     (default ~/android-sdk/ndk/30.0.16248370)
#   --ogre-media DIR  OGRE's media, whose Main and RTShaderLib the backend reads (default
#                 ~/android-work/ogre-install/share/OGRE-14.6/Media, build_ogre.sh android-arm64)
# The APK's assets also carry OGRE's media and data/ui, under resources/ and listed in
# resources.txt after a stamp of their content; the app copies them to internal storage when the
# stamp changes (platform_android.cpp).
# Development builds only, until stage A6: the package is io.github.yapel.tlrecomp.dev (it installs
# next to the app and is debuggable), signed with the development key (~/.android/debug.keystore,
# made by keytool if missing). The app's own package, io.github.yapel.tlrecomp, is only ever signed
# with the release key, by the release job (docs/android-port.md, A6): an app signed with another
# key would block the real one on the device until it is uninstalled, with the saves.
# Linux and macOS (GNU or BSD tools).
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
version_code=1 version_name=0.0.0-dev keep_debug=0
sdk="$HOME/android-sdk" ndk="$HOME/android-sdk/ndk/30.0.16248370"
ogre_media="$HOME/android-work/ogre-install/share/OGRE-14.6/Media"
platform=android-36 build_tools=36.1.0 min_sdk=30 target_sdk=36
usage() { sed -n '9,19p' "$0" >&2; exit 2; }
while [ $# -gt 0 ]; do
  case "$1" in
    --version-code) version_code=$2; shift ;;
    --version-name) version_name=$2; shift ;;
    --keep-debug) keep_debug=1 ;;
    --android-sdk) sdk=$2; shift ;;
    --ndk) ndk=$2; shift ;;
    --ogre-media) ogre_media=$2; shift ;;
    --) shift; break ;;
    -*) usage ;;
    *) break ;;
  esac
  shift
done
[ $# -eq 3 ] || usage
main=$1 prefix=$2 out=$3

tools="$sdk/build-tools/$build_tools"
android_jar="$sdk/platforms/$platform/android.jar"
java_home="$sdk/jdk"
for f in "$main" "$prefix/lib/librexruntime.so" "$prefix/lib/librexgpu-null.so" "$tools/aapt2" "$tools/d8" "$tools/zipalign" \
  "$tools/apksigner" "$android_jar" "$java_home/bin/javac" "$ogre_media/Main" \
  "$ogre_media/RTShaderLib"; do
  [ -e "$f" ] || { echo "build_apk: missing $f" >&2; exit 2; }
done
llvm=$(ls -d "$ndk"/toolchains/llvm/prebuilt/*/bin | head -n 1)
libcxx=$(ls "$ndk"/toolchains/llvm/prebuilt/*/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so \
  | head -n 1)
[ -n "$llvm" ] && [ -n "$libcxx" ] || { echo "build_apk: no NDK toolchain in $ndk" >&2; exit 2; }
main_name=$(basename "$main" .so)
main_name=${main_name#lib}

# SDL's Java and the SDL in librexruntime.so call each other: the same version (README there).
sdl_version() {
  awk '/#define SDL_(MAJOR|MINOR|MICRO)_VERSION/ {v = v (v ? "." : "") $3} END {print v}' "$1"
}
native_sdl=$(sdl_version "$prefix/include/SDL3/SDL_version.h")
java_sdl=$(sed -n 's/^# SDL \([0-9.]*\),.*/\1/p' "$root/third_party/sdl3-android/README.md")
if [ "$native_sdl" != "$java_sdl" ]; then
  echo "build_apk: SDL $native_sdl in the SDK, $java_sdl in third_party/sdl3-android" >&2
  exit 1
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
export JAVA_HOME="$java_home"  # for the build tools' own launch scripts, this process only

# Resources and manifest. The assets name the main library for the activity and carry the host's
# files with their list (the stamp first: a hash of every path and its content).
mkdir -p "$work/res" "$work/assets/resources/ogre/media" "$work/assets/resources/data"
printf '%s\n' "$main_name" > "$work/assets/main_library.txt"
cp -R "$ogre_media/Main" "$ogre_media/RTShaderLib" "$work/assets/resources/ogre/media/"
cp -R "$root/data/ui" "$work/assets/resources/data/"
(cd "$work/assets/resources" && find . -type f | sed 's|^\./||' | LC_ALL=C sort > "$work/files.txt"
 sha=$(command -v sha256sum || echo "shasum -a 256")
 stamp=$(while IFS= read -r f; do $sha "$f"; done < "$work/files.txt" | $sha | cut -c1-16)
 { echo "$stamp"; cat "$work/files.txt"; } > "$work/assets/resources.txt")
"$tools/aapt2" compile --dir "$root/packaging/android/res" -o "$work/res.zip"
# Debuggable: `adb shell run-as` reads a development build's internal storage.
"$tools/aapt2" link -o "$work/base.apk" -I "$android_jar" \
  --manifest "$root/packaging/android/AndroidManifest.xml" \
  --min-sdk-version "$min_sdk" --target-sdk-version "$target_sdk" \
  --version-code "$version_code" --version-name "$version_name" \
  --rename-manifest-package io.github.yapel.tlrecomp.dev --debug-mode \
  -A "$work/assets" "$work/res.zip"

# Java: SDL's classes and ours, to dex.
mkdir -p "$work/classes" "$work/dex"
find "$root/third_party/sdl3-android/java" "$root/packaging/android/java" -name '*.java' \
  > "$work/sources.txt"
"$java_home/bin/javac" -nowarn -Xlint:-options -source 8 -target 8 -bootclasspath "$android_jar" \
  -d "$work/classes" @"$work/sources.txt"
find "$work/classes" -name '*.class' > "$work/classes.txt"
"$tools/d8" --release --min-api "$min_sdk" --lib "$android_jar" --output "$work/dex" \
  @"$work/classes.txt"

# The libraries, uncompressed (stored), stripped unless asked otherwise.
mkdir -p "$work/apk/lib/arm64-v8a"
# The runtime loads its GPU plugin from its own folder: the native mode's null one (no Xenos on
# Android).
for lib in "$main" "$prefix/lib/librexruntime.so" "$prefix/lib/librexgpu-null.so" "$libcxx"; do
  dest="$work/apk/lib/arm64-v8a/$(basename "$lib")"
  if [ "$keep_debug" = 1 ]; then cp "$lib" "$dest"; else "$llvm/llvm-strip" --strip-debug -o "$dest" "$lib"; fi
done
cp "$work/base.apk" "$work/unaligned.apk"
(cd "$work/dex" && zip -q -X "$work/unaligned.apk" classes.dex)
(cd "$work/apk" && zip -q -X -0 -r "$work/unaligned.apk" lib)

# 16 KB alignment of the stored libraries (-P 16, for when they are read in place again), then the
# development signature.
"$tools/zipalign" -f -P 16 4 "$work/unaligned.apk" "$work/aligned.apk"
keystore="$HOME/.android/debug.keystore"
if [ ! -f "$keystore" ]; then
  mkdir -p "$HOME/.android"
  "$java_home/bin/keytool" -genkeypair -keystore "$keystore" -storepass android -keypass android \
    -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
    -dname "CN=Android Debug,O=Android,C=US" >/dev/null
fi
"$tools/apksigner" sign --ks "$keystore" --ks-pass pass:android --ks-key-alias androiddebugkey \
  --out "$out" "$work/aligned.apk"
"$tools/zipalign" -c -P 16 4 "$out"
echo "build_apk: $out ($(du -h "$out" | cut -f1); main library lib$main_name.so)"
