// The hook over the fixture's sub_2: a strong definition in the same link, as in src/hooks.

extern "C" void __imp__sub_2(int* calls);

extern "C" void sub_2(int* calls) {
  __imp__sub_2(calls);
  *calls += 10;
}
