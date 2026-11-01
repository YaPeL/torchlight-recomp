// Recompiled code in miniature, for tools/android/check_guest_call_binding.py: each guest function
// is __imp__sub_X with a weak alias sub_X that callers use, the codegen's DEFINE_REX_FUNC on ELF
// and COFF; on Mach-O, which has no overridable alias, sub_X is a weak function tail-calling it.
// sub_2 is hooked (hook.cpp); sub_3 has a cold piece split off. FIXTURE_BYPASS adds a generated
// function and makes the piece call the hooked function's __imp__ directly: two bypasses the check
// has to report.

#if defined(__APPLE__)
#define GUEST_FUNC(name)                                                            \
  extern "C" void __imp__##name(int* calls);                                        \
  extern "C" __attribute__((weak, noinline)) void name(int* calls) {                \
    [[clang::musttail]] return __imp__##name(calls);                                \
  }                                                                                 \
  extern "C" __attribute__((noinline)) void __imp__##name(int* calls)
#else
#define GUEST_FUNC(name)                                                            \
  extern "C" void __imp__##name(int* calls);                                        \
  extern "C" __attribute__((weak, noinline, alias("__imp__" #name))) void name(    \
      int* calls);                                                                  \
  extern "C" __attribute__((noinline)) void __imp__##name(int* calls)
#endif

extern "C" void sub_2(int* calls);
extern "C" void sub_3(int* calls);

GUEST_FUNC(sub_1) {
  sub_2(calls);
  ++*calls;
}

GUEST_FUNC(sub_2) {
  sub_3(calls);
  ++*calls;
}

// A piece the compiler split off a body, named and linked as clang makes them (a local
// __imp__X.cold.1): the check has to leave it out of the guest functions, since it has no
// sub_3.cold.1 beside it, and still check its branches: in BYPASS it also calls the hooked
// function's __imp__, a second bypass the check has to report.
#define FIXTURE_STRING(x) #x
#define FIXTURE_LABEL(prefix, name) FIXTURE_STRING(prefix) name
static __attribute__((noinline, cold)) void FixtureColdPiece(int* calls) __asm__(
    FIXTURE_LABEL(__USER_LABEL_PREFIX__, "__imp__sub_3.cold.1"));
static void FixtureColdPiece(int* calls) {
#ifdef FIXTURE_BYPASS
  __imp__sub_2(calls);
#endif
  *calls -= 1;
}

GUEST_FUNC(sub_3) {
  if (*calls < 0) FixtureColdPiece(calls);
  *calls += 2;
}

#ifdef FIXTURE_BYPASS
GUEST_FUNC(sub_4) {
  __imp__sub_2(calls);
  ++*calls;
}
#endif
