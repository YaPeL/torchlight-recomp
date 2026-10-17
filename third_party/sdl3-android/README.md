# SDL 3.4.14, Android Java sources

`java/org/libsdl/app/` is SDL's `android-project/app/src/main/java/org/libsdl/app/` at the tag
`release-3.4.14` (https://github.com/libsdl-org/SDL), unchanged; `LICENSE.txt` is SDL's (Zlib).
The APK (`packaging/android/`, `tools/android/build_apk.sh`) compiles them with our activity.

They must be the same version as the SDL built into the SDK's `librexruntime.so`
(`thirdparty/sdl3` of the ReXGlue SDK): the Java side and the native side call each other through
JNI. `tools/android/build_apk.sh` compares this version with the SDK install's `SDL_version.h` and
stops when they differ; update both together.
