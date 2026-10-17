#include "platform/power_supply_linux.h"

#include <fstream>
#include <string>
#include <system_error>

namespace torchlight::platform {

namespace {

// The first line of a supply's attribute file; empty when it has none.
std::string Read(const std::filesystem::path& supply, const char* name) {
  std::ifstream in(supply / name);
  std::string value;
  std::getline(in, value);
  return value;
}

}  // namespace

bool OnBatteryFromPowerSupplies(const std::filesystem::path& power_supply_dir) {
  bool discharging = false;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(power_supply_dir, ec)) {
    const std::filesystem::path& supply = entry.path();
    if (Read(supply, "scope") == "Device") continue;  // a pad's or a mouse's own battery
    const std::string type = Read(supply, "type");
    if (type == "Mains" || type == "USB") {
      if (Read(supply, "online") == "1") return false;
    } else if (type == "Battery") {
      if (Read(supply, "present") == "0") continue;
      if (Read(supply, "status") == "Discharging") discharging = true;
    }
  }
  return discharging;
}

}  // namespace torchlight::platform
