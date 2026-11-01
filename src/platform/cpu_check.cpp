// The x86-64-v3 Linux build's CPU check (TORCHLIGHT_X86_64_V3, CMakeLists.txt): the game's code is
// compiled for x86-64-v3 (AVX, AVX2, BMI1/2, F16C, FMA, LZCNT, MOVBE), so on an older CPU it would
// crash on its first such instruction. This file is compiled for baseline x86-64.
//
// The check runs from the executable's .preinit_array: the dynamic linker calls it before the
// initialisers of every shared library and every constructor of the executable, whatever their
// order. It has to stop the game there, before any initialiser: the executable exports symbols the
// libraries also use, so their initialisers can run its x86-64-v3 code (found with qemu: the SDK's
// librexruntime.so binds an inline rex::cvar::FlagEntry constructor to the executable's copy and
// crashed in its own initialiser). Without x86-64-v3 it writes the message to standard error with
// the write system call, shows it in SDL's message box (no SDL_Init needed; without a display it
// fails and the message on standard error stands) and exits. libc sets environ only in its own
// initialiser, after this one, so it is set here from the environment the loader passes: SDL starts
// its helper (zenity on Wayland) with it, which otherwise finds no display.

#if defined(__x86_64__) && defined(__linux__) && defined(TORCHLIGHT_X86_64_V3)

#include <unistd.h>

#include <SDL3/SDL_messagebox.h>

namespace {

constexpr const char* kTitle = "Torchlight Recomp";
constexpr char kMessage[] =
    "This build of Torchlight Recomp needs a processor with x86-64-v3 (AVX2, FMA, BMI2), such "
    "as the Steam Deck's or a PC processor from about 2015 on (Intel Haswell, AMD Zen or later). "
    "This processor does not have it.";

void CheckCpu(int, char**, char** envp) {
  __builtin_cpu_init();
  if (__builtin_cpu_supports("x86-64-v3")) return;
  if (!environ) environ = envp;
  const char prefix[] = "Torchlight Recomp: ";
  (void)!write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
  (void)!write(STDERR_FILENO, kMessage, sizeof(kMessage) - 1);
  (void)!write(STDERR_FILENO, "\n", 1);
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, kMessage, nullptr);
  _exit(1);
}

__attribute__((used, section(".preinit_array"))) void (*const kCheckCpu)(int, char**, char**) =
    &CheckCpu;

}  // namespace

#endif
