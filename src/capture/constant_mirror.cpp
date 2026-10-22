#include "capture/constant_mirror.h"

#include <algorithm>
#include <cstring>

namespace torchlight::capture {

bool ConstantMirror::Holds(bool floats, uint8_t stage, uint32_t physical, const uint8_t* words,
                           uint32_t count) const {
  if (stage >= kStages) return false;
  const Array& a = stages_[stage].arrays[floats];
  if (uint64_t(physical) + count > a.known.size()) return false;
  if (std::find(a.known.begin() + physical, a.known.begin() + physical + count, uint8_t(0)) !=
      a.known.begin() + physical + count) {
    return false;
  }
  return std::memcmp(a.bytes.data() + 4 * size_t(physical), words, 4 * size_t(count)) == 0;
}

void ConstantMirror::Store(bool floats, uint8_t stage, uint32_t physical, const uint8_t* words,
                           uint32_t count) {
  if (stage >= kStages) return;
  Array& a = stages_[stage].arrays[floats];
  if (uint64_t(physical) + count > a.known.size()) {
    a.bytes.resize(4 * (size_t(physical) + count));
    a.known.resize(size_t(physical) + count);
  }
  std::memcpy(a.bytes.data() + 4 * size_t(physical), words, 4 * size_t(count));
  std::fill(a.known.begin() + physical, a.known.begin() + physical + count, uint8_t(1));
}

bool ConstantMirror::SameTail(uint8_t stage, const std::vector<commands::AutoConstant>& autos,
                              const std::optional<bool>& transpose) const {
  if (stage >= kStages) return false;
  const Stage& s = stages_[stage];
  return s.tail_known && s.autos == autos && s.transpose == transpose;
}

void ConstantMirror::StoreTail(uint8_t stage, const std::vector<commands::AutoConstant>& autos,
                               const std::optional<bool>& transpose) {
  if (stage >= kStages) return;
  Stage& s = stages_[stage];
  s.tail_known = true;
  s.autos = autos;
  s.transpose = transpose;
}

void ConstantMirror::Reset() { stages_ = {}; }

}  // namespace torchlight::capture
