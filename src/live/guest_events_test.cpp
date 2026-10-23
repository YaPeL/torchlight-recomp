// The long frame's guest events (guest_events.h): accumulation, reset and the report line.

#include <cstdio>
#include <string>

#include "live/guest_events.h"

namespace {

using torchlight::live::DescribeLongFrame;
using torchlight::live::GuestEvents;
using torchlight::live::GuestFrameEvents;

int failures = 0;

void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

}  // namespace

int main() {
  GuestEvents& g = GuestEvents::Get();
  g.FileCheck("game:\\a\\", false, 0.5);
  g.FileCheck("game:\\b.mesh", true, 2.0);
  g.FileCheck("game:\\c\\", false, 1.0);
  g.Read(4096, 0.25);
  g.Read(1024, 0.25);
  g.Allocation(1 << 20, 3.0);
  g.Allocation(256, 0.5);
  GuestFrameEvents e = g.Take();
  Check(e.file_checks == 3 && e.files_missing == 2, "file checks counted");
  Check(e.file_check_ms == 3.5 && e.slowest_check_path == "game:\\b.mesh", "slowest check kept");
  Check(e.reads == 2 && e.read_bytes == 5120 && e.read_ms == 0.5, "reads counted");
  Check(e.allocations == 2 && e.allocated_bytes == (1 << 20) + 256, "allocations counted");
  Check(e.largest_allocation == (1 << 20) && e.largest_allocation_ms == 3.0, "largest kept");
  // Take resets: the next frame starts empty.
  GuestFrameEvents next = g.Take();
  Check(next.file_checks == 0 && next.reads == 0 && next.allocations == 0, "reset after take");
  const std::string line = DescribeLongFrame(50.0, e);
  Check(line.find("50.0 ms | guest: file checks 3 (2 missing, 3.50 ms; slowest 2.00 ms "
                  "'game:\\b.mesh')") == 0, "line start");
  Check(line.find("reads 2 (5.0 KiB, 0.50 ms)") != std::string::npos, "line reads");
  Check(line.find("allocations 2 (1.0 MiB, 3.50 ms; largest 1.0 MiB in 3.00 ms)") !=
            std::string::npos, "line allocations");
  Check(line.find("the rest 42.5 ms | looked up 1: 'game:\\b.mesh' (found)") != std::string::npos,
        "line rest and the path looked up");
  // The paths looked up: found or not, each once (found if any check found it), directories and
  // wildcard searches left out, the first kMaxLookedUpPaths listed, the rest counted.
  g.FileCheck("game:\\media\\", true, 0.1);
  g.FileCheck("game:/media/", true, 0.1);
  g.FileCheck("game:\\media\\*.dds", true, 0.1);
  g.FileCheck("", false, 0.1);
  g.FileCheck("tlmod0:\\f0.dds", false, 0.1);
  g.FileCheck("game:\\f0.dds", false, 0.1);
  g.FileCheck("game:\\f0.dds", true, 0.1);
  for (int i = 1; i < 18; ++i) g.FileCheck("game:\\f" + std::to_string(i) + ".dds", false, 0.1);
  GuestFrameEvents many = g.Take();
  using torchlight::live::kMaxLookedUpPaths;
  Check(many.looked_up.size() == kMaxLookedUpPaths && many.looked_up[0].path == "tlmod0:\\f0.dds" &&
            !many.looked_up[0].found && many.looked_up[1].path == "game:\\f0.dds" &&
            many.looked_up[1].found && many.looked_up.back().path == "game:\\f14.dds",
        "looked up: each path once, found kept, no directories, wildcards or empty paths");
  Check(many.looked_up_unlisted == 3, "paths past the list counted");
  const std::string many_line = DescribeLongFrame(35.0, many);
  Check(many_line.find("| looked up 19: 'tlmod0:\\f0.dds' 'game:\\f0.dds' (found) "
                       "'game:\\f1.dds'") != std::string::npos &&
            many_line.find("'game:\\f14.dds' (+3 more)") != std::string::npos,
        "line lists the paths looked up and the rest");
  Check(g.Take().looked_up.empty(), "reset after take: no paths");
  // A frame with no events says so, without the optional parts.
  Check(DescribeLongFrame(40.0, GuestFrameEvents{}) ==
            "40.0 ms | guest: file checks 0 (0 missing, 0.00 ms), reads 0 (0 B, 0.00 ms), "
            "allocations 0 (0 B, 0.00 ms), the rest 40.0 ms", "empty line");
  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  std::puts("guest_events_test: ok");
  return 0;
}
