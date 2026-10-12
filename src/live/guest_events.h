// What the guest did between two swaps that can make a frame long, for the long frame report
// (live/frame_timing.h, every mode): its file existence checks (OGRE looks a resource up in each
// resource location), file reads and XMemAlloc calls, counted and timed by the overrides in
// hooks/guest_io_hooks.cpp on every guest thread. Cheap: a few counters per call, one log line
// only for a frame past kLongFrameMs.

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace torchlight::live {

inline constexpr double kLongFrameMs = 33.3;  // past two 60 Hz frames

struct GuestFrameEvents {
  uint32_t file_checks = 0, files_missing = 0;
  double file_check_ms = 0;
  std::string slowest_check_path;
  double slowest_check_ms = 0;
  uint32_t reads = 0;
  uint64_t read_bytes = 0;
  double read_ms = 0;
  uint32_t allocations = 0;
  uint64_t allocated_bytes = 0;
  double allocation_ms = 0;
  uint64_t largest_allocation = 0;
  double largest_allocation_ms = 0;
};

class GuestEvents {
 public:
  static GuestEvents& Get();
  void FileCheck(std::string_view path, bool found, double ms);
  void Read(uint64_t bytes, double ms);
  void Allocation(uint64_t bytes, double ms);
  // The events since the previous call (the guest's swap), then zero.
  GuestFrameEvents Take();

 private:
  std::mutex mutex_;
  GuestFrameEvents frame_;
};

// "N ms | file checks ..., reads ..., allocations ...": the long frame's line.
std::string DescribeLongFrame(double frame_ms, const GuestFrameEvents& events);

}  // namespace torchlight::live
