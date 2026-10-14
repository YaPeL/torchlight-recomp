// The host replay of GpuProgramParameters::_updateAutoParams (auto_params_hooks.cpp): the pure
// parts, tested on their own. The data comes and goes as 32-bit words in the guest's byte order.

#pragma once

#include <array>
#include <cstdint>

#include "guest_abi/auto_params.h"

namespace torchlight::hooks {

// Whether the host replay handles an AutoConstantType (the ones the game's programs use); an entry
// of any other type leaves the whole update to the guest.
constexpr bool SupportedAutoConstant(uint32_t type) {
  using A = guest_abi::auto_params::Act;
  switch (static_cast<A>(type)) {
    case A::kWorldMatrix: case A::kInverseWorldMatrix: case A::kInverseTransposeWorldMatrix:
    case A::kWorldMatrixArray3x4: case A::kViewMatrix: case A::kViewProjMatrix:
    case A::kInverseTransposeWorldViewMatrix: case A::kWorldViewProjMatrix:
    case A::kSurfaceDiffuseColour: case A::kSurfaceSpecularColour: case A::kSurfaceEmissiveColour:
    case A::kSurfaceShininess: case A::kLightDiffuseColour: case A::kLightPositionViewSpace:
    case A::kDerivedAmbientLightColour: case A::kDerivedSceneColour:
    case A::kDerivedLightDiffuseColour: case A::kTextureViewProjMatrix: case A::kTextureMatrix:
      return true;
    default:
      return false;
  }
}

// Matrix4::transpose on the 16 words of a row-major matrix (what the guest's write does when
// mTransposeMatrices is set), as a word permutation.
constexpr std::array<uint32_t, 16> TransposeWords(const std::array<uint32_t, 16>& m) {
  std::array<uint32_t, 16> t{};
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) t[c * 4 + r] = m[r * 4 + c];
  return t;
}

// How many floats a matrix write stores: elementCount, at most 16 (0x821C2868).
constexpr uint32_t MatrixWriteCount(uint32_t element_count) { return element_count > 16 ? 16 : element_count; }
// How many floats a colour or vector write stores: elementCount, at most 4 (0x82467DE0).
constexpr uint32_t ColourWriteCount(uint32_t element_count) { return element_count > 4 ? 4 : element_count; }

}  // namespace torchlight::hooks
