#include "live/one_player_input.h"

#include <rex/input/device_assignment.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>

namespace torchlight::live {

std::unique_ptr<rex::input::DeviceAssignment> OnePlayerAssignment() {
  return std::make_unique<rex::input::SharedAssignment>();
}

void InstallOnePlayerInput(rex::Runtime* runtime) {
  auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  if (!input) return;
  input->SetDeviceAssignment(OnePlayerAssignment());
  REXLOG_INFO("input: every controller plays guest user 0 (single player)");
}

}  // namespace torchlight::live
