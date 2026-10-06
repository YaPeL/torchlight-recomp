// The gamepad for the host UI: ImGui keys from a pad state and the guest input block.

#include <cstdio>
#include <cstdlib>

#include "live/ui_gamepad.h"

namespace {

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
  }
}

const torchlight::live::UiGamepadKey* Find(const std::vector<torchlight::live::UiGamepadKey>& keys,
                                           ImGuiKey key) {
  for (const auto& k : keys) {
    if (k.key == key) return &k;
  }
  return nullptr;
}

}  // namespace

int main() {
  using namespace torchlight::live;
  torchlight::platform::GamepadState pad;
  pad.connected = true;
  pad.a = true;
  pad.down = true;
  pad.left_x = -0.8f;
  pad.left_y = 0.1f;  // inside the dead zone
  auto keys = GamepadToImGui(pad);
  Check(keys.size() == 10, "every key, every frame");
  Check(Find(keys, ImGuiKey_GamepadFaceUp)->down, "A is the input activation (enters text fields)");
  Check(!Find(keys, ImGuiKey_GamepadFaceDown)->down, "plain activation only from Y");
  Check(!Find(keys, ImGuiKey_GamepadFaceRight)->down, "B up");
  Check(Find(keys, ImGuiKey_GamepadDpadDown)->down, "d-pad down");
  Check(Find(keys, ImGuiKey_GamepadDpadLeft)->down, "stick left moves like the d-pad");
  Check(!Find(keys, ImGuiKey_GamepadDpadRight)->down, "stick right up");
  Check(!Find(keys, ImGuiKey_GamepadDpadUp)->down, "small stick values do not move");
  Check(!Find(keys, ImGuiKey_GamepadLStickLeft), "the stick does not scroll");

  torchlight::platform::GamepadState idle;
  Check(!GamepadHeld(idle), "idle pad");
  Check(GamepadHeld(pad), "held pad");
  idle.left_y = 0.2f;
  Check(!GamepadHeld(idle), "stick drift is not held");

  Check(BlockGuestInput(true, false, idle), "blocked while dialogs are open");
  Check(BlockGuestInput(false, true, pad), "still blocked after closing while A is held");
  Check(!BlockGuestInput(false, true, idle), "released after closing");
  Check(!BlockGuestInput(false, false, pad), "a press without dialogs reaches the game");
  std::printf("ui gamepad test: ok\n");
  return 0;
}
