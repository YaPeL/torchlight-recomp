// The x86-64-v3 Linux build's CPU check (TORCHLIGHT_X86_64_V3, CMakeLists.txt): the game's code is
// compiled for x86-64-v3 (AVX, AVX2, BMI1/2, F16C, FMA, LZCNT, MOVBE), so on an older CPU it would
// crash on its first such instruction. This file is compiled for baseline x86-64.
//
// The check runs from the executable's .preinit_array: the dynamic linker calls it before the
// initialisers of every shared library and every constructor of the executable, whatever their
// order. Only the write system call is safe that early (the libraries, libc's stdio and SDL
// included, are not initialised yet), so it writes the message to standard error and records the
// result. The constructor below, of the highest user priority, runs before every other
// constructor of the executable (the shared libraries' initialisers before it are not built for
// x86-64-v3): it shows the message in SDL's message box and exits.

#if defined(__x86_64__) && defined(__linux__) && defined(TORCHLIGHT_X86_64_V3)

#include <unistd.h>

#include <SDL3/SDL_messagebox.h>

namespace {

constexpr const char* kTitle = "Torchlight Recomp";
constexpr char kMessage[] =
    "This build of Torchlight Recomp needs a processor with x86-64-v3 (AVX2, FMA, BMI2), such "
    "as the Steam Deck's or a PC processor from about 2015 on (Intel Haswell, AMD Zen or later). "
    "This processor does not have it.";

bool g_unsupported = false;

void CheckCpu(int, char**, char**) {
  __builtin_cpu_init();
  if (__builtin_cpu_supports("x86-64-v3")) return;
  g_unsupported = true;
  const char prefix[] = "Torchlight Recomp: ";
  (void)!write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
  (void)!write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
  (void)!write(STDERR_FILENO, "\n", 1);
}

__attribute__((used, section(".preinit_array"))) void (*const kCheckCpu)(int, char**, char**) =
    &CheckCpu;

__attribute__((constructor(101))) void ExitOnUnsupportedCpu() {
  if (!g_unsupported) return;
  // SDL's message box needs no SDL_Init; without a display it fails and the message written above
  // stands.
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, kMessage, nullptr);
  _exit(1);
}

}  // namespace

#endif
