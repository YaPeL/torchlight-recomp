// The touch controls drawn (docs/android-port.md, A4.2): over the game and its UI, on ImGui's
// foreground draw list, from the touch input's state (live/touch_input.h). A dialog of the runtime's
// that never takes the input (like the frame counter), drawn only while the controls are shown and
// no other dialog is open: a dialog is touched directly, and SDL's mouse made from the touch would
// also reach it.
//
// Until the owner approves the icons (Kenney's Onscreen Controls), the controls are plain shapes:
// a dark round button with a light ring and its pad name, the face buttons in their 360 colours;
// the stick a ring with its knob.

#pragma once

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
class ImGuiDrawer;
}  // namespace ui
}  // namespace rex

namespace torchlight::live {

// UI thread, each dialog frame (live/install.cpp): the overlay's dialog, made on the first call once
// the drawer exists; null without touch controls installed. Left out of "a dialog is open", which
// would cut the guest's input.
rex::ui::ImGuiDialog* TouchOverlayDialog(rex::ui::ImGuiDrawer* drawer);
// Whether another dialog of the runtime's is open (live/install.cpp, each UI frame): the overlay
// hides while one is.
void SetTouchOverlayDialogOpen(bool open);

}  // namespace torchlight::live
