// Android: SDLActivity loads the replay as a shared library and calls its SDL_main (the
// development APK that replays captures on a device, docs/android-port.md, stage A1). SDL_main.h
// turns this main into SDL_main; the replay's own main is compiled as ReplayMain there.

#include <SDL3/SDL_main.h>

int ReplayMain(int argc, char** argv);

int main(int argc, char** argv) { return ReplayMain(argc, argv); }
