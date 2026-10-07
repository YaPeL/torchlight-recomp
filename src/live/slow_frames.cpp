#include "live/slow_frames.h"

#include <algorithm>
#include <format>

namespace torchlight::live {

std::string SlowFrameDetector::Describe(const FrameRecord& f) {
  std::string recording =
      f.recording_ms < 0 ? std::string("untimed") : std::format("{:.2f}", f.recording_ms);
  return std::format(
      "swap={} guest={:.2f} dropped={} worst_dropped={:.2f} recording={} commands={} "
      "snapshots={}/{}KB announced={} destroyed={} backend={:.2f} interval={:.2f} "
      "content={:.2f} commands_ms={:.2f} draws={:.2f} end={:.2f} present={:.2f} release={:.2f} "
      "textures={}/{:.2f}ms programs={}/{:.2f}ms (rtss {:.2f}ms) buffers={} layouts={}",
      f.swap, f.guest_ms, f.dropped, f.worst_dropped_guest_ms, recording, f.commands,
      f.snapshot_copies, f.snapshot_bytes / 1024, f.resources_announced, f.resources_destroyed,
      f.backend_ms, f.present_interval_ms, f.content_ms, f.commands_ms, f.draws_ms, f.end_ms,
      f.present_ms, f.release_ms, f.textures_created, f.texture_ms, f.programs_generated,
      f.program_ms, f.program_generate_ms, f.gpu_buffers_created, f.vertex_layouts_created);
}

std::optional<std::string> SlowFrameDetector::Add(const FrameRecord& frame) {
  const bool guest =
      std::max(frame.guest_ms, frame.worst_dropped_guest_ms) > kSlowFrameMs;
  const bool backend = frame.backend_ms > kSlowFrameMs;
  const bool interval = frame.present_interval_ms > kSlowFrameMs;
  std::optional<std::string> line;
  if (guest || backend || interval) {
    ++slow_;
    if (guest) ++guest_;
    if (backend) ++backend_;
    if (!guest && !backend) ++interval_only_;
    if (slow_ - unreported_ <= kSlowFrameReportsPerPeriod) {
      const char* by = guest && backend ? "both" : guest ? "guest" : backend ? "backend" : "interval";
      line = std::format("slow frame by={} | {} | previous: {}", by, Describe(frame),
                         previous_ ? Describe(*previous_) : std::string("none"));
    } else {
      ++unreported_;
    }
  }
  previous_ = frame;
  return line;
}

std::string SlowFrameDetector::TakePeriodSummary() {
  std::string s;
  if (slow_) {
    s = std::format(
        "slow frames (over {:.0f} ms): {} (guest {}, backend {}, present interval only {}), {} not "
        "reported",
        kSlowFrameMs, slow_, guest_, backend_, interval_only_, unreported_);
  }
  slow_ = guest_ = backend_ = interval_only_ = unreported_ = 0;
  return s;
}

}  // namespace torchlight::live
