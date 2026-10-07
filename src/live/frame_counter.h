// The frame counter (F3, bind_frame_counter): the game's frame rate and frame times in the top right
// corner, with a graph of the last frames, to see drops and stutters while playing. Every mode: the
// native backend draws it with the runtime's dialogs, Xenos with the SDK's overlays (it takes F3
// from the SDK's debug overlay, which has no frame times of its own here). It never takes input.
// The numbers are the guest's swap to swap (live/frame_timing.h), as the log's.

#pragma once

#include <cstddef>
#include <vector>

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
}  // namespace ui
}  // namespace rex

namespace torchlight::live {

// What the counter shows, from the frame times (ms) of the last seconds, oldest first: the frame
// rate over the last second, the last frame, and over all of them the 1 % low (the frame rate of
// the 99th percentile frame time), the longest frame and the frames past 33 and 50 ms.
struct FrameCounterStats {
  double fps = 0, last_ms = 0, low_1pct_fps = 0, max_ms = 0;
  size_t over_33 = 0, over_50 = 0, frames = 0;
};
FrameCounterStats SummarizeFrames(const std::vector<float>& frame_ms);

// UI thread, once the runtime exists: F3 (bind_frame_counter) shows and hides the counter.
void InstallFrameCounter(rex::Runtime* runtime);
// The counter's dialog while it is shown, else null (only mode draws it but keeps it out of "a
// dialog is open", which would cut the guest's input).
rex::ui::ImGuiDialog* FrameCounterDialog();

}  // namespace torchlight::live
