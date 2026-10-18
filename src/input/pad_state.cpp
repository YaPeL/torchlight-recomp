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

uint64_t PadKeyBits(const PadState& pad) {
  uint64_t f = pad.buttons;
  f |= uint64_t(pad.left_trigger > kTriggerKeyThreshold) << 16;
  f |= uint64_t(pad.right_trigger > kTriggerKeyThreshold) << 17;
  const auto stick = [&](int16_t x, int16_t y, int base) {
    uint64_t u = y > kThumbKeyThreshold, d = y < ~kThumbKeyThreshold;
    uint64_t r = x > kThumbKeyThreshold, l = x < ~kThumbKeyThreshold;
    if (u && l) { u = l = 0; f |= uint64_t(1) << (base + 4); }
    if (u && r) { u = r = 0; f |= uint64_t(1) << (base + 5); }
    if (d && r) { d = r = 0; f |= uint64_t(1) << (base + 6); }
    if (d && l) { d = l = 0; f |= uint64_t(1) << (base + 7); }
    f |= u << base;
    f |= d << (base + 1);
    f |= r << (base + 2);
    f |= l << (base + 3);
  };
  stick(pad.left_x, pad.left_y, 18);
  stick(pad.right_x, pad.right_y, 26);
  return f;
}

uint16_t PadKeyVirtualKey(int bit) {
  // XInput's VK_PAD_* in the order of the bits above (bits 10 and 11, the guide and an unknown
  // one, have none).
  static constexpr uint16_t kKeys[kPadKeyCount] = {
      0x5810, 0x5811, 0x5812, 0x5813, 0x5814, 0x5815, 0x5816, 0x5817,  // d-pad, Start, Back, LS, RS
      0x5805, 0x5804, 0,      0,      0x5800, 0x5801, 0x5802, 0x5803,  // LB, RB, -, -, A, B, X, Y
      0x5806, 0x5807,                                                  // LT, RT
      0x5820, 0x5821, 0x5822, 0x5823, 0x5824, 0x5825, 0x5826, 0x5827,  // left stick
      0x5830, 0x5831, 0x5832, 0x5833, 0x5834, 0x5835, 0x5836, 0x5837,  // right stick
  };
  return bit >= 0 && bit < kPadKeyCount ? kKeys[bit] : 0;
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
