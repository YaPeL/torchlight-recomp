#include "capture/install.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>

#include "capture/session.h"

REXCVAR_DEFINE_STRING(capture_dir, "", "Torchlight",
                      "Directory for F9 frame captures (empty disables capturing)");
REXCVAR_DEFINE_UINT32(capture_frames, 1, "Torchlight", "Frames (swap to swap) per F9 capture");

namespace torchlight::capture {

void Install(rex::ui::Presenter* presenter) {
  Session& session = Session::Get();
  session.Configure(REXCVAR_GET(capture_dir), REXCVAR_GET(capture_frames));
  if (presenter != nullptr) {
    session.SetReferenceImageProvider([presenter](commands::ReferenceImage& out) {
      rex::ui::RawImage image;
      if (!presenter->CaptureGuestOutput(image)) return false;
      out.width = image.width;
      out.height = image.height;
      out.stride = uint32_t(image.stride);
      out.rgbx = std::move(image.data);
      return true;
    });
  }
  rex::ui::RegisterBind("bind_capture_frame", "F9", "Capture the next frame to --capture_dir",
                        [] { Session::Get().RequestCapture(); });
  REXLOG_INFO("capture: F9 armed, dir='{}', frames={}", std::string(REXCVAR_GET(capture_dir)),
              uint32_t(REXCVAR_GET(capture_frames)));
}

}  // namespace torchlight::capture
