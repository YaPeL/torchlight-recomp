#include "dev/script_input.h"

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(dev_input_script, "", "Torchlight",
                      "Development builds only: a file of controller commands played into the game "
                      "(docs/dev-input-script.md)");

#ifndef TORCHLIGHT_DEV_INPUT

namespace torchlight::dev {
void InstallInputScript(rex::Runtime*, std::function<void()>) {
  if (!REXCVAR_GET(dev_input_script).empty()) {
    REXLOG_WARN("dev: --dev_input_script ignored: this build has no input scripts "
                "(CMake option TORCHLIGHT_DEV_INPUT)");
  }
}
}  // namespace torchlight::dev

#else

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>

#include "capture/session.h"
#include "dev/deterministic_time.h"
#include "dev/guest_command.h"
#include "dev/input_script.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/ogre_layout.h"
#include "live/frame_timing.h"

namespace torchlight::dev {

namespace {

namespace abi = torchlight::guest_abi;
using Clock = std::chrono::steady_clock;
using rex::X_RESULT;
using rex::X_STATUS;

// The class name of a guest object from MSVC's RTTI (vtable[-1] -> complete object locator ->
// type descriptor, name at +8, ".?AVName@@"): what a menu wait matches. Empty when not readable.
std::string ClassName(const uint8_t* base, uint32_t object) {
  const auto in_image = [](uint32_t a) { return a >= 0x82000000u && a < 0x84000000u; };
  if (object == 0) return {};
  const uint32_t vtable = abi::ReadU32(base, object);
  if (!in_image(vtable)) return {};
  const uint32_t locator = abi::ReadU32(base, vtable - 4);
  if (!in_image(locator)) return {};
  const uint32_t type = abi::ReadU32(base, locator + 12);
  if (!in_image(type)) return {};
  std::string name;
  for (uint32_t i = 0; i < 128; ++i) {
    const char c = char(abi::ReadU8(base, type + 8 + i));
    if (c == 0) break;
    name.push_back(c);
  }
  return name.rfind(".?A", 0) == 0 ? name : std::string();
}

// One run of measured frames between two marks, logged as the step overlay logs its steps (the
// measurement scripts read these lines).
struct Segment {
  std::string name;
  std::vector<double> frames, presented;
  size_t dropped = 0;

  void Log() const {
    if (name.empty() || frames.empty()) return;
    auto stats = [](std::vector<double> v, double& sum, double& p99) {
      std::sort(v.begin(), v.end());
      sum = 0;
      for (double x : v) sum += x;
      p99 = v[std::min(v.size() - 1, size_t(0.99 * v.size()))];
      return v;
    };
    double sum = 0, p99 = 0;
    const auto f = stats(frames, sum, p99);
    const auto over = [&](double ms) { return f.end() - std::upper_bound(f.begin(), f.end(), ms); };
    REXLOG_INFO("perf step '{}': {} frames, {:.1f} fps, mean {:.2f} ms, p99 {:.2f}, max {:.2f}, "
                "over 33 ms {}, over 50 ms {}",
                name, f.size(), 1000.0 * f.size() / sum, sum / f.size(), p99, f.back(), over(33.0),
                over(50.0));
    if (presented.empty()) return;
    const auto p = stats(presented, sum, p99);
    REXLOG_INFO("perf step '{}' presented: {} frames, {:.1f} fps, mean {:.2f} ms, p99 {:.2f}, max "
                "{:.2f}, {} guest frames dropped",
                name, p.size(), 1000.0 * p.size() / sum, sum / p.size(), p99, p.back(), dropped);
  }
};

// The script's controller: one synthetic device, which the SDK feeds to guest user 0 together with
// any real controller (rex::input::SlotAssignment / SharedAssignment).
class ScriptInputDriver : public rex::input::InputDriver {
 public:
  static constexpr rex::input::DeviceId kDevice = static_cast<rex::input::DeviceId>(0x53435249);

  ScriptInputDriver() : InputDriver(nullptr, 0) {}
  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {
    if (!connected_) return;
    rex::input::DeviceInfo info;
    info.id = kDevice;
    info.name = "input script";
    info.guid = "torchlight-input-script";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out) override {
    if (id != kDevice || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    if (out) {
      std::memset(out, 0, sizeof(*out));
      if (is_active()) {
        out->packet_number = packet_;
        out->gamepad.buttons = pad_.buttons;
        out->gamepad.left_trigger = pad_.left_trigger;
        out->gamepad.right_trigger = pad_.right_trigger;
        out->gamepad.thumb_lx = pad_.left_x;
        out->gamepad.thumb_ly = pad_.left_y;
        out->gamepad.thumb_rx = pad_.right_x;
        out->gamepad.thumb_ry = pad_.right_y;
      }
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t,
                                 rex::input::X_INPUT_CAPABILITIES* out) override {
    if (id != kDevice || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
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
    return id == kDevice && connected_ ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t,
                              rex::input::X_INPUT_KEYSTROKE* out) override {
    if (id != kDevice || !connected_) return X_ERROR_DEVICE_NOT_CONNECTED;
    std::lock_guard lock(mutex_);
    if (keys_.empty() || !is_active()) return X_ERROR_EMPTY;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->virtual_key = keys_.front().virtual_key;
      out->flags = keys_.front().down ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN
                                      : rex::input::X_INPUT_KEYSTROKE_KEYUP;
    }
    keys_.pop_front();
    return X_ERROR_SUCCESS;
  }

  // The guest's render thread, once a frame.
  void Set(const PadState& pad) {
    std::lock_guard lock(mutex_);
    if (pad == pad_) return;
    for (const auto& k : PadKeystrokes(pad_, pad)) {
      if (keys_.size() < 64) keys_.push_back(k);
    }
    pad_ = pad;
    ++packet_;
  }
  // The script ended: the device goes away and a real controller is alone again.
  void Disconnect() { connected_ = false; }

 private:
  std::mutex mutex_;
  PadState pad_;
  uint32_t packet_ = 1;
  std::deque<PadKeystroke> keys_;
  std::atomic<bool> connected_{true};
};

// Steps the script once per guest frame and runs its actions.
class ScriptRunner : public live::FrameObserver {
 public:
  ScriptRunner(InputScript script, ScriptInputDriver* driver, std::function<void()> close)
      : script_(std::move(script)), driver_(driver), close_(std::move(close)),
        start_(Clock::now()) {}

  void OnGuestFrame(double ms) override {
    std::lock_guard lock(mutex_);
    if (ms > 0) segment_.frames.push_back(ms);
    events_.frame++;
    // With --dev_deterministic_time the script's seconds are the game's virtual ones.
    AdvanceDeterministicTime();
    events_.seconds = DeterministicTimeOn() ? DeterministicSeconds()
                                            : std::chrono::duration<double>(Clock::now() - start_).count();
    if (finished_) return;
    std::vector<ScriptAction> actions;
    const PadState pad = script_.Step(events_, actions);
    driver_->Set(pad);
    for (const auto& a : actions) Run(a);
  }
  void OnPresentedFrame(double ms, size_t dropped) override {
    std::lock_guard lock(mutex_);
    segment_.presented.push_back(ms);
    segment_.dropped += dropped;
  }
  void OnLevelLoaded() override {
    std::lock_guard lock(mutex_);
    events_.level_loads++;
  }
  void OnMenuOpened(std::string name) {
    std::lock_guard lock(mutex_);
    REXLOG_INFO("dev input script: menu opened: {}", name);
    events_.menus_opened.push_back(std::move(name));
  }

 private:
  // With the mutex held.
  void Run(const ScriptAction& a) {
    switch (a.kind) {
      case ScriptAction::Kind::kMark:
        segment_.Log();
        segment_ = Segment{a.text};
        REXLOG_INFO("perf step {}: {}", step_++, a.text);
        break;
      case ScriptAction::Kind::kCapture:
        REXLOG_INFO("dev input script: capture");
        capture::Session::Get().RequestCapture();
        break;
      case ScriptAction::Kind::kCommand:
        if (QueueGuestCommand(a.text)) {
          REXLOG_INFO("dev input script: guest command \"{}\" queued", a.text);
        } else {
          REXLOG_ERROR("dev input script FAILED: \"command {}\" needs TORCHLIGHT_DEV_COMMANDS too; "
                       "closing the game", a.text);
          finished_ = true;
          driver_->Disconnect();
          if (close_) close_();
        }
        break;
      case ScriptAction::Kind::kQuit:
        segment_.Log();
        segment_ = {};
        REXLOG_INFO("dev input script: quit");
        if (close_) close_();
        break;
      case ScriptAction::Kind::kFailed:
        segment_.Log();
        segment_ = {};
        REXLOG_ERROR("dev input script FAILED: {}; closing the game", a.text);
        finished_ = true;
        driver_->Disconnect();
        if (close_) close_();
        break;
      case ScriptAction::Kind::kFinished:
        segment_.Log();
        segment_ = {};
        REXLOG_INFO("dev input script: finished");
        finished_ = true;
        driver_->Disconnect();
        break;
    }
  }

  std::mutex mutex_;
  InputScript script_;
  ScriptInputDriver* driver_;
  std::function<void()> close_;
  Clock::time_point start_;
  ScriptEvents events_;
  Segment segment_;
  int step_ = 0;
  bool finished_ = false;
};

std::unique_ptr<ScriptRunner> g_runner;

}  // namespace

void InstallInputScript(rex::Runtime* runtime, std::function<void()> request_close) {
  const std::string path = REXCVAR_GET(dev_input_script);
  if (path.empty()) return;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    REXLOG_ERROR("dev input script: cannot read {}", path);
    return;
  }
  std::stringstream text;
  text << file.rdbuf();
  std::string error;
  auto script = InputScript::Parse(text.str(), error);
  if (!script) {
    REXLOG_ERROR("dev input script {}: {}", path, error);
    return;
  }
  auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
  if (!input) {
    REXLOG_ERROR("dev input script: no input system");
    return;
  }
  const size_t commands = script->size();
  auto driver = std::make_unique<ScriptInputDriver>();
  ScriptInputDriver* raw = driver.get();
  input->AddDriver(std::move(driver));
  InstallDeterministicTime();  // the runner steps its clock
  g_runner = std::make_unique<ScriptRunner>(std::move(*script), raw, std::move(request_close));
  live::SetFrameObserver(g_runner.get());
  REXLOG_INFO("dev input script: {} ({} commands), played as a controller of its own", path,
              commands);
}

}  // namespace torchlight::dev

extern "C" {

// The base CDropdownMenu::SetOpen (guest_abi game_ui dropdown_menu::kSetOpen, 0x82351FF0), which
// every menu's own SetOpen ends in: a menu that opens is an event the script can wait for, named
// by its class. Development builds only.
REX_EXTERN(__imp__sub_82351FF0);
REX_FUNC(sub_82351FF0) {
  namespace abi = torchlight::guest_abi;
  const uint32_t menu = ctx.r3.u32;
  const bool opening = ctx.r4.u32 != 0 &&
                       abi::ReadU8(base, menu + abi::game_ui::dropdown_menu::kOpen.offset) == 0;
  std::string name =
      opening && torchlight::dev::g_runner ? torchlight::dev::ClassName(base, menu) : std::string();
  __imp__sub_82351FF0(ctx, base);
  if (opening && torchlight::dev::g_runner) torchlight::dev::g_runner->OnMenuOpened(std::move(name));
}

}  // extern "C"

#endif  // TORCHLIGHT_DEV_INPUT
