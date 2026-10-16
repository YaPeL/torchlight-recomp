// A notice over the game while its thread is held by long work of ours (the mods' unit index built
// at the first start with a large pack, mods/unit_index_install.cpp), so the still screen does not
// look like a hang: one line in the bottom centre ("Preparing mods... 42%"), drawn with the
// runtime's dialogs in every mode. It never takes input and is left out of "a dialog is open"
// (install.cpp). In the native mode no guest frame comes meanwhile, so the consumer presents the
// last frame again with the notice over it while it is shown (live_mode.cpp).
//
// Threads: Show, Progress and Hide from any thread (the game's, usually); the dialog is created,
// drawn and destroyed on the UI thread.

#pragma once

#include <cstddef>
#include <string>

namespace rex {
class Runtime;
namespace ui {
class ImGuiDialog;
class WindowedAppContext;
}  // namespace ui
}  // namespace rex

namespace torchlight::live {

// Once, before the guest runs (live::Install).
void InstallBusyNotice(rex::Runtime* runtime, rex::ui::WindowedAppContext* app_context);

// Shows `text` (translated; "{percent}" is the progress, busy_notice_text.h), progress 0.
void ShowBusyNotice(std::string text);
void SetBusyNoticeProgress(size_t done, size_t total);
void HideBusyNotice();

// Whether the notice is meant to be on screen (any thread).
bool BusyNoticeShown();
// UI thread: the notice's dialog while it exists, else null.
rex::ui::ImGuiDialog* BusyNoticeDialog();

}  // namespace torchlight::live
