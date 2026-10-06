// Guest frame time (swap to swap), always measured and summarised in the log every 10 s, with
// or without the live mode, so the two can be compared.

#pragma once

#include <chrono>
#include <vector>

namespace torchlight::live {

class FrameTiming {
 public:
  static FrameTiming& Get();
  // Guest render thread, at every device swap.
  void OnSwap();

 private:
  FrameTiming() = default;
  std::chrono::steady_clock::time_point last_swap_{}, last_summary_{};
  std::vector<double> frame_ms_;
};

}  // namespace torchlight::live
