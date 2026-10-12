#include "frontend/physical_constants.h"

#include <algorithm>
#include <cstring>

namespace torchlight::frontend {

void PhysicalConstants::SetRange(uint32_t first, const void* bits, uint32_t count) {
  const auto* in = static_cast<const uint8_t*>(bits);
  const uint64_t end = uint64_t(first) + count;
  const uint32_t dense_end = uint32_t(std::min<uint64_t>(end, kDense));
  if (first < dense_end) {
    if (dense_end > values_.size()) {
      const size_t size = std::min<size_t>(kDense, std::max<size_t>(64, size_t(dense_end) * 2));
      values_.resize(size);
      written_.resize(size);
    }
    std::memcpy(&values_[first], in, size_t(dense_end - first) * sizeof(float));
    std::memset(&written_[first], 1, dense_end - first);
  }
  const uint64_t far_end = std::min<uint64_t>(end, uint64_t(1) << 32);
  for (uint64_t i = std::max<uint64_t>(first, kDense); i < far_end; ++i) {
    float v;
    std::memcpy(&v, in + (i - first) * sizeof(float), sizeof(float));
    far_[uint32_t(i)] = v;
  }
  // The index is 32-bit: a range past the top wraps to 0, as the per-float writes it replaces did.
  if (end > far_end) {
    SetRange(0, in + (far_end - first) * sizeof(float), uint32_t(end - far_end));
  }
}

const float* PhysicalConstants::GetFar(uint32_t index) const {
  auto it = far_.find(index);
  return it == far_.end() ? nullptr : &it->second;
}

}  // namespace torchlight::frontend
