#include "live/touch_input.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_touch.h>
#include <SDL3/SDL_video.h>
#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/runtime.h>

#include "input/touch_visibility.h"
#include "platform/platform.h"

#if TORCHLIGHT_DEV_INPUT
REXCVAR_DEFINE_BOOL(dev_touch_mouse, false, "Torchlight",
                    "Development: the mouse's left button is a finger on the touch controls, which "
                    "count as a touch screen (docs/android-port.md, A4)");
#endif

namespace torchlight::live {
namespace {

using rex::X_RESULT;
using rex::X_STATUS;

// A real pad's axis past this counts as the pad being used (Auto hides the controls).
constexpr int kPadUsedAxis = 8000;
// The mouse as a finger (development) and the input scripts' fingers: ids no screen gives.
constexpr uint64_t kMouseFinger = 0xF1D6E700'00000001ull;
constexpr uint64_t kInjectedFingers = 0xF1D6E700'00010000ull;

input::TouchMode ModeOf(settings::TouchControls mode) {
  switch (mode) {
    case settings::TouchControls::kAlways:
      return input::TouchMode::kAlways;
    case settings::TouchControls::kNever:
      return input::TouchMode::kNever;
    default:
      return input::TouchMode::kAuto;
  }
}

class TouchInputDriver : public rex::input::InputDriver {
 public:
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x544F5543);

  TouchInputDriver(input::TouchMode mode, bool pad_at_start, bool touch_screen, bool mouse_finger,
                   float width, float height, float points_w, float points_h, float px_per_mm)
      : InputDriver(nullptr, 0),
        visibility_(mode, pad_at_start, touch_screen),
        mouse_finger_(mouse_finger),
        width_(width),
        height_(height),
        points_w_(points_w),
        points_h_(points_h),
        px_per_mm_(px_per_mm) {
    pad_.SetLayout(input::GameplayLayout({width_, height_, px_per_mm_}));
    SDL_AddEventWatch(Watch, this);
  }
  ~TouchInputDriver() override { SDL_RemoveEventWatch(Watch, this); }

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "touch controls";
    info.guid = "torchlight-touch-controls";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->packet_number = packet_;
      if (is_active()) {
        out->gamepad.buttons = state_.buttons;
        out->gamepad.left_trigger = state_.left_trigger;
        out->gamepad.right_trigger = state_.right_trigger;
        out->gamepad.thumb_lx = state_.left_x;
        out->gamepad.thumb_ly = state_.left_y;
        out->gamepad.thumb_rx = state_.right_x;
        out->gamepad.thumb_ry = state_.right_y;
      }
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
      out->gamepad.buttons = 0xFFFF;
      out->gamepad.left_trigger = 0xFF;
      out->gamepad.right_trigger = 0xFF;
      out->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
      out->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
      out->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
      out->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
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
    const auto key = keys_.Pop(input::KeystrokeQueue::Clock::now());
    if (!key || !is_active()) return X_ERROR_EMPTY;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->virtual_key = key->virtual_key;
      out->flags = key->down ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN : rex::input::X_INPUT_KEYSTROKE_KEYUP;
    }
    return X_ERROR_SUCCESS;
  }

  // A finger in window fractions (screens and scripts).
  void Finger(uint64_t id, float fx, float fy, bool down, bool up) {
    std::lock_guard lock(mutex_);
    const float x = fx * width_, y = fy * height_;
    if (down) {
      const bool was_visible = visibility_.visible();
      if (!visibility_.OnFingerDown()) {
        // The touch that showed them only shows them: its finger acts on nothing until it lifts.
        ignored_.insert(id);
        if (!was_visible && visibility_.visible()) REXLOG_INFO("touch: controls shown (a touch)");
        return;
      }
      pad_.Down(id, x, y);
    } else if (up) {
      if (ignored_.erase(id)) return;
      pad_.Up(id);
    } else {
      if (ignored_.contains(id)) return;
      pad_.Move(id, x, y);
    }
    Update();
  }

  void PadUsed() {
    std::lock_guard lock(mutex_);
    if (!visibility_.OnPadUsed()) return;
    REXLOG_INFO("touch: controls hidden (a pad was used)");
    pad_.Clear();
    ignored_.clear();
    Update();
  }

  void Resized(float width, float height) {
    std::lock_guard lock(mutex_);
    if (width <= 0 || height <= 0 || (width == width_ && height == height_)) return;
    width_ = width;
    height_ = height;
    pad_.SetLayout(input::GameplayLayout({width_, height_, px_per_mm_}));
    Update();
  }

  void ResizedPoints(float width, float height) {
    std::lock_guard lock(mutex_);
    if (width > 0 && height > 0) {
      points_w_ = width;
      points_h_ = height;
    }
  }

  bool Snapshot(TouchSnapshot& out) {
    std::lock_guard lock(mutex_);
    out.visible = visibility_.visible();
    out.layout = pad_.layout();
    out.state = state_;
    out.stick_base_x = pad_.stick_base_x();
    out.stick_base_y = pad_.stick_base_y();
    out.stick_held = pad_.stick_held();
    out.width = width_;
    out.height = height_;
    return true;
  }

 private:
  // Under the lock: the new state, its keystrokes and a new packet.
  void Update() {
    const input::PadState next = visibility_.visible() ? pad_.State() : input::PadState{};
    if (next == state_) return;
    keys_.Push(state_, next, input::KeystrokeQueue::Clock::now());
    state_ = next;
    ++packet_;
  }

  static bool Watch(void* self, SDL_Event* event);

  std::mutex mutex_;
  input::TouchPad pad_;
  input::TouchVisibility visibility_;
  input::PadState state_;
  input::KeystrokeQueue keys_;
  std::set<uint64_t> ignored_;
  uint32_t packet_ = 1;
  bool mouse_finger_;
  bool mouse_down_ = false;  // the mouse finger (UI thread only)
  float width_, height_, points_w_, points_h_, px_per_mm_;
};

// SDL's event watch: on Android, finger events come on the Java side's thread.
bool TouchInputDriver::Watch(void* self, SDL_Event* event) {
  auto* driver = static_cast<TouchInputDriver*>(self);
  switch (event->type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED:
      driver->Finger(static_cast<uint64_t>(event->tfinger.fingerID), event->tfinger.x, event->tfinger.y,
                     event->type == SDL_EVENT_FINGER_DOWN,
                     event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED);
      break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
      driver->PadUsed();
      break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
      if (std::abs(static_cast<int>(event->gaxis.value)) > kPadUsedAxis) driver->PadUsed();
      break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
      driver->Resized(static_cast<float>(event->window.data1), static_cast<float>(event->window.data2));
      break;
    case SDL_EVENT_WINDOW_RESIZED:
      driver->ResizedPoints(static_cast<float>(event->window.data1), static_cast<float>(event->window.data2));
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
      // The mouse as a finger (development); never the mouse SDL makes from touches.
      if (driver->mouse_finger_ && event->button.which != SDL_TOUCH_MOUSEID &&
          event->button.button == SDL_BUTTON_LEFT) {
        const bool down = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        driver->mouse_down_ = down;
        driver->Finger(kMouseFinger, event->button.x / driver->points_w_, event->button.y / driver->points_h_,
                       down, !down);
      }
      break;
    case SDL_EVENT_MOUSE_MOTION:
      if (driver->mouse_finger_ && driver->mouse_down_ && event->motion.which != SDL_TOUCH_MOUSEID) {
        driver->Finger(kMouseFinger, event->motion.x / driver->points_w_, event->motion.y / driver->points_h_,
                       false, false);
      }
      break;
    default:
      break;
  }
  return true;
}

std::atomic<TouchInputDriver*> g_driver{nullptr};

}  // namespace

void InstallTouchControls(rex::Runtime* runtime, settings::TouchControls mode) {
  if (mode == settings::TouchControls::kNever) {
    REXLOG_INFO("touch: controls off (touch_controls = \"never\")");
    return;
  }
  auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  if (!input) return;
  bool mouse_finger = false;
#if TORCHLIGHT_DEV_INPUT
  mouse_finger = REXCVAR_GET(dev_touch_mouse);
#endif
  int touch_devices = 0;
  if (SDL_TouchID* devices = SDL_GetTouchDevices(&touch_devices)) SDL_free(devices);
  const bool touch_screen = touch_devices > 0 || mouse_finger;
  int gamepads = 0;
  if (SDL_JoystickID* pads = SDL_GetGamepads(&gamepads)) SDL_free(pads);
  // The game window's size (its pixels for the layout, its points for the mouse) and density.
  int count = 0, w = 0, h = 0, pw = 0, ph = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  if (windows && count == 1) {
    SDL_GetWindowSizeInPixels(windows[0], &w, &h);
    SDL_GetWindowSize(windows[0], &pw, &ph);
  }
  if (windows) SDL_free(windows);
  const float px_per_mm = platform::GameWindowPixelsPerMm();
  if (w <= 0 || h <= 0 || px_per_mm <= 0) {
    REXLOG_WARN("touch: no game window to lay the controls out on; none installed");
    return;
  }
  // The real pad's events reach the watch only with SDL's gamepad support on.
  SDL_InitSubSystem(SDL_INIT_GAMEPAD);
  auto driver = std::make_unique<TouchInputDriver>(ModeOf(mode), gamepads > 0, touch_screen, mouse_finger,
                                                   float(w), float(h), float(pw), float(ph), px_per_mm);
  g_driver = driver.get();
  input->AddDriver(std::move(driver));
  TouchSnapshot snap;
  ReadTouchControls(snap);
  REXLOG_INFO("touch: controls {} (mode {}, {} touch screen(s){}, {} pad(s) at start, {:.1f} px/mm, {}x{})",
              snap.visible ? "shown" : "hidden", settings::TouchControlsName(mode), touch_devices,
              mouse_finger ? ", the mouse as a finger" : "", gamepads, px_per_mm, w, h);
}

bool TouchControlCentre(std::string_view name, float& x, float& y) {
  TouchSnapshot snap;
  if (!ReadTouchControls(snap)) return false;
  const auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
  for (const auto& b : snap.layout.buttons) {
    if (b.name.size() != name.size()) continue;
    if (!std::equal(b.name.begin(), b.name.end(), name.begin(),
                    [&](char p, char q) { return lower(p) == lower(q); }))
      continue;
    if (snap.width <= 0 || snap.height <= 0) return false;
    x = b.x / snap.width;
    y = b.y / snap.height;
    return true;
  }
  return false;
}

bool ReadTouchControls(TouchSnapshot& out) {
  TouchInputDriver* driver = g_driver.load();
  return driver && driver->Snapshot(out);
}

void InjectTouch(uint64_t finger, float x, float y, bool up) {
  TouchInputDriver* driver = g_driver.load();
  if (!driver) return;
  // A finger the driver has not seen is a new one.
  static std::mutex mutex;
  static std::set<uint64_t> down;
  std::lock_guard lock(mutex);
  const uint64_t id = kInjectedFingers + finger;
  const bool is_down = down.contains(id);
  if (up) {
    if (!is_down) return;
    down.erase(id);
    driver->Finger(id, x, y, false, true);
  } else {
    if (!is_down) down.insert(id);
    driver->Finger(id, x, y, !is_down, false);
  }
}

}  // namespace torchlight::live
