// The pure parts of the automatic parameter replay (auto_params.h).

#include <cstdio>

#include "hooks/auto_params.h"

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}
}  // namespace

int main() {
  using namespace torchlight::hooks;
  Check(SupportedAutoConstant(22) && SupportedAutoConstant(122) && SupportedAutoConstant(4), "the game's types");
  Check(!SupportedAutoConstant(5) && !SupportedAutoConstant(84) && !SupportedAutoConstant(125) && !SupportedAutoConstant(126),
        "others, and out of range, are left to the guest");
  std::array<uint32_t, 16> m{};
  for (uint32_t i = 0; i < 16; ++i) m[i] = 100 + i;
  const auto t = TransposeWords(m);
  Check(t[0] == 100 && t[1] == 104 && t[4] == 101 && t[15] == 115 && t[7] == 113, "transpose permutes rows and columns");
  Check(TransposeWords(t) == m, "transposing twice is the identity");
  Check(MatrixWriteCount(12) == 12 && MatrixWriteCount(16) == 16 && MatrixWriteCount(64) == 16, "matrix writes at most 16 floats");
  Check(ColourWriteCount(3) == 3 && ColourWriteCount(4) == 4 && ColourWriteCount(16) == 4, "colour writes at most 4");
  if (failures) return 1;
  std::puts("auto_params_test: ok");
  return 0;
}
