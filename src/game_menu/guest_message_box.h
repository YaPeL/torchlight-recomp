// A message box over the game, through the runtime's XamShowMessageBoxUI export, as the game shows
// its own: drawn by the runtime's dialogs, answered with the pad. It never blocks the game's
// thread: its texts, buttons, result and XOVERLAPPED are in one block of the system heap, alive
// until the box closes, and Poll reads the XOVERLAPPED. Used by the PC saves' import
// (save_import_menu.cpp) and the mods' import (video_menu.cpp).
//
// Game's thread only (the call needs the guest's context).

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <rex/ppc/context.h>

namespace rex {
class Runtime;
}

namespace torchlight::game_menu {

class GuestMessageBox {
 public:
  explicit GuestMessageBox(rex::Runtime* runtime = nullptr) : runtime_(runtime) {}
  GuestMessageBox(const GuestMessageBox&) = delete;
  GuestMessageBox& operator=(const GuestMessageBox&) = delete;

  void set_runtime(rex::Runtime* runtime) { runtime_ = runtime; }
  // Opens the box; false if it could not be shown, or while one of this object is still open
  // (Poll has not returned its answer). `active`: the button selected when it opens.
  bool Show(PPCContext& ctx, uint8_t* base, const std::string& title, const std::string& text,
            const std::vector<std::string>& buttons, int active);
  // The chosen button once the box closed (-1 if it closed without one, or none was open);
  // nothing while it is open.
  std::optional<int> Poll(uint8_t* base);
  bool open() const { return block_ != 0; }

 private:
  void Free();

  rex::Runtime* runtime_;
  uint32_t block_ = 0, result_ = 0, overlapped_ = 0;
};

}  // namespace torchlight::game_menu
