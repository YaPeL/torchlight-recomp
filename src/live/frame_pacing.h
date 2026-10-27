// Frame pacing: how many display refreshes each frame the screen showed stayed there, from the
// window system's presentation times (platform/presentation_time.h). At 60 frames a second on a
// 144 Hz display a frame stays 2 or 3 refreshes (2.4 on average), however evenly the game makes the
// frames: the eye reads the mix as judder. The measure is the share of frames held the most common
// number of refreshes ("even"), with the histogram, in the 10 s summaries and the perf steps.
// Pure, tested in frame_pacing_test.

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace torchlight::live {

class PacingCounter {
 public:
  // Refresh counts 1 to kBuckets - 1 have their own bucket; more go to the last one.
  static constexpr size_t kBuckets = 8;

  struct Summary {
    size_t frames = 0;                    // frames counted (a known refresh, a previous frame)
    std::array<size_t, kBuckets> held{};  // held[n]: frames shown n refreshes (0 unused)
    uint32_t mode = 0;                    // the most common number of refreshes (0: no frames)
    double even_percent = 0;              // the share of frames held `mode` refreshes
  };

  // A frame shown `gap` after the screen's previous one, the display refreshing every `refresh`.
  // A gap rounds to the nearest whole number of refreshes, at least one; frames with an unknown
  // refresh (zero) are not counted.
  void AddShown(std::chrono::nanoseconds gap, std::chrono::nanoseconds refresh);
  // What was counted since the last Take, and starts again.
  Summary Take();
  // "2x97.3% 3x2.7% (even 97.3% of 1234)", or "no presentation times" with no frames.
  static std::string Describe(const Summary& s);

 private:
  Summary current_;
};

}  // namespace torchlight::live
