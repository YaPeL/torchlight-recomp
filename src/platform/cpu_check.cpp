// The x86-64-v3 Linux build's CPU check (TORCHLIGHT_X86_64_V3, CMakeLists.txt): the game's code is
// compiled for x86-64-v3 (AVX, AVX2, BMI1/2, F16C, FMA, LZCNT, MOVBE), so on an older CPU it would
// crash on its first such instruction. This file is compiled for baseline x86-64 and checks the
// CPU in a constructor of the highest user priority, which runs before every other constructor of
// the executable (the libraries it loads are not built for x86-64-v3) and before main: without
// x86-64-v3 it says so on standard error and in a message box, and exits.

#if defined(__x86_64__) && defined(__linux__) && defined(TORCHLIGHT_X86_64_V3)

#include <unistd.h>

#include <cstdio>

#include <SDL3/SDL_messagebox.h>

namespace {

constexpr const char* kTitle = "Torchlight Recomp";
constexpr const char* kMessage =
    "This build of Torchlight Recomp needs a processor with x86-64-v3 (AVX2, FMA, BMI2), such "
    "as the Steam Deck's or a PC processor from about 2015 on (Intel Haswell, AMD Zen or later). "
    "This processor does not have it.";

__attribute__((constructor(101))) void CheckCpu() {
  __builtin_cpu_init();
  if (__builtin_cpu_supports("x86-64-v3")) return;
  std::fprintf(stderr, "%s: %s\n", kTitle, kMessage);
  // SDL's message box needs no SDL_Init; without a display it fails and the message above stands.
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, kMessage, nullptr);
  _exit(1);
}

}  // namespace

#endif
