// The touch controls drawn (docs/android-port.md, A4.2): over the game and its UI, on ImGui's
// foreground draw list, from the touch input's state (live/touch_input.h). A dialog of the runtime's
// that never takes the input (like the frame counter), in the drawer only while the controls are
// shown and no other dialog is open: a dialog is touched directly, and SDL's mouse made from the
// touch would also reach it. While it is in the drawer, the drawer listens to the window (as with
// any dialog): the game's own input comes through the input drivers, not the window's listeners.
//
// The look: Kenney's "Mobile Controls" style A (CC0; the dark round button, its pressed highlight,
// the stick's pad and nub), from data/ui/touch/ next to the executable, with the pad's name on each
// button (A, B, X, Y in the 360's colours). Without those files: plain shapes in their place.

#pragma once

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace ui
}  // namespace rex

namespace torchlight::live {

// UI thread, each dialog frame (live/install.cpp): the overlay's dialog, made on the first call once
// the drawer exists; null without touch controls installed. Left out of "a dialog is open", which
// would cut the guest's input.
rex::ui::ImGuiDialog* TouchOverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* textures);
// Whether the touch controls are shown now (installed and visible): the overlay is in the drawer
// only then, and only while no other dialog is open (live/install.cpp).
bool TouchControlsShown();
// Before the drawer goes away: the overlay's dialog deleted (a new drawer makes it again).
void ForgetTouchOverlayDialog();

}  // namespace torchlight::live
