#include "capture/constant_mirror.h"

#include <algorithm>

namespace torchlight::capture {

bool ConstantMirror::Holds(bool floats, uint8_t stage, uint32_t physical, const uint32_t* values,
                           uint32_t count) const {
  if (stage >= kStages) return false;
  const Array& a = stages_[stage].arrays[floats];
  if (uint64_t(physical) + count > a.values.size()) return false;
  for (uint32_t i = 0; i < count; ++i) {
    if (!a.known[physical + i] || a.values[physical + i] != values[i]) return false;
  }
  return true;
}

void ConstantMirror::Store(bool floats, uint8_t stage, uint32_t physical, const uint32_t* values,
                           uint32_t count) {
  if (stage >= kStages) return;
  Array& a = stages_[stage].arrays[floats];
  if (uint64_t(physical) + count > a.values.size()) {
    a.values.resize(physical + count);
    a.known.resize(physical + count);
  }
  std::copy(values, values + count, a.values.begin() + physical);
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
