#include "dev/script_input.h"

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(dev_input_script, "", "Torchlight",
                      "Development builds only: a file of controller commands played into the game "
                      "(docs/dev-input-script.md)");
REXCVAR_DEFINE_STRING(dev_input_record, "", "Torchlight",
                      "Development builds only: write the player's controller, from the first level "
                      "load on, to this file as an input script (docs/dev-input-script.md)");

#ifndef TORCHLIGHT_DEV_INPUT

namespace torchlight::dev {
void InstallInputScript(rex::Runtime*, std::function<void()>) {
  if (!REXCVAR_GET(dev_input_script).empty() || !REXCVAR_GET(dev_input_record).empty()) {
    REXLOG_WARN("dev: --dev_input_script / --dev_input_record ignored: this build has no input "
                "scripts (CMake option TORCHLIGHT_DEV_INPUT)");
  }
}
}  // namespace torchlight::dev

#else

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
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
    events_.seconds = std::chrono::duration<double>(Clock::now() - start_).count();
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

// --dev_input_record: the player's controller as an input script. The menus are written as the
// standard scripts do them (the first level load ends them); from then on, the controller as it
// was, a `state` (or `idle`) line per change, timed in milliseconds (characters move by time, not
// by frames); each later level load as a wait for that load count; and a guided run's steps
// (live::MarkStep, the step overlay) as marks, the last one ("Done...") as quit. Every line is
// flushed as it is written: the game ends without running destructors.
class InputRecorder : public live::FrameObserver {
 public:
  InputRecorder(rex::input::InputSystem* input, std::FILE* file) : input_(input), file_(file) {
    std::fputs(
        "# Recorded with --dev_input_record (docs/dev-input-script.md): the menus as the standard\n"
        "# scripts do them, then the player's controller from the first level load on.\n"
        "press A every 3s until menu MainMenu timeout 90s\n"
        "mark MAIN MENU: hands off\n"
        "idle 10s\n"
        "press A every 3s until level_loaded timeout 60s\n",
        file_);
    std::fflush(file_);
  }

  void OnGuestFrame(double) override {
    std::lock_guard lock(mutex_);
    if (!recording_) return;
    rex::input::X_INPUT_STATE state;
    PadState pad;
    if (input_->GetStateForUI(0, &state) == X_ERROR_SUCCESS) pad = Quantize(state);
    if (pad == current_) return;
    Flush(Clock::now());
    current_ = pad;
  }
  void OnPresentedFrame(double, size_t) override {}
  void OnLevelLoaded() override {
    std::lock_guard lock(mutex_);
    ++loads_;
    const auto now = Clock::now();
    if (!recording_) {
      recording_ = true;  // the prefix's "until level_loaded" covers this first load
      since_ = now;
      REXLOG_INFO("dev input record: recording the controller from the first level load on");
      return;
    }
    Flush(now);
    std::fprintf(file_, "wait level_loads %u timeout 90s\n", loads_);
    std::fflush(file_);
  }
  void OnStepMark(const std::string& text) override {
    std::lock_guard lock(mutex_);
    if (!recording_ || ended_) return;
    Flush(Clock::now());
    if (text.rfind("Done", 0) == 0) {
      std::fputs("quit\n", file_);
      ended_ = true;
    } else {
      std::fprintf(file_, "mark %s\n", text.c_str());
    }
    std::fflush(file_);
  }

 private:
  // Sticks to 1/32 with a small dead zone and triggers to 1/16 of their range: an analog stick
  // never holds one raw value, and a line per frame would be no script.
  static PadState Quantize(const rex::input::X_INPUT_STATE& s) {
    auto axis = [](int16_t raw) -> int16_t {
      const double v = std::clamp(raw / 32767.0, -1.0, 1.0);
      if (std::abs(v) < 0.12) return 0;
      return int16_t(std::lround(std::round(v * 32) / 32 * 32767));
    };
    auto trigger = [](uint8_t raw) -> uint8_t { return raw < 16 ? 0 : uint8_t((raw / 16) * 16 + 15); };
    PadState p;
    p.buttons = s.gamepad.buttons;
    p.left_trigger = trigger(s.gamepad.left_trigger);
    p.right_trigger = trigger(s.gamepad.right_trigger);
    p.left_x = axis(s.gamepad.thumb_lx);
    p.left_y = axis(s.gamepad.thumb_ly);
    p.right_x = axis(s.gamepad.thumb_rx);
    p.right_y = axis(s.gamepad.thumb_ry);
    return p;
  }
  // Writes the controller held since since_ (with the mutex held).
  void Flush(Clock::time_point now) {
    const long ms = long(std::chrono::duration<double, std::milli>(now - since_).count() + 0.5);
    since_ = now;
    if (ms <= 0 || ended_) return;
    if (current_ == PadState{}) {
      std::fprintf(file_, "idle %ldms\n", ms);
    } else {
      std::string line = "state";
      static constexpr std::pair<uint16_t, const char*> kNames[] = {
          {pad::kA, "A"},         {pad::kB, "B"},          {pad::kX, "X"},
          {pad::kY, "Y"},         {pad::kStart, "START"},  {pad::kBack, "BACK"},
          {pad::kLeftShoulder, "LB"}, {pad::kRightShoulder, "RB"}, {pad::kUp, "UP"},
          {pad::kDown, "DOWN"},   {pad::kLeft, "LEFT"},    {pad::kRight, "RIGHT"},
          {pad::kLeftThumb, "LS"}, {pad::kRightThumb, "RS"}};
      for (const auto& [bit, name] : kNames) {
        if (current_.buttons & bit) line += std::string(" ") + name;
      }
      char buf[96];
      if (current_.left_x || current_.left_y) {
        std::snprintf(buf, sizeof(buf), " lstick %.4g %.4g", current_.left_x / 32767.0,
                      current_.left_y / 32767.0);
        line += buf;
      }
      if (current_.right_x || current_.right_y) {
        std::snprintf(buf, sizeof(buf), " rstick %.4g %.4g", current_.right_x / 32767.0,
                      current_.right_y / 32767.0);
        line += buf;
      }
      if (current_.left_trigger) line += " lt " + std::to_string(current_.left_trigger);
      if (current_.right_trigger) line += " rt " + std::to_string(current_.right_trigger);
      std::fprintf(file_, "%s for %ldms\n", line.c_str(), ms);
    }
    std::fflush(file_);
  }

  std::mutex mutex_;
  rex::input::InputSystem* input_;
  std::FILE* file_;
  bool recording_ = false, ended_ = false;
  uint32_t loads_ = 0;
  PadState current_;
  Clock::time_point since_{};
};

std::unique_ptr<InputRecorder> g_recorder;

}  // namespace

void InstallInputScript(rex::Runtime* runtime, std::function<void()> request_close) {
  if (const std::string record = REXCVAR_GET(dev_input_record); !record.empty()) {
    auto* input = runtime ? static_cast<rex::input::InputSystem*>(runtime->input_system()) : nullptr;
    std::FILE* file = input ? std::fopen(record.c_str(), "w") : nullptr;
    if (!file) {
      REXLOG_ERROR("dev input record: cannot write {}", record);
      return;
    }
    g_recorder = std::make_unique<InputRecorder>(input, file);
    live::SetFrameObserver(g_recorder.get());
    REXLOG_INFO("dev input record: the controller goes to {}", record);
    if (!REXCVAR_GET(dev_input_script).empty()) {
      REXLOG_WARN("dev input record: --dev_input_script ignored while recording");
    }
    return;
  }
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
