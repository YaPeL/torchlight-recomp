// Tests for the frame reclaimer: every frame handed over is freed (its content references
// dropped), by the reclaimer's thread or, with the backlog full, by the caller; none is left at
// destruction.

#include <cstdio>
#include <cstdlib>
#include <memory>

#include "live/frame_reclaimer.h"

namespace {

using namespace torchlight::commands;
using torchlight::live::FrameReclaimer;
using torchlight::live::LiveFrame;
using torchlight::live::SnapshotStore;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

}  // namespace

int main() {
  auto blob = std::make_shared<const Blob>();
  std::weak_ptr<const Blob> watch = blob;
  constexpr size_t kFrames = 1000;
  size_t behind = 0, inline_freed = 0;
  {
    FrameReclaimer reclaimer;
    for (size_t i = 0; i < kFrames; ++i) {
      LiveFrame f;
      f.swap = i;
      f.commands.resize(100);
      f.contents.push_back({ResourceId{}, blob});
      reclaimer.Free(std::move(f));
      Check(f.contents.empty(), "the frame handed over is left empty");
    }
    blob.reset();
    // Destruction below frees whatever is still pending.
    behind = reclaimer.freed_behind();
    inline_freed = reclaimer.freed_inline();
    Check(behind + inline_freed <= kFrames, "no frame freed twice");
  }
  Check(watch.expired(), "every frame's content reference is dropped by destruction");
  std::printf("frame_reclaimer_test: ok (%zu freed behind before the end, %zu inline)\n", behind,
              inline_freed);
  return 0;
}
