// The desktop fixtures are executables, as the game is there: an entry that calls the guest code.
// On Windows it also calls KERNEL32's RtlCaptureContext through an import thunk (fixture.cpp).

extern "C" void sub_1(int* calls);

#if defined(_WIN32)
extern "C" void sub_5(int* calls);
extern "C" void __stdcall RtlCaptureContext(void* context);  // no dllimport: a thunk in the map
#endif

int main() {
  int calls = 0;
  sub_1(&calls);
#if defined(_WIN32)
  alignas(16) static unsigned char context[2048];
  RtlCaptureContext(context);
  sub_5(&calls);
#endif
  return calls;
}
