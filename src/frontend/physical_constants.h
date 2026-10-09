// A guest program stage's float constants by physical index (the index into the guest's
// GpuProgramParameters float array), as last written by SetConstants.
//
// Dense: they are written for every SetConstants, hundreds per frame, and read for every draw. An
// index past kDense (the guest's arrays are far smaller) is kept apart rather than growing the
// array.

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace torchlight::frontend {

class PhysicalConstants {
 public:
  static constexpr uint32_t kDense = 1 << 16;
  void Set(uint32_t index, float value) { SetRange(index, &value, 1); }
  // `count` floats from `first`, given as their bits (the command's raw 32-bit values).
  void SetRange(uint32_t first, const void* bits, uint32_t count);
  const float* Get(uint32_t index) const {  // null if never written
    if (index < values_.size()) return written_[index] ? &values_[index] : nullptr;
    return index >= kDense ? GetFar(index) : nullptr;
  }

 private:
  const float* GetFar(uint32_t index) const;

  std::vector<float> values_;
  std::vector<uint8_t> written_;
  std::unordered_map<uint32_t, float> far_;
};

}  // namespace torchlight::frontend
