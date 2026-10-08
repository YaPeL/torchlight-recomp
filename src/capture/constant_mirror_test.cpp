// ConstantMirror (constant_mirror.h): which constant ranges the backend already holds.

#include <cstdint>
#include <cstdio>
#include <vector>

#include "capture/constant_mirror.h"

namespace {

using torchlight::capture::ConstantMirror;
using torchlight::commands::AutoConstant;

int failures = 0;

void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

constexpr bool kFloats = true;
constexpr uint8_t kVertex = 0, kFragment = 1;

}  // namespace

int main() {
  ConstantMirror m;
  // A block of three ranges (a matrix, a colour, a scalar) sent once.
  std::vector<uint32_t> matrix(16), colour{1, 2, 3, 4}, scalar{7};
  for (uint32_t i = 0; i < 16; ++i) matrix[i] = 100 + i;
  Check(!m.Holds(kFloats, kVertex, 0, matrix.data(), 16), "nothing held at first");
  m.Store(kFloats, kVertex, 0, matrix.data(), 16);
  m.Store(kFloats, kVertex, 16, colour.data(), 4);
  m.Store(kFloats, kVertex, 20, scalar.data(), 1);
  // The same block bound again: every range is held.
  Check(m.Holds(kFloats, kVertex, 0, matrix.data(), 16), "matrix held");
  Check(m.Holds(kFloats, kVertex, 16, colour.data(), 4), "colour held");
  Check(m.Holds(kFloats, kVertex, 20, scalar.data(), 1), "scalar held");
  // One constant changes (the colour's third value): only that range is sent again.
  colour[2] = 33;
  Check(m.Holds(kFloats, kVertex, 0, matrix.data(), 16), "matrix still held");
  Check(!m.Holds(kFloats, kVertex, 16, colour.data(), 4), "changed colour not held");
  Check(m.Holds(kFloats, kVertex, 20, scalar.data(), 1), "scalar still held");
  m.Store(kFloats, kVertex, 16, colour.data(), 4);
  Check(m.Holds(kFloats, kVertex, 16, colour.data(), 4), "changed colour held once sent");
  // Another block writing the same physical indices overwrites what the backend holds: the first
  // block's matrix is no longer held (the backend keeps one array per stage).
  std::vector<uint32_t> other(16, 9);
  m.Store(kFloats, kVertex, 0, other.data(), 16);
  Check(!m.Holds(kFloats, kVertex, 0, matrix.data(), 16), "overwritten by another block");
  // A range partly beyond what was ever sent, or partly unknown, is not held.
  Check(!m.Holds(kFloats, kVertex, 18, matrix.data(), 8), "partly unknown range");
  // Stages, and floats against ints, are separate arrays.
  Check(!m.Holds(kFloats, kFragment, 20, scalar.data(), 1), "other stage");
  Check(!m.Holds(!kFloats, kVertex, 20, scalar.data(), 1), "ints apart from floats");
  // Auto constants and the transpose flag: replaced by every command.
  std::vector<AutoConstant> autos(1);
  autos[0].raw_type = 5;
  autos[0].physical_index = 0;
  Check(!m.SameTail(kVertex, autos, true), "no tail at first");
  m.StoreTail(kVertex, autos, true);
  Check(m.SameTail(kVertex, autos, true), "same tail");
  Check(!m.SameTail(kVertex, autos, false), "transpose differs");
  autos[0].data = 1;
  Check(!m.SameTail(kVertex, autos, true), "autos differ");
  // A new live session: nothing held.
  m.Reset();
  Check(!m.Holds(kFloats, kVertex, 20, scalar.data(), 1), "reset");
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::puts("constant_mirror_test: ok");
  return 0;
}
