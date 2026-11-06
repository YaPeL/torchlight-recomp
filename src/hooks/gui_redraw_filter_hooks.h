// The GUI redraw filter (gui_redraw_filter_hooks.cpp, --native_gui_redraw_filter).

#pragma once

namespace torchlight::hooks {

// Logs which of the GUI's redraws run (--native_gui_redraw_filter, and the shadow mode in
// diagnostics builds), once at startup.
void LogGuiRedrawFilterMode();

}  // namespace torchlight::hooks
