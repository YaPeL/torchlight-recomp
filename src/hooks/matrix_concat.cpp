#include "hooks/matrix_concat.h"

#include <bit>
#include <cmath>

#include "guest_abi/ogre_layout.h"

namespace torchlight::hooks {

namespace abi = torchlight::guest_abi;

void ConcatenateMatrices(uint8_t* membase, uint32_t result, uint32_t a, uint32_t b) {
  double lhs[4][4], rhs[4][4];
  for (uint32_t row = 0; row < 4; ++row) {
    for (uint32_t col = 0; col < 4; ++col) {
      lhs[row][col] = abi::ReadF32(membase, a + 16 * row + 4 * col);
      rhs[row][col] = abi::ReadF32(membase, b + 16 * row + 4 * col);
    }
  }
  for (uint32_t row = 0; row < 4; ++row) {
    for (uint32_t col = 0; col < 4; ++col) {
      // The guest's order: the second term as a product, then the first, third and fourth terms
      // as fused multiply-adds, each rounded to float.
      double sum = float(lhs[row][1] * rhs[1][col]);
      sum = float(std::fma(lhs[row][0], rhs[0][col], sum));
      sum = float(std::fma(lhs[row][2], rhs[2][col], sum));
      sum = float(std::fma(lhs[row][3], rhs[3][col], sum));
      abi::WriteU32(membase, result + 16 * row + 4 * col, std::bit_cast<uint32_t>(float(sum)));
    }
  }
}

}  // namespace torchlight::hooks
