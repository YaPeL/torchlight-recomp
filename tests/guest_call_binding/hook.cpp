// The hook over the fixture's sub_2: a strong definition in the same link, as in src/hooks. Its
// call to __imp__sub_2 sits in two outlined pieces, named as the machine outliner names them, as
// AppleClang's outliner left the game's hook sub_8276A858 with a PGO profile: reached from the
// hook only, they are no bypass. In BYPASS a generated function reaches them too (fixture.cpp).

extern "C" void __imp__sub_2(int* calls);

extern "C" __attribute__((noinline)) void OUTLINED_FUNCTION_90002(int* calls) {
  __imp__sub_2(calls);
}

extern "C" __attribute__((noinline)) void OUTLINED_FUNCTION_90001(int* calls) {
  OUTLINED_FUNCTION_90002(calls);
  *calls += 1;
}

extern "C" void sub_2(int* calls) {
  OUTLINED_FUNCTION_90001(calls);
  *calls += 10;
}
