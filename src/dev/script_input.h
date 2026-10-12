// Development only: --dev_input_script=FILE plays an input script (input_script.h) into the game
// as a controller of its own, so runs need no one at the controller (docs/dev-input-script.md).
// Compiled in only with the CMake option TORCHLIGHT_DEV_INPUT (off by default and never set by the
// release workflows); other builds ignore the flag and log that.

#pragma once

#include <functional>

namespace rex {
class Runtime;
}

namespace torchlight::dev {

// Before the guest runs, and before anything sets the input system's active callback (the script's
// controller gets it too). `request_close` closes the game window as the player would (the
// script's quit), from any thread.
void InstallInputScript(rex::Runtime* runtime, std::function<void()> request_close);

}  // namespace torchlight::dev
