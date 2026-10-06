// Wires the capture session into the app: --capture_dir / --capture_frames, the F9 bind and the
// reference image taken from the presenter.

#pragma once

namespace rex::ui {
class Presenter;
}

namespace torchlight::capture {

// Call once from the app after the runtime is set up. `presenter` may be null (no reference
// image then).
void Install(rex::ui::Presenter* presenter);

}  // namespace torchlight::capture
