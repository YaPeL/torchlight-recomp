// Slow frame report of the live mode: when a frame takes longer than kSlowFrameMs on the guest
// (swap to swap), on the backend (consuming it, present included) or between two presents, the log
// gets one line with what happened in that frame and in the previous one, to tell the causes of
// stutters apart (programs generated, textures or buffers created, dropped frames, recording,
// present). Diagnostics only: nothing here changes behaviour.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace torchlight::live {

inline constexpr double kSlowFrameMs = 25;  // above one 60 Hz vblank missed (33.3 ms)
inline constexpr size_t kSlowFrameReportsPerPeriod = 20;

// One consumed frame, guest and backend sides.
struct FrameRecord {
  uint64_t swap = 0;
  // Guest (LiveFrame::Producer).
  double guest_ms = 0;
  uint32_t dropped = 0;  // frames dropped into this one
  double worst_dropped_guest_ms = 0;
  double recording_ms = -1;  // -1: not timed on this frame
  uint32_t commands = 0;
  uint32_t snapshot_copies = 0;
  uint64_t snapshot_bytes = 0;
  uint32_t resources_announced = 0, resources_destroyed = 0;
  // Backend thread.
  double backend_ms = 0;  // consuming the frame, present included
  double present_interval_ms = 0;  // since the previous present; 0 for the first
  double content_ms = 0, commands_ms = 0, draws_ms = 0, end_ms = 0, present_ms = 0, release_ms = 0;
  uint32_t textures_created = 0;
  double texture_ms = 0;
  uint32_t programs_generated = 0;
  double program_ms = 0, program_generate_ms = 0;  // the draws that generated programs; RTSS
  uint32_t gpu_buffers_created = 0, vertex_layouts_created = 0;
};

class SlowFrameDetector {
 public:
  // The report line when `frame` is slow (nothing otherwise, or once the period's line budget is
  // spent). `frame` becomes the previous frame for the next call.
  std::optional<std::string> Add(const FrameRecord& frame);
  // The period's counts ("slow frames: ..."), then a new period starts. Empty when there were none.
  std::string TakePeriodSummary();

  static std::string Describe(const FrameRecord& frame);

 private:
  std::optional<FrameRecord> previous_;
  size_t slow_ = 0, guest_ = 0, backend_ = 0, interval_only_ = 0, unreported_ = 0;
};

}  // namespace torchlight::live
