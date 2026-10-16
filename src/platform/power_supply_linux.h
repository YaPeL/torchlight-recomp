// Whether a Linux machine runs on battery, from the kernel's power supplies
// (/sys/class/power_supply). Not SDL's SDL_GetPowerInfo: the SDK builds SDL without its power
// subsystem (SDL_POWER OFF), so that call answers "unknown" on every platform.
//
// On battery: a system battery (type Battery, scope not Device, present) is discharging and no
// external supply (type Mains or USB, scope not Device) is online. A battery that stops charging
// at a threshold while plugged in ("Not charging", a ThinkPad's charge limit) is not on battery;
// neither is a machine without a battery, or one whose files cannot be read.

#pragma once

#include <filesystem>

namespace torchlight::platform {

bool OnBatteryFromPowerSupplies(const std::filesystem::path& power_supply_dir);

}  // namespace torchlight::platform
