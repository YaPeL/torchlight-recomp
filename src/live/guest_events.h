// What the guest did between two swaps that can make a frame long, for the long frame report
// (live/frame_timing.h, every mode): its file existence checks (OGRE looks a resource up in each
// resource location), file reads and XMemAlloc calls, counted and timed by the overrides in
// hooks/guest_io_hooks.cpp on every guest thread, and the paths those checks asked for. The game
// looks every resource up as a loose file (the replacements and mods folders, the game's folder)
// before it reads it from its pak, so the paths asked for in a frame are the resources it loaded
// then, found loose or not; a resource already loaded is not looked up again. Cheap: a few counters
// per call, one log line only for a frame past the long frame threshold (--long_frame_ms,
// frame_timing.cpp).

#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::live {

// The paths looked up that a long frame line lists; the rest are only counted.
inline constexpr size_t kMaxLookedUpPaths = 16;

struct GuestFrameEvents {
  uint32_t file_checks = 0, files_missing = 0;
  double file_check_ms = 0;
  std::string slowest_check_path;
  double slowest_check_ms = 0;
  // The paths the checks asked for, in order, each once, up to kMaxLookedUpPaths: files, found or
  // not (directories and wildcard searches left out); `found` marks the ones found loose.
  struct LookedUp {
    std::string path;
    bool found = false;
  };
  std::vector<LookedUp> looked_up;
  uint32_t looked_up_unlisted = 0;
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
