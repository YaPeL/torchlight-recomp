// Recompiled code in miniature, for tools/android/check_guest_call_binding.py: each guest function
// is __imp__sub_X with a weak alias sub_X that callers use, the codegen's DEFINE_REX_FUNC on ELF.
// sub_2 is hooked (hook.cpp). FIXTURE_BYPASS adds a generated function that calls the hooked
// function's __imp__ directly, which the check has to report.

#define GUEST_FUNC(name)                                                            \
  extern "C" void __imp__##name(int* calls);                                        \
  extern "C" __attribute__((weak, noinline, alias("__imp__" #name))) void name(    \
      int* calls);                                                                  \
  extern "C" __attribute__((noinline)) void __imp__##name(int* calls)

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

GUEST_FUNC(sub_3) { *calls += 2; }

#ifdef FIXTURE_BYPASS
GUEST_FUNC(sub_4) {
  __imp__sub_2(calls);
  ++*calls;
}
#endif
