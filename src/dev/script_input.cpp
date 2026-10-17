#include "dev/script_input.h"

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(dev_input_script, "", "Torchlight",
                      "Development builds only: a file of controller commands played into the game "
                      "(docs/dev-input-script.md)");

#ifndef TORCHLIGHT_DEV_INPUT

namespace torchlight::dev {
void OnUiFrame(PPCContext&, uint8_t*) {}
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
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/cvar.h>
#include <rex/runtime.h>

#include "capture/session.h"
#include "dev/guest_command.h"
#include "dev/input_script.h"
#include "dev/ui_tree.h"
#include "game_menu/guest_call.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/game_ui_sheet.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/xbox_memory.h"
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

// ---------------------------------------------------------------------------------------------
// The game UI, read from the guest's CEGUI (guest_abi game_ui.h, game_ui_sheet.h): what focus and
// window waits match, and dump_ui's tree. On the game's thread, from CGameUI::Update (OnUiFrame).
namespace ui_abi = torchlight::guest_abi::game_ui;
namespace sheet_abi = torchlight::guest_abi::game_ui_sheet;

constexpr size_t kMaxWindows = 6000;
constexpr int kMaxDepth = 40;

bool InGuestHeap(uint32_t a) { return a >= 0x40000000u && a < 0xA0000000u; }

// A CEGUI String (UTF-32): length +0, reserve +4, inline code points at +0x14 or the heap buffer at
// +0x94 past 0x20 (game_ui.h cegui_string).
std::string ReadCeguiString(const uint8_t* base, uint32_t str) {
  namespace cs = ui_abi::cegui_string;
  if (!InGuestHeap(str)) return {};
  const uint32_t length = abi::ReadU32(base, str + cs::kLength.offset);
  const uint32_t reserve = abi::ReadU32(base, str + cs::kReserve.offset);
  if (length > 4096) return {};
  const uint32_t data = reserve > cs::kInlineCapacity ? abi::ReadU32(base, str + cs::kHeapBuffer.offset)
                                                      : str + cs::kInlineBuffer.offset;
  if (length && !InGuestHeap(data)) return {};
  std::vector<uint32_t> points(length);
  for (uint32_t i = 0; i < length; ++i) points[i] = abi::ReadU32(base, data + 4 * i);
  return Utf32ToUtf8(points);
}

std::string WindowName(const uint8_t* base, uint32_t window) {
  return ReadCeguiString(base, window + ui_abi::kWindowName.offset);
}

std::vector<uint32_t> Children(const uint8_t* base, uint32_t window) {
  namespace w = sheet_abi::window;
  const uint32_t begin = abi::ReadU32(base, window + w::kChildrenBegin.offset);
  const uint32_t end = abi::ReadU32(base, window + w::kChildrenEnd.offset);
  std::vector<uint32_t> out;
  if (!begin || end < begin || end - begin > 4 * 4096) return out;
  for (uint32_t at = begin; at < end; at += 4) {
    const uint32_t child = abi::ReadU32(base, at);
    if (InGuestHeap(child)) out.push_back(child);
  }
  return out;
}

bool OwnVisible(const uint8_t* base, uint32_t window) {
  return abi::ReadU8(base, window + ui_abi::kWindowVisible.offset) != 0;
}

uint32_t Sheet(const uint8_t* base, uint32_t game_ui) {
  return abi::ReadU32(base, game_ui + sheet_abi::game_ui_object::kSheet.offset);
}

// The names of the windows shown (visible with every parent) under the sheet.
void CollectShown(const uint8_t* base, uint32_t window, int depth, std::vector<std::string>& out) {
  if (depth > kMaxDepth || out.size() >= kMaxWindows || !OwnVisible(base, window)) return;
  out.push_back(WindowName(base, window));
  for (uint32_t child : Children(base, window)) CollectShown(base, child, depth + 1, out);
}

// The deepest shown, active window at or under `window` (the first active child at each level), or
// 0 when `window` itself is not.
uint32_t DeepestActive(const uint8_t* base, uint32_t window, int depth) {
  if (depth > kMaxDepth || !OwnVisible(base, window) ||
      abi::ReadU8(base, window + ui_abi::kWindowActive.offset) == 0) {
    return 0;
  }
  for (uint32_t child : Children(base, window)) {
    if (const uint32_t found = DeepestActive(base, child, depth + 1)) return found;
  }
  return window;
}

// Every menu SetOpen has opened and not closed since (the hook below): CGameUI's list
// (game_ui_object::kMenus) leaves out the front end's menus (the main menu is not in it). Game
// thread.
std::mutex g_menus_mutex;
std::vector<uint32_t> g_menus;

void NoteMenu(uint32_t menu, bool open) {
  std::lock_guard lock(g_menus_mutex);
  const auto at = std::find(g_menus.begin(), g_menus.end(), menu);
  if (open && at == g_menus.end()) g_menus.push_back(menu);
  if (!open && at != g_menus.end()) g_menus.erase(at);
}

// The open menus, each with its layout's root and the window its pad focus is on
// (the deepest active window, DeepestActive), latest opened last.
std::vector<UiMenu> OpenMenus(const uint8_t* base) {
  namespace dm = ui_abi::dropdown_menu;
  std::vector<uint32_t> menus;
  {
    std::lock_guard lock(g_menus_mutex);
    menus = g_menus;
  }
  std::vector<UiMenu> out;
  for (uint32_t menu : menus) {
    if (!InGuestHeap(menu) || abi::ReadU8(base, menu + dm::kOpen.offset) == 0) continue;
    UiMenu m;
    m.class_name = ClassName(base, menu);
    if (m.class_name.empty()) continue;  // destroyed since
    const uint32_t root = abi::ReadU32(base, menu + dm::kRoot.offset);
    if (InGuestHeap(root)) m.root = WindowName(base, root);
    // The pad focus: the deepest active window under the menu's root (game_ui.h kWindowActive).
    if (InGuestHeap(root)) {
      const uint32_t focus = DeepestActive(base, root, 0);
      if (focus && focus != root) {
        m.focus = WindowName(base, focus);
        for (uint32_t w : Children(base, focus)) {
          if (std::string text = ReadCeguiString(base, w + ui_abi::kWindowText.offset); !text.empty()) {
            m.focus_texts.push_back(std::move(text));
          }
        }
        if (std::string text = ReadCeguiString(base, focus + ui_abi::kWindowText.offset); !text.empty()) {
          m.focus_texts.push_back(std::move(text));
        }
      }
    }
    out.push_back(std::move(m));
  }
  return out;
}

// Guest calls for one dump: CEGUI Strings in the scratch area, the rect getters, the property map.
class WindowReader {
 public:
  WindowReader(PPCContext& ctx, uint8_t* base) : call_(ctx, base, 0x4000), base_(base) {}

  UiRect Rect(uint32_t window, bool clipped) {
    namespace r = ui_abi::cegui_rect;
    const uint32_t mark = call_.Mark();
    const uint32_t rect = call_.Reserve(r::kSize);
    UiRect out;
    if (rect) {
      call_.Call((clipped ? ui_abi::kWindowPixelRect : ui_abi::kWindowUnclippedPixelRect).address, {rect, window});
      out.top = Float(rect + r::kTop.offset);
      out.bottom = Float(rect + r::kBottom.offset);
      out.left = Float(rect + r::kLeft.offset);
      out.right = Float(rect + r::kRight.offset);
    }
    call_.Release(mark);
    return out;
  }

  // The property's value when the window has it (kWindowPropertyMap), else none: getProperty
  // throws for a property the window lacks.
  std::optional<std::string> Property(uint32_t window, const char* name) {
    const uint32_t mark = call_.Mark();
    const uint32_t key = MakeString(name);
    const uint32_t iterator = call_.Reserve(8);
    std::optional<std::string> value;
    if (key && iterator) {
      call_.Call(ui_abi::kPropertyMapFind.address, {iterator, window + ui_abi::kWindowPropertyMap.offset, key});
      const bool present = call_.ReadU32(iterator) != call_.ReadU32(window + ui_abi::kWindowPropertyMapEnd.offset);
      const uint32_t result = present ? call_.Reserve(ui_abi::kCeguiStringSize) : 0;
      if (result) {
        call_.Call(ui_abi::kWindowGetProperty.address, {result, window, key});
        value = ReadCeguiString(base_, result);
        call_.Call(ui_abi::kCeguiStringDtor.address, {result});
      }
      call_.Call(ui_abi::kCeguiStringDtor.address, {key});
    }
    call_.Release(mark);
    return value;
  }

 private:
  uint32_t MakeString(const char* text) {
    const size_t n = std::strlen(text);
    const uint32_t chars = call_.Reserve(uint32_t(n + 1));
    const uint32_t str = call_.Reserve(ui_abi::kCeguiStringSize);
    if (!chars || !str) return 0;
    std::memcpy(abi::xbox_memory::HostAddress(base_, chars), text, n + 1);
    call_.Call(ui_abi::kCeguiStringFromUtf8.address, {str, chars});
    return str;
  }
  float Float(uint32_t address) {
    const uint32_t bits = call_.ReadU32(address);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  }

  game_menu::GuestCall call_;
  uint8_t* base_;
};

constexpr const char* kImageProperties[] = {"Image", "NormalImage", "HoverImage", "PushedImage", "DisabledImage"};
constexpr const char* kOtherProperties[] = {
    "UnifiedAreaRect", "HorizontalAlignment", "VerticalAlignment", "TextColour", "HorzTextFormatting",
    "VertFormatting",  "tabOrder",            "ClippedByParent",   "AlwaysOnTop", "InheritsAlpha",
    "Disabled"};

UiWindow ReadWindow(WindowReader& reader, const uint8_t* base, uint32_t window, bool parent_shown,
                    float parent_alpha, int depth, size_t& count) {
  UiWindow w;
  ++count;
  w.name = WindowName(base, window);
  w.type = ReadCeguiString(base, window + ui_abi::kWindowType.offset);
  w.class_name = ClassName(base, window);
  w.visible = OwnVisible(base, window);
  w.shown = parent_shown && w.visible;
  w.active = abi::ReadU8(base, window + ui_abi::kWindowActive.offset) != 0;
  w.rect = reader.Rect(window, false);
  w.clipped = reader.Rect(window, true);
  w.text = ReadCeguiString(base, window + ui_abi::kWindowText.offset);
  if (const auto alpha = reader.Property(window, "Alpha")) w.alpha = std::strtof(alpha->c_str(), nullptr);
  if (const auto font = reader.Property(window, "Font")) w.font = *font;
  for (const char* name : kImageProperties) {
    if (const auto value = reader.Property(window, name)) {
      if (const auto image = ParseImageProperty(*value)) w.images[name] = *image;
    }
  }
  for (const char* name : kOtherProperties) {
    if (const auto value = reader.Property(window, name)) w.properties[name] = *value;
  }
  const auto inherits = w.properties.find("InheritsAlpha");
  const bool inherit = inherits == w.properties.end() || inherits->second != "False";
  w.effective_alpha = w.alpha.value_or(1.0f) * (inherit ? parent_alpha : 1.0f);
  if (depth < kMaxDepth) {
    for (uint32_t child : Children(base, window)) {
      if (count >= kMaxWindows) break;
      w.children.push_back(ReadWindow(reader, base, child, w.shown, w.effective_alpha, depth + 1, count));
    }
  }
  return w;
}

// One run of measured frames between two marks, logged as the step overlay logs its steps (the
// measurement scripts read these lines).
struct Segment {
  std::string name;
  std::vector<double> frames, presented;
  std::vector<live::PresentLatency> latency;
  std::vector<double> screen;  // swap to screen, where the window system tells (OnScreenFrame)
  size_t screen_gaps = 0;      // screen frames more than 25 ms after the previous one
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
    if (latency.empty()) return;
    const live::LatencyMeans m = live::MeanLatency(latency);
    std::vector<double> totals;
    for (const auto& l : latency) totals.push_back(l.total_ms());
    const auto t = stats(totals, sum, p99);
    REXLOG_INFO("perf step '{}' latency: mean {:.2f} ms (queue {:.2f}, render {:.2f}, present "
                "{:.2f}), p99 {:.2f}, max {:.2f}",
                name, m.total_ms, m.queue_ms, m.render_ms, m.present_ms, p99, t.back());
    if (screen.empty()) return;
    const auto sc = stats(screen, sum, p99);
    REXLOG_INFO("perf step '{}' on screen: {} frames, latency mean {:.2f} ms, p99 {:.2f}, max {:.2f}, "
                "{} gaps over 25 ms",
                name, sc.size(), sum / sc.size(), p99, sc.back(), screen_gaps);
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
    // A keystroke the game did not read soon after it happened is dropped: a screen that does not
    // read keystrokes (the title) left its presses to the next one, which acted on them later.
    const auto now = Clock::now();
    while (!keys_.empty() && now - keys_.front().when > kKeystrokeLifetime) keys_.pop_front();
    if (keys_.empty() || !is_active()) return X_ERROR_EMPTY;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->virtual_key = keys_.front().key.virtual_key;
      out->flags = keys_.front().key.down ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN
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
      if (keys_.size() < 64) keys_.push_back({k, Clock::now()});
    }
    pad_ = pad;
    ++packet_;
  }
  // The script ended: the device goes away and a real controller is alone again.
  void Disconnect() { connected_ = false; }

 private:
  static constexpr auto kKeystrokeLifetime = std::chrono::milliseconds(200);
  struct TimedKeystroke {
    PadKeystroke key;
    Clock::time_point when;
  };
  std::mutex mutex_;
  PadState pad_;
  uint32_t packet_ = 1;
  std::deque<TimedKeystroke> keys_;
  std::atomic<bool> connected_{true};
};

// Steps the script once per guest frame and runs its actions.
class ScriptRunner : public live::FrameObserver {
 public:
  ScriptRunner(InputScript script, ScriptInputDriver* driver, std::function<void()> close)
      : script_(std::move(script)), driver_(driver), close_(std::move(close)),
        start_(Clock::now()), reads_ui_(script_.ReadsUi()) {}

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
  void OnPresentedFrame(double ms, size_t dropped, const live::PresentLatency& latency) override {
    std::lock_guard lock(mutex_);
    segment_.presented.push_back(ms);
    segment_.latency.push_back(latency);
    segment_.dropped += dropped;
  }
  void OnScreenFrame(double ms, double gap_ms) override {
    std::lock_guard lock(mutex_);
    segment_.screen.push_back(ms);
    if (gap_ms > 25.0) ++segment_.screen_gaps;
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
  // The game's thread (OnUiFrame): whether the script waits on the UI, the UI as read now, and
  // the dumps asked for.
  bool ReadsUi() const { return reads_ui_; }
  void OnUiState(std::vector<std::string> focused, std::vector<std::string> shown) {
    std::lock_guard lock(mutex_);
    events_.ui_reads++;
    events_.focused = std::move(focused);
    events_.shown_windows = std::move(shown);
  }
  std::optional<std::string> TakeDump() {
    std::lock_guard lock(mutex_);
    if (dumps_.empty()) return std::nullopt;
    std::string name = std::move(dumps_.front());
    dumps_.pop_front();
    return name;
  }
  uint64_t frame() {
    std::lock_guard lock(mutex_);
    return events_.frame;
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
        if (a.text.empty()) {
          REXLOG_INFO("dev input script: capture");
        } else {
          REXLOG_INFO("dev input script: capture {}", a.text);
        }
        capture::Session::Get().RequestCapture();
        break;
      case ScriptAction::Kind::kDumpUi:
        REXLOG_INFO("dev input script: dump_ui {}", a.text);
        dumps_.push_back(a.text);
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
  std::deque<std::string> dumps_;  // dump_ui names, for OnUiFrame
  InputScript script_;
  ScriptInputDriver* driver_;
  std::function<void()> close_;
  Clock::time_point start_;
  ScriptEvents events_;
  Segment segment_;
  int step_ = 0;
  bool finished_ = false;
  const bool reads_ui_;
};

std::unique_ptr<ScriptRunner> g_runner;

void WriteDump(PPCContext& ctx, uint8_t* base, uint32_t game_ui, const std::string& name) {
  const std::string folder = rex::cvar::Query<std::string>("capture_dir");
  if (folder.empty()) {
    REXLOG_ERROR("dev input script: dump_ui {} needs --capture_dir; not written", name);
    return;
  }
  const uint32_t sheet = Sheet(base, game_ui);
  if (!InGuestHeap(sheet)) {
    REXLOG_ERROR("dev input script: dump_ui {}: no GUI sheet", name);
    return;
  }
  UiDump dump;
  dump.name = name;
  dump.frame = g_runner->frame();
  dump.menus = OpenMenus(base);
  size_t count = 0;
  {
    WindowReader reader(ctx, base);
    const UiRect screen = reader.Rect(sheet, false);
    dump.display_width = screen.right - screen.left;
    dump.display_height = screen.bottom - screen.top;
    for (uint32_t child : Children(base, sheet)) {
      if (count >= kMaxWindows) break;
      dump.windows.push_back(ReadWindow(reader, base, child, OwnVisible(base, sheet), 1.0f, 1, count));
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const std::filesystem::path path = std::filesystem::path(folder) / (name + ".json");
  std::ofstream out(path, std::ios::binary);
  out << UiDumpJson(dump);
  if (!out) {
    REXLOG_ERROR("dev input script: dump_ui {}: cannot write {}", name, path.string());
    return;
  }
  REXLOG_INFO("dev input script: dump_ui {}: {} windows, display {}x{}, to {}", name, count, dump.display_width,
              dump.display_height, path.string());
}

}  // namespace

void OnUiFrame(PPCContext& ctx, uint8_t* base) {
  if (!g_runner) return;
  const uint32_t game_ui = abi::ReadU32(base, ui_abi::kGameUiGlobal);
  if (!InGuestHeap(game_ui)) return;
  if (g_runner->ReadsUi()) {
    std::vector<std::string> focused, shown;
    for (const UiMenu& m : OpenMenus(base)) {
      if (m.focus.empty()) continue;
      focused.push_back(m.focus);
      // A button's label is often a child of it with no name of its own: its text counts too.
      for (const std::string& text : m.focus_texts) focused.push_back(text);
    }
    const uint32_t sheet = Sheet(base, game_ui);
    if (InGuestHeap(sheet)) CollectShown(base, sheet, 0, shown);
    // Every change of focus in the log, for writing and reading scripts.
    static std::vector<std::string> last_focused;
    if (focused != last_focused) {
      std::string list;
      for (const auto& f : focused) list += (list.empty() ? "" : " | ") + f;
      REXLOG_INFO("dev input script: focus: {}", list.empty() ? std::string("none") : list);
      last_focused = focused;
    }
    g_runner->OnUiState(std::move(focused), std::move(shown));
  }
  while (auto name = g_runner->TakeDump()) WriteDump(ctx, base, game_ui, *name);
}

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
  const bool request = ctx.r4.u32 != 0;
  __imp__sub_82351FF0(ctx, base);
  if (torchlight::dev::g_runner) {
    torchlight::dev::NoteMenu(menu, request && abi::ReadU8(base, menu + abi::game_ui::dropdown_menu::kOpen.offset) != 0);
  }
  if (opening && torchlight::dev::g_runner) torchlight::dev::g_runner->OnMenuOpened(std::move(name));
}

}  // extern "C"

#endif  // TORCHLIGHT_DEV_INPUT
