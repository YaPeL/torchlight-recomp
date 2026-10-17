// Android: SDLActivity loads the replay as a shared library and calls its SDL_main (the
// development APK that replays captures on a device, docs/android-port.md, stage A1). SDL_main.h
// turns this main into SDL_main; the replay's own main is compiled as ReplayMain there.
//
// The replay writes some files at relative paths (OGRE's log, tl_backend_ogre.log; the default
// --out folder): an app starts in "/", which it cannot write, so it works in the app's external
// files folder, which `adb pull` reads.

#include <unistd.h>

#include <SDL3/SDL_main.h>
#include <SDL3/SDL_system.h>

int ReplayMain(int argc, char** argv);

int main(int argc, char** argv) {
  if (const char* files = SDL_GetAndroidExternalStoragePath()) chdir(files);
  return ReplayMain(argc, argv);
}
