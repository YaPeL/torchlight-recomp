#include "live/guest_events.h"

#include <format>
#include <utility>

namespace torchlight::live {

GuestEvents& GuestEvents::Get() {
  static GuestEvents events;
  return events;
}

void GuestEvents::FileCheck(std::string_view path, bool found, double ms) {
  std::lock_guard lock(mutex_);
  ++frame_.file_checks;
  if (!found) ++frame_.files_missing;
  frame_.file_check_ms += ms;
  if (ms > frame_.slowest_check_ms) {
    frame_.slowest_check_ms = ms;
    frame_.slowest_check_path.assign(path);
  }
}

void GuestEvents::Read(uint64_t bytes, double ms) {
  std::lock_guard lock(mutex_);
  ++frame_.reads;
  frame_.read_bytes += bytes;
  frame_.read_ms += ms;
}

void GuestEvents::Allocation(uint64_t bytes, double ms) {
  std::lock_guard lock(mutex_);
  ++frame_.allocations;
  frame_.allocated_bytes += bytes;
  frame_.allocation_ms += ms;
  if (bytes > frame_.largest_allocation) {
    frame_.largest_allocation = bytes;
    frame_.largest_allocation_ms = ms;
  }
}

GuestFrameEvents GuestEvents::Take() {
  std::lock_guard lock(mutex_);
  return std::exchange(frame_, {});
}

namespace {

std::string Size(uint64_t bytes) {
  if (bytes >= (uint64_t{1} << 20)) return std::format("{:.1f} MiB", bytes / 1048576.0);
  if (bytes >= 1024) return std::format("{:.1f} KiB", bytes / 1024.0);
  return std::format("{} B", bytes);
}

}  // namespace

std::string DescribeLongFrame(double frame_ms, const GuestFrameEvents& e) {
  std::string s = std::format("{:.1f} ms | guest: file checks {} ({} missing, {:.2f} ms", frame_ms,
                              e.file_checks, e.files_missing, e.file_check_ms);
  if (e.file_checks)
    s += std::format("; slowest {:.2f} ms '{}'", e.slowest_check_ms, e.slowest_check_path);
  s += std::format("), reads {} ({}, {:.2f} ms), allocations {} ({}, {:.2f} ms", e.reads,
                   Size(e.read_bytes), e.read_ms, e.allocations, Size(e.allocated_bytes),
                   e.allocation_ms);
  if (e.allocations)
    s += std::format("; largest {} in {:.2f} ms", Size(e.largest_allocation),
                     e.largest_allocation_ms);
  s += ")";
  const double accounted = e.file_check_ms + e.read_ms + e.allocation_ms;
  s += std::format(", the rest {:.1f} ms", frame_ms > accounted ? frame_ms - accounted : 0.0);
  return s;
}

}  // namespace torchlight::live
