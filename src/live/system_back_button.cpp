#include "live/system_back_button.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include "platform/platform.h"

namespace torchlight::live {
namespace {

using rex::X_RESULT;
using rex::X_STATUS;

constexpr uint16_t kVkPadBack = 0x5815;  // the keystroke the game reads for Back (VK_PAD_BACK)

class SystemBackDriver : public rex::input::InputDriver {
 public:
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x42414B31);

  SystemBackDriver() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "system back button";
    info.guid = "torchlight-system-back";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->packet_number = packet_.load();
      if (pressed_.load()) out->gamepad.buttons = rex::input::X_INPUT_GAMEPAD_BACK;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->type = rex::input::XINPUT_DEVTYPE_GAMEPAD;
      out->sub_type = rex::input::XINPUT_DEVSUBTYPE_GAMEPAD;
      out->gamepad.buttons = rex::input::X_INPUT_GAMEPAD_BACK;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(rex::input::DeviceId id, rex::input::X_INPUT_VIBRATION*) override {
    return id == kDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    if (pending_ == Pending::kNone) return X_ERROR_EMPTY;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->virtual_key = kVkPadBack;
      out->flags = pending_ == Pending::kDown ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN
                                              : rex::input::X_INPUT_KEYSTROKE_KEYUP;
    }
    pending_ = pending_ == Pending::kDownUp ? Pending::kUp : Pending::kNone;
    return X_ERROR_SUCCESS;
  }

  // SDL's thread that pumps events (an event watch).
  void Set(bool down) {
    if (pressed_.exchange(down) == down) return;
    ++packet_;
    std::lock_guard lock(mutex_);
    // A press and its release both reach a game that reads keystrokes late.
    if (down) {
      pending_ = Pending::kDown;
    } else {
      pending_ = pending_ == Pending::kDown ? Pending::kDownUp : Pending::kUp;
    }
  }

 private:
  // The keystrokes not read yet: a down, a release, or both in order.
  enum class Pending { kNone, kDown, kUp, kDownUp };
  std::atomic<bool> pressed_{false};
  std::atomic<uint32_t> packet_{1};
  std::mutex mutex_;
  Pending pending_ = Pending::kNone;
};

bool WatchBackKey(void* driver, SDL_Event* event) {
  if ((event->type == SDL_EVENT_KEY_DOWN || event->type == SDL_EVENT_KEY_UP) &&
      event->key.scancode == SDL_SCANCODE_AC_BACK) {
    static_cast<SystemBackDriver*>(driver)->Set(event->type == SDL_EVENT_KEY_DOWN);
  }
  return true;
}

}  // namespace

void InstallSystemBackButton(rex::Runtime* runtime) {
  if (!platform::HasSystemBackButton()) return;
  auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  if (!input) return;
  // Android's Back reaches the app as a key instead of finishing the activity.
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
  auto driver = std::make_unique<SystemBackDriver>();
  SDL_AddEventWatch(WatchBackKey, driver.get());  // the driver lives as long as the input system
  input->AddDriver(std::move(driver));
  REXLOG_INFO("input: the system's Back button is the pad's Back");
}

}  // namespace torchlight::live
