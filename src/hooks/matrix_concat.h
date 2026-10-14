// Matrix4::concatenate (guest_abi ogre::matrix4::kConcatenate) as the recompiled code computes it,
// on the host (docs/game-thread-parallelism.md, section 12): each of the 16 results is the row's
// second element times the column's second, then three fused multiply-adds in the guest's order
// (elements 0, 2, 3), every step a double operation rounded to float as the generated code does,
// so the result is the guest's bit for bit. Pure over a guest memory base, tested on a fake memory.

#pragma once

#include <cstdint>

namespace torchlight::hooks {

// Writes the 16 floats of a * b (OGRE row-major, m[row][col]) at `result`. The three may alias:
// both operands are read before anything is written, as the guest's result would be.
void ConcatenateMatrices(uint8_t* membase, uint32_t result, uint32_t a, uint32_t b);

}  // namespace torchlight::hooks
