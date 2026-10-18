#include "input/pad_state.h"

#include <utility>

namespace torchlight::input {

std::vector<PadKeystroke> PadKeystrokes(const PadState& before, const PadState& after) {
  static constexpr std::pair<uint16_t, uint16_t> kKeys[] = {
      {pad::kA, 0x5800},         {pad::kB, 0x5801},           {pad::kX, 0x5802},
      {pad::kY, 0x5803},         {pad::kRightShoulder, 0x5804}, {pad::kLeftShoulder, 0x5805},
      {pad::kUp, 0x5810},        {pad::kDown, 0x5811},        {pad::kLeft, 0x5812},
      {pad::kRight, 0x5813},     {pad::kStart, 0x5814},       {pad::kBack, 0x5815},
      {pad::kLeftThumb, 0x5816}, {pad::kRightThumb, 0x5817},
  };
  std::vector<PadKeystroke> keys;
  for (const auto& [bit, vk] : kKeys) {
    const bool was = before.buttons & bit, is = after.buttons & bit;
    if (was != is) keys.push_back({vk, is});
  }
  const auto trigger = [&](uint8_t was, uint8_t is, uint16_t vk) {
    if ((was > 0) != (is > 0)) keys.push_back({vk, is > 0});
  };
  trigger(before.left_trigger, after.left_trigger, 0x5806);
  trigger(before.right_trigger, after.right_trigger, 0x5807);
  return keys;
}

void KeystrokeQueue::Push(const PadState& before, const PadState& after, Clock::time_point now) {
  for (const auto& k : PadKeystrokes(before, after)) {
    if (keys_.size() < kMaxQueued) keys_.push_back({k, now});
  }
}

std::optional<PadKeystroke> KeystrokeQueue::Pop(Clock::time_point now) {
  while (!keys_.empty() && now - keys_.front().when > kLifetime) keys_.pop_front();
  if (keys_.empty()) return std::nullopt;
  const PadKeystroke key = keys_.front().key;
  keys_.pop_front();
  return key;
}

}  // namespace torchlight::input
