// The host Matrix4::concatenate (matrix_concat.h) on a fake guest memory: the product of small
// integer matrices (exact in every order), the identity, aliasing of the result with an operand,
// and nothing else written.

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "guest_abi/ogre_layout.h"
#include "hooks/matrix_concat.h"

namespace {

namespace abi = torchlight::guest_abi;
using namespace torchlight::hooks;

int failures = 0;
void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

void WriteMatrix(uint8_t* m, uint32_t at, const float v[16]) {
  for (uint32_t i = 0; i < 16; ++i) abi::WriteU32(m, at + 4 * i, std::bit_cast<uint32_t>(v[i]));
}
bool SameMatrix(const uint8_t* m, uint32_t at, const float v[16]) {
  for (uint32_t i = 0; i < 16; ++i)
    if (abi::ReadF32(m, at + 4 * i) != v[i]) return false;
  return true;
}

constexpr uint32_t kResult = 0x1000, kA = 0x2000, kB = 0x3000;

}  // namespace

int main() {
  std::vector<uint8_t> m(0x4000);
  for (size_t i = 0; i < m.size(); ++i) m[i] = uint8_t(i * 7 + 1);
  const float a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  const float b[16] = {2, 0, 1, -1, 3, 1, 0, 2, -2, 4, 1, 0, 1, 1, 1, 1};
  // Row-major a * b, by hand.
  const float ab[16] = {1 * 2 + 2 * 3 + 3 * -2 + 4 * 1,  1 * 0 + 2 * 1 + 3 * 4 + 4 * 1,  1 * 1 + 2 * 0 + 3 * 1 + 4 * 1,  1 * -1 + 2 * 2 + 3 * 0 + 4 * 1,
                        5 * 2 + 6 * 3 + 7 * -2 + 8 * 1,  5 * 0 + 6 * 1 + 7 * 4 + 8 * 1,  5 * 1 + 6 * 0 + 7 * 1 + 8 * 1,  5 * -1 + 6 * 2 + 7 * 0 + 8 * 1,
                        9 * 2 + 10 * 3 + 11 * -2 + 12 * 1, 9 * 0 + 10 * 1 + 11 * 4 + 12 * 1, 9 * 1 + 10 * 0 + 11 * 1 + 12 * 1, 9 * -1 + 10 * 2 + 11 * 0 + 12 * 1,
                        13 * 2 + 14 * 3 + 15 * -2 + 16 * 1, 13 * 0 + 14 * 1 + 15 * 4 + 16 * 1, 13 * 1 + 14 * 0 + 15 * 1 + 16 * 1, 13 * -1 + 14 * 2 + 15 * 0 + 16 * 1};
  WriteMatrix(m.data(), kA, a);
  WriteMatrix(m.data(), kB, b);
  std::vector<uint8_t> before = m;
  ConcatenateMatrices(m.data(), kResult, kA, kB);
  Check(SameMatrix(m.data(), kResult, ab), "a * b of small integers");
  bool untouched = true;
  for (size_t i = 0; i < m.size(); ++i)
    if ((i < kResult || i >= kResult + 64) && m[i] != before[i]) untouched = false;
  Check(untouched, "only the result is written");

  const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  WriteMatrix(m.data(), kB, identity);
  ConcatenateMatrices(m.data(), kResult, kA, kB);
  Check(SameMatrix(m.data(), kResult, a), "a * identity");
  ConcatenateMatrices(m.data(), kResult, kB, kA);
  Check(SameMatrix(m.data(), kResult, a), "identity * a");

  // Aliasing: the result over an operand gives the same as a separate result.
  WriteMatrix(m.data(), kB, b);
  ConcatenateMatrices(m.data(), kA, kA, kB);
  Check(SameMatrix(m.data(), kA, ab), "result aliased with the first operand");
  WriteMatrix(m.data(), kA, a);
  ConcatenateMatrices(m.data(), kB, kA, kB);
  Check(SameMatrix(m.data(), kB, ab), "result aliased with the second operand");

  // Values where the rounding order shows: the result equals the fused sequence, not a plain sum.
  const float c[16] = {1.0000001f, 3.0000002f, 0.3f, 7.1f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  const float d[16] = {1.1f, 0, 0, 0, 2.2f, 0, 0, 0, 3.3f, 0, 0, 0, 4.4f, 0, 0, 0};
  WriteMatrix(m.data(), kA, c);
  WriteMatrix(m.data(), kB, d);
  ConcatenateMatrices(m.data(), kResult, kA, kB);
  double expect = float(double(c[1]) * double(d[4]));
  expect = float(std::fma(double(c[0]), double(d[0]), expect));
  expect = float(std::fma(double(c[2]), double(d[8]), expect));
  expect = float(std::fma(double(c[3]), double(d[12]), expect));
  Check(abi::ReadF32(m.data(), kResult) == float(expect), "the guest's fused order");

  if (failures == 0) std::printf("matrix_concat_test: ok\n");
  return failures == 0 ? 0 : 1;
}
