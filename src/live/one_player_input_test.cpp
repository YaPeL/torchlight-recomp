// The one-player input policy (one_player_input.h): two real pads and a synthetic controller (the
// touch controls, a script's) all feed guest user 0; users 1-3 see no controller, so the game never
// asks a second user to sign in (issue #138). For comparison, the SDK's SlotAssignment (the policy
// its CreateDefaultInputSystem installs) gives the second pad to user 1.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <rex/input/device_assignment.h>

#include "live/one_player_input.h"

namespace {

using rex::input::DeviceId;
using rex::input::DeviceInfo;

void Check(bool ok, const std::string& what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    std::exit(1);
  }
}

std::vector<DeviceInfo> TwoPadsAndATouchController() {
  std::vector<DeviceInfo> devices(3);
  devices[0].id = DeviceId(11);
  devices[0].ordinal = 0;
  devices[1].id = DeviceId(12);
  devices[1].ordinal = 1;
  devices[2].id = DeviceId(13);
  devices[2].ordinal = 2;
  devices[2].synthetic = true;
  return devices;
}

bool Has(const std::vector<DeviceId>& ids, uint64_t id) {
  return std::find(ids.begin(), ids.end(), DeviceId(id)) != ids.end();
}

}  // namespace

int main() {
  auto one = torchlight::live::OnePlayerAssignment();
  one->OnDevicesChanged(TwoPadsAndATouchController());
  std::vector<DeviceId> ids;
  one->DevicesForUser(0, ids);
  Check(ids.size() == 3 && Has(ids, 11) && Has(ids, 12) && Has(ids, 13), "both pads and the synthetic one on user 0");
  for (uint32_t user = 1; user < rex::input::kMaxGuestUsers; ++user) {
    one->DevicesForUser(user, ids);
    Check(ids.empty(), "user " + std::to_string(user) + ": no controller");
  }

  // SlotAssignment, the SDK's default policy and the cause: the second pad is user 1's.
  rex::input::SlotAssignment slots;
  slots.OnDevicesChanged(TwoPadsAndATouchController());
  slots.DevicesForUser(1, ids);
  Check(ids.size() == 1 && Has(ids, 12), "the default: the second pad on user 1");

  std::puts("one_player_input_test: ok");
  return 0;
}
