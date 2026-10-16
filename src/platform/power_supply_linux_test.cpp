// OnBatteryFromPowerSupplies (power_supply_linux.h) on made-up /sys/class/power_supply trees,
// among them the ThinkPad this was checked on: AC, BAT0 and two USB-C ports (ucsi) of type USB.

#include "platform/power_supply_linux.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;
using torchlight::platform::OnBatteryFromPowerSupplies;

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// A supply's folder with its attribute files (each value written with the kernel's newline).
void Supply(const fs::path& dir, const char* name,
            std::initializer_list<std::pair<const char*, const char*>> attributes) {
  fs::create_directories(dir / name);
  for (const auto& [key, value] : attributes) std::ofstream(dir / name / key) << value << "\n";
}

fs::path Fresh(const fs::path& root, const char* name) {
  const fs::path dir = root / name;
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  return dir;
}

}  // namespace

int main() {
  const fs::path root = fs::temp_directory_path() / "power_supply_linux_test";

  // The ThinkPad, unplugged: AC offline, BAT0 discharging, the USB-C ports offline.
  fs::path d = Fresh(root, "unplugged");
  Supply(d, "AC", {{"type", "Mains"}, {"online", "0"}});
  Supply(d, "BAT0", {{"type", "Battery"}, {"status", "Discharging"}, {"present", "1"}});
  Supply(d, "ucsi-source-psy-USBC000:001", {{"type", "USB"}, {"status", "Not charging"}, {"online", "0"}, {"scope", "System"}});
  Supply(d, "ucsi-source-psy-USBC000:002", {{"type", "USB"}, {"status", "Not charging"}, {"online", "0"}, {"scope", "System"}});
  Check(OnBatteryFromPowerSupplies(d), "unplugged: on battery");

  // Plugged in at the charge limit: AC online, BAT0 "Not charging".
  d = Fresh(root, "plugged_at_limit");
  Supply(d, "AC", {{"type", "Mains"}, {"online", "1"}});
  Supply(d, "BAT0", {{"type", "Battery"}, {"status", "Not charging"}, {"present", "1"}});
  Check(!OnBatteryFromPowerSupplies(d), "plugged in at the charge limit: not on battery");

  // Plugged in and charging.
  d = Fresh(root, "charging");
  Supply(d, "AC", {{"type", "Mains"}, {"online", "1"}});
  Supply(d, "BAT0", {{"type", "Battery"}, {"status", "Charging"}, {"present", "1"}});
  Check(!OnBatteryFromPowerSupplies(d), "charging: not on battery");

  // A USB-C charger online while the battery still reports discharging (a weak charger).
  d = Fresh(root, "usb_charger");
  Supply(d, "BAT0", {{"type", "Battery"}, {"status", "Discharging"}, {"present", "1"}});
  Supply(d, "ucsi-source-psy-USBC000:001", {{"type", "USB"}, {"online", "1"}, {"scope", "System"}});
  Check(!OnBatteryFromPowerSupplies(d), "a USB-C supply online: not on battery");

  // A desktop: no battery at all.
  d = Fresh(root, "desktop");
  Check(!OnBatteryFromPowerSupplies(d), "no supplies: not on battery");

  // A desktop with a wireless pad's battery discharging (scope Device): not the machine's.
  d = Fresh(root, "pad_battery");
  Supply(d, "ps-controller-battery", {{"type", "Battery"}, {"status", "Discharging"}, {"scope", "Device"}});
  Check(!OnBatteryFromPowerSupplies(d), "a device's own battery: not on battery");

  // A battery bay without its battery.
  d = Fresh(root, "absent");
  Supply(d, "AC", {{"type", "Mains"}, {"online", "0"}});
  Supply(d, "BAT1", {{"type", "Battery"}, {"status", "Discharging"}, {"present", "0"}});
  Check(!OnBatteryFromPowerSupplies(d), "an absent battery: not on battery");

  // No folder (no sysfs): not on battery.
  Check(!OnBatteryFromPowerSupplies(root / "missing"), "no folder: not on battery");

  std::error_code ec;
  fs::remove_all(root, ec);
  if (failures) return EXIT_FAILURE;
  std::printf("power_supply_linux_test: ok\n");
  return EXIT_SUCCESS;
}
