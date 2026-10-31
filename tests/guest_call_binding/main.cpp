// The desktop fixtures are executables, as the game is there: an entry that calls the guest code.

extern "C" void sub_1(int* calls);

int main() {
  int calls = 0;
  sub_1(&calls);
  return calls;
}
