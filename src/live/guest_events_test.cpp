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
  Check(line.find("the rest 42.5 ms | looked up 1: 'GAME:\\B.MESH' (found)") != std::string::npos,
        "line rest and the resource looked up");
  // A resource's name: from MEDIA\ on, upper case, '/' as '\', no compiled suffix.
  using torchlight::live::ResourceName;
  Check(ResourceName("game:\\music\\MEDIA\\Models\\a.MESH.ADM") == "MEDIA\\MODELS\\A.MESH" &&
            ResourceName("tlhost:/media/models/a.mesh") == "MEDIA\\MODELS\\A.MESH" &&
            ResourceName("GAME:\\MEDIA\\UI\\X.UILAYOUT.CMP") == "MEDIA\\UI\\X.UILAYOUT" &&
            ResourceName("game:\\b.mesh") == "GAME:\\B.MESH" && ResourceName(".ADM") == ".ADM",
        "resource names");
  // The resources looked up: each once whatever its location and form, found if any check found
  // it, directories, wildcard searches and empty paths left out, the first kMaxLookedUpPaths
  // listed, the rest counted.
  g.FileCheck("game:\\media\\", true, 0.1);
  g.FileCheck("game:/media/", true, 0.1);
  g.FileCheck("game:\\media\\*.dds", true, 0.1);
  g.FileCheck("", false, 0.1);
  g.FileCheck("game:\\music\\MEDIA\\T0.DDS", false, 0.1);
  g.FileCheck("tlhost:\\MEDIA\\T0.DDS", false, 0.1);
  g.FileCheck("GAME:\\MEDIA\\T0.DDS", false, 0.1);
  g.FileCheck("GAME:\\MEDIA\\M0.MESH.ADM", false, 0.1);
  g.FileCheck("tlmod0:\\MEDIA\\M0.MESH", true, 0.1);
  for (int i = 1; i < 18; ++i)
    g.FileCheck("GAME:\\MEDIA\\T" + std::to_string(i) + ".DDS", false, 0.1);
  GuestFrameEvents many = g.Take();
  using torchlight::live::kMaxLookedUpPaths;
  Check(many.looked_up.size() == kMaxLookedUpPaths && many.looked_up[0].path == "MEDIA\\T0.DDS" &&
            !many.looked_up[0].found && many.looked_up[1].path == "MEDIA\\M0.MESH" &&
            many.looked_up[1].found && many.looked_up.back().path == "MEDIA\\T14.DDS",
        "looked up: each resource once, found kept, no directories, wildcards or empty paths");
  Check(many.looked_up_unlisted == 3, "resources past the list counted");
  const std::string many_line = DescribeLongFrame(35.0, many);
  Check(many_line.find("| looked up 19: 'MEDIA\\T0.DDS' 'MEDIA\\M0.MESH' (found) "
                       "'MEDIA\\T1.DDS'") != std::string::npos &&
            many_line.find("'MEDIA\\T14.DDS' (+3 more)") != std::string::npos,
        "line lists the resources looked up and the rest");
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
