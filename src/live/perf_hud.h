// TEMPORARY, for the native/Xenos measurement only (branch perf/native-vs-xenos; not to be
// integrated): an on-screen frame rate counter in the top right corner, and below it the step of
// the measurement the player has to do, with a beep at each change. Each timed step's frame times
// go to the log as one line (docs/performance-profile.md).

#pragma once

#include <chrono>

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
class WindowedAppContext;
}  // namespace ui
}  // namespace rex

namespace torchlight::live::perf {

// Install (UI thread): where to draw; the counter appears on the first guest frame.
void Install(rex::ui::WindowedAppContext* app_context, rex::Runtime* runtime);
// Guest render thread, at every device swap: one frame of `frame_ms`.
void OnSwap(double frame_ms);
// Game thread, after each of the game's level loads.
void OnLevelLoaded();
// The counter's dialog once it exists, else null (only mode keeps it out of "a dialog is open").
rex::ui::ImGuiDialog* Hud();

}  // namespace torchlight::live::perf
