#include "dev/script_input.h"

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(dev_input_script, "", "Torchlight",
                      "Development builds only: a file of controller commands played into the game "
                      "(docs/dev-input-script.md)");

#ifndef TORCHLIGHT_DEV_INPUT

namespace torchlight::dev {
void OnUiFrame(PPCContext&, uint8_t*) {}
void OnPadStateRead(const uint8_t*, uint32_t, uint32_t, uint32_t) {}
void InstallInputScript(rex::Runtime*, std::function<void()>) {
  if (!REXCVAR_GET(dev_input_script).empty()) {
    REXLOG_WARN("dev: --dev_input_script ignored: this build has no input scripts "
                "(CMake option TORCHLIGHT_DEV_INPUT)");
  }
}
}  // namespace torchlight::dev

#else

#include <algorithm>
#include <bit>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include "live/touch_input.h"
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
#include "guest_abi/hero_motion.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/xbox_memory.h"
#include "guest_abi/xinput.h"
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
// window waits match, and dump_ui's tree. On the game's thread, at the frame's swap (OnUiFrame).
namespace ui_abi = torchlight::guest_abi::game_ui;
namespace sheet_abi = torchlight::guest_abi::game_ui_sheet;

constexpr size_t kMaxWindows = 6000;
constexpr int kMaxDepth = 40;

bool InGuestHeap(uint32_t a) { return a >= 0x40000000u && a < 0xA0000000u; }

// Set by a script's `mark watch interact target` (WatchInteractTarget, below).
bool g_watch_target = false;

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

void FocusTexts(const uint8_t* base, uint32_t focus, UiMenu& m) {
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

// The open menus, each with its layout's root and the window its pad focus is on
// (the deepest active window, DeepestActive), latest opened last; then, as menus with no class,
// the GUI sheet's other shown roots that hold a focus (a menu whose own SetOpen does not reach the
// base one, the settings menu's among them).
std::vector<UiMenu> OpenMenus(const uint8_t* base, uint32_t sheet) {
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
      if (focus && focus != root) FocusTexts(base, focus, m);
    }
    out.push_back(std::move(m));
  }
  if (InGuestHeap(sheet)) {
    for (uint32_t root : Children(base, sheet)) {
      const std::string name = WindowName(base, root);
      if (std::any_of(out.begin(), out.end(), [&](const UiMenu& m) { return m.root == name; })) continue;
      const uint32_t focus = DeepestActive(base, root, 0);
      if (!focus || focus == root) continue;
      UiMenu m;
      m.root = name;
      FocusTexts(base, focus, m);
      out.push_back(std::move(m));
    }
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
    // Not read while the input is inactive: the keystrokes wait (and expire) as they would.
    if (!is_active()) return X_ERROR_EMPTY;
    const auto key = keys_.Pop(input::KeystrokeQueue::Clock::now());
    if (!key) return X_ERROR_EMPTY;
    if (out) {
      std::memset(out, 0, sizeof(*out));
      out->virtual_key = key->virtual_key;
      out->flags = key->down ? rex::input::X_INPUT_KEYSTROKE_KEYDOWN : rex::input::X_INPUT_KEYSTROKE_KEYUP;
    }
    return X_ERROR_SUCCESS;
  }

  // The guest's render thread, once a frame.
  void Set(const PadState& pad) {
    std::lock_guard lock(mutex_);
    if (pad == pad_) return;
    keys_.Push(pad_, pad, input::KeystrokeQueue::Clock::now());
    pad_ = pad;
    ++packet_;
  }
  // The script ended: the device goes away and a real controller is alone again.
  void Disconnect() { connected_ = false; }

 private:
  std::mutex mutex_;
  PadState pad_;
  uint32_t packet_ = 1;
  input::KeystrokeQueue keys_;
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
    // A replay's frame, for the trace OnUiFrame writes at the next swap, once this pad was read.
    if (const auto& f = script_.replay_frame()) {
      trace_ = TraceFrame{*f, events_.frame};
      replaying_ = true;
    } else {
      replaying_ = false;
    }
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
  void OnDumpDone(bool written) {
    std::lock_guard lock(mutex_);
    (written ? events_.dumps_written : events_.dumps_failed)++;
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
  // The game's thread (OnUiFrame): the replay frame given to the game since the last call, and
  // whether a replay is still playing.
  struct TraceFrame {
    ReplayFrame replay;
    uint64_t frame = 0;
  };
  std::optional<TraceFrame> TakeTraceFrame(bool& replaying) {
    std::lock_guard lock(mutex_);
    replaying = replaying_;
    return std::exchange(trace_, std::nullopt);
  }

 private:
  // With the mutex held.
  void Run(const ScriptAction& a) {
    switch (a.kind) {
      case ScriptAction::Kind::kMark:
        segment_.Log();
        segment_ = Segment{a.text};
        REXLOG_INFO("perf step {}: {}", step_++, a.text);
        if (a.text == "watch interact target") g_watch_target = true;
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
      case ScriptAction::Kind::kTouch:
        if (a.release) {
          REXLOG_INFO("dev input script: finger {} lifted", a.finger);
          live::InjectTouch(static_cast<uint64_t>(a.finger), a.x, a.y, true);
        } else if (!a.text.empty()) {
          float x = 0, y = 0;
          if (live::TouchControlCentre(a.text, x, y)) {
            REXLOG_INFO("dev input script: finger {} on {} ({:.3f}, {:.3f})", a.finger, a.text, x, y);
            live::InjectTouch(static_cast<uint64_t>(a.finger), x, y, false);
          } else {
            REXLOG_WARN("dev input script: no touch control {} (touch controls off?)", a.text);
          }
        } else {
          REXLOG_INFO("dev input script: finger {} at {:.3f}, {:.3f}", a.finger, a.x, a.y);
          live::InjectTouch(static_cast<uint64_t>(a.finger), a.x, a.y, false);
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
  std::optional<TraceFrame> trace_;  // for OnUiFrame
  bool replaying_ = false;
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
std::filesystem::path g_script_folder;  // the script's relative paths start here

// A name the script gives a file, from the script's folder when relative.
std::filesystem::path ScriptPath(std::string_view name) {
  const std::filesystem::path path{std::string(name)};
  return path.is_absolute() ? path : g_script_folder / path;
}

// While a trace is written: the last stick vector the hero's input handler read
// (hm::kReadStick's result, the hook below), and how many times it read one. Game's thread only.
bool g_watch_stick = false;

// Diagnostic (branch diag/interact-height, not for merging): the HUD's "A Interact" prompt sits at
// the hover target's position + 0.4 on x and z and + 1.0 - target+348 on y (sub_821A83C8
// @0x821AB3F0). A script's `mark watch interact target` starts logging, at each change of the target
// (the game UI's +72; its +56 is the player), the target's name (+1028), its +336..+348 and position.
std::string ReadGuestWString(const uint8_t* base, uint32_t str) {
  // The guest's std::wstring: the characters inline at +0 while the capacity (+20) is below 8, else
  // a pointer at +0; the length at +16 (sub_822D32C8 @0x822D3314).
  const uint32_t length = abi::ReadU32(base, str + 16);
  const uint32_t capacity = abi::ReadU32(base, str + 20);
  if (length > 256) return {};
  const uint32_t data = capacity >= 8 ? abi::ReadU32(base, str) : str;
  if (length && !InGuestHeap(data)) return {};
  std::vector<uint32_t> points(length);
  for (uint32_t i = 0; i < length; ++i) points[i] = abi::ReadU16(base, data + 2 * i);
  return Utf32ToUtf8(points);
}

float ReadGuestF32(const uint8_t* base, uint32_t a) { return std::bit_cast<float>(abi::ReadU32(base, a)); }

void WatchInteractTarget(const uint8_t* base) {
  static uint32_t last = 0;
  const uint32_t ui = abi::ReadU32(base, ui_abi::kGameUiGlobal);
  if (!InGuestHeap(ui)) return;
  const uint32_t target = abi::ReadU32(base, ui + 72);
  if (target == last) return;
  last = target;
  if (!InGuestHeap(target)) {
    REXLOG_INFO("dev interact target: none");
    return;
  }
  REXLOG_INFO("dev interact target: {:08X} \"{}\" +336..+348 = {:.4f} {:.4f} {:.4f} {:.4f}, position {:.3f} {:.3f} "
              "{:.3f}, y lift 1.0 - +348 = {:.4f}",
              target, ReadGuestWString(base, target + 1028), ReadGuestF32(base, target + 336),
              ReadGuestF32(base, target + 340), ReadGuestF32(base, target + 344), ReadGuestF32(base, target + 348),
              ReadGuestF32(base, target + 0x70), ReadGuestF32(base, target + 0x74),
              ReadGuestF32(base, target + 0x78), 1.0f - ReadGuestF32(base, target + 348));
}
float g_stick_x = 0, g_stick_y = 0;
uint64_t g_stick_reads = 0;
// The same frame's last pad state the recomp's XamInputGetState gave the game (OnPadStateRead).
int16_t g_xam_lx = 0, g_xam_ly = 0;
uint32_t g_xam_user = 0;
uint64_t g_xam_reads = 0;

// The replay's hero trace (dev/pad_replay.h), written on the game's thread at each swap: the hero
// as the frame that ends now left it, and the stick the game read in it.
class HeroTrace {
 public:
  void Write(const uint8_t* base, const ScriptRunner::TraceFrame& f) {
    if (!out_.is_open() || f.replay.trace != name_) {
      Close();
      if (f.replay.trace.empty()) return;
      name_ = f.replay.trace;
      path_ = ScriptPath(name_);
      out_.open(path_, std::ios::binary | std::ios::trunc);
      if (!out_) {
        REXLOG_ERROR("dev input script: cannot write the trace {}", path_.string());
        return;
      }
      out_ << HeroTraceHeader();
      REXLOG_INFO("dev input script: trace to {}", path_.string());
      // Next to it, the stick the game read in each frame against the one given.
      stick_path_ = path_;
      stick_path_ += ".stick.csv";
      stick_.open(stick_path_, std::ios::binary | std::ios::trunc);
      stick_ << "# the stick given, as XamInputGetState gave it to the game, and as the hero's input handler "
                "read it (sub_82191018, x1000)\n"
                "t_us,frame,lx,ly,xam_lx,xam_ly,xam_user,xam_reads,read_x_mu,read_y_mu,reads\n";
      g_xam_reads = 0;
      g_watch_stick = true;
      g_stick_reads = 0;
    }
    namespace hm = abi::hero_motion;
    const uint32_t client = hm::Client(base);
    const uint32_t hero = client ? abi::ReadU32(base, client + hm::kClientHero.offset) : 0;
    const uint32_t node = InGuestHeap(hero) ? abi::ReadU32(base, hero + hm::kUnitNode.offset) : 0;
    if (!InGuestHeap(node)) {
      // Once per trace, what the chain gave: the first link that is 0 or outside the heap failed.
      if (!missing_++) {
        REXLOG_WARN("dev input script: trace: no hero at frame {}: client {:08X}, hero {:08X}, node {:08X}",
                    f.frame, client, hero, node);
      }
      return;
    }
    // The node's world transform as _getFullTransform would build it, read without updating it:
    // stale while the node waits for its parent's update.
    if (abi::ReadU8(base, node + hm::kNodeNeedParentUpdate.offset)) ++stale_;
    const auto f32 = [&](uint32_t at) { return abi::ReadF32(base, at); };
    const uint32_t q = node + hm::kNodeDerivedOrientation.offset;
    const float w = f32(q), qx = f32(q + 4), qy = f32(q + 8), qz = f32(q + 12);
    const float scale_z = f32(node + hm::kNodeDerivedScale.offset + 8);
    HeroTraceRow row;
    row.t_us = f.replay.t_us;
    row.frame = f.frame;
    row.x = f32(node + hm::kNodeDerivedPosition.offset);
    row.z = f32(node + hm::kNodeDerivedPosition.offset + 8);
    // The rotation matrix's third column, rows 0 and 2, times the scale (Matrix4 = R * S).
    row.fx = 2 * (qx * qz + w * qy) * scale_z;
    row.fz = (1 - 2 * (qx * qx + qy * qy)) * scale_z;
    row.scale = f32(hero + hm::kSpeedScale.offset);
    row.speed = f32(hero + hm::kSpeed.offset);
    row.lx = f.replay.pad.left_x;
    row.ly = f.replay.pad.left_y;
    if (!lines_) {
      // The node against the unit's own place (hm::kUnitPosition) and orientation, once.
      const uint32_t o = node + hm::kNodeOrientation.offset;
      REXLOG_INFO("dev input script: trace: hero {:08X}, node {:08X}: node at ({:.3f}, {:.3f}), unit at "
                  "({:.3f}, {:.3f}); orientation derived ({:.3f}, {:.3f}, {:.3f}, {:.3f}), own ({:.3f}, "
                  "{:.3f}, {:.3f}, {:.3f}), scale z {:.3f}",
                  hero, node, row.x, row.z, f32(hero + hm::kUnitPosition.offset),
                  f32(hero + hm::kUnitPosition.offset + 8), w, qx, qy, qz, f32(o), f32(o + 4), f32(o + 8),
                  f32(o + 12), scale_z);
    }
    if (!camera_logged_ && row.t_us >= 1000000) {
      camera_logged_ = true;
      LogStickTurn(base, client, row);
    }
    WriteStick(row);
    out_ << HeroTraceLine(row);
    if (++lines_ % 64 == 0) out_.flush();
  }
  // Once, 1 s after t=0: the angle the hero's input handler turns the stick by this frame, as it
  // computes it from the camera (hm::kCameraOrientation), with the camera's and the hero's place.
  static void LogStickTurn(const uint8_t* base, uint32_t client, const HeroTraceRow& row) {
    namespace hm = abi::hero_motion;
    const uint32_t holder = abi::ReadU32(base, client + hm::kClientCameraHolder.offset);
    const uint32_t camera = InGuestHeap(holder) ? abi::ReadU32(base, holder + hm::kCameraHolderCamera.offset) : 0;
    if (!InGuestHeap(camera)) {
      REXLOG_WARN("dev input script: trace: no camera (holder {:08X}, camera {:08X})", holder, camera);
      return;
    }
    const auto f32 = [&](uint32_t at) { return abi::ReadF32(base, at); };
    const uint32_t q = camera + hm::kCameraOrientation.offset;
    const float w = f32(q), qx = f32(q + 4), qy = f32(q + 8), qz = f32(q + 12);
    // Quaternion * Vector3 (OGRE's: v + 2w (q x v) + 2 q x (q x v)) of minus kUnitZ.
    const float vx = -f32(hm::kUnitZ), vy = -f32(hm::kUnitZ + 4), vz = -f32(hm::kUnitZ + 8);
    const float ux = qy * vz - qz * vy, uy = qz * vx - qx * vz, uz = qx * vy - qy * vx;
    const float uux = qy * uz - qz * uy, uuy = qz * ux - qx * uz, uuz = qx * uy - qy * ux;
    const float fx = vx + 2 * w * ux + 2 * uux, fy = vy + 2 * w * uy + 2 * uuy, fz = vz + 2 * w * uz + 2 * uuz;
    const float degrees = float(std::atan2(fx, fz)) * f32(hm::kStickTurnScale) - f32(hm::kStickTurnOffset);
    const uint32_t p = camera + hm::kCameraPosition.offset;
    REXLOG_INFO("dev input script: trace: stick turn at t={} us, frame {}: {:.4f} degrees ({:.6f} rad); "
                "camera {:08X} orientation ({:.5f}, {:.5f}, {:.5f}, {:.5f}), forward ({:.5f}, {:.5f}, "
                "{:.5f}), position ({:.3f}, {:.3f}, {:.3f}); hero at ({:.3f}, {:.3f})",
                row.t_us, row.frame, degrees, degrees * 0.017453292519943295, camera, w, qx, qy, qz, fx, fy,
                fz, f32(p), f32(p + 4), f32(p + 8), row.x, row.z);
  }

  // The stick read in this frame (the last read before this swap) and the given one; and, for the
  // summary, the given lengths the game read as zero and the ones it did not.
  void WriteStick(const HeroTraceRow& row) {
    const int64_t x = std::llround(double(g_stick_x) * 1000.0), y = std::llround(double(g_stick_y) * 1000.0);
    stick_ << row.t_us << ',' << row.frame << ',' << row.lx << ',' << row.ly << ',' << g_xam_lx << ',' << g_xam_ly
           << ',' << g_xam_user << ',' << g_xam_reads << ',' << x << ',' << y << ',' << g_stick_reads << '\n';
    g_stick_reads = 0;
    if (g_xam_reads) {
      if (g_xam_lx == row.lx && g_xam_ly == row.ly) ++xam_same_;
      else ++xam_differs_;
    }
    g_xam_reads = 0;
    const double given = std::hypot(double(row.lx), double(row.ly)) / 32767.0;
    if (given == 0) return;
    if (g_stick_x == 0 && g_stick_y == 0) {
      largest_zeroed_ = std::max(largest_zeroed_, given);
    } else {
      smallest_read_ = std::min(smallest_read_, given);
      const double read = std::hypot(double(g_stick_x), double(g_stick_y));
      max_ratio_error_ = std::max(max_ratio_error_, std::abs(read - given));
    }
  }

  void Close() {
    if (!out_.is_open()) return;
    g_watch_stick = false;
    stick_.close();
    REXLOG_INFO("dev input script: trace: stick read to {}: given lengths read as zero up to {:.4f}, read as "
                "nonzero from {:.4f}, largest |read - given| length {:.4f}",
                stick_path_.string(), largest_zeroed_, smallest_read_, max_ratio_error_);
    REXLOG_INFO("dev input script: trace: XamInputGetState gave the game the given stick in {} frames, "
                "another one in {}",
                xam_same_, xam_differs_);
    xam_same_ = xam_differs_ = 0;
    largest_zeroed_ = 0;
    smallest_read_ = 2;
    max_ratio_error_ = 0;
    out_.close();
    REXLOG_INFO("dev input script: trace {}: {} frames, {} waiting for the parent's update, {} frames "
                "without the hero (not written)",
                path_.string(), lines_, stale_, missing_);
    lines_ = stale_ = missing_ = 0;
    camera_logged_ = false;
    name_.clear();
  }

 private:
  std::ofstream out_;
  std::string name_;
  std::filesystem::path path_;
  uint64_t lines_ = 0, stale_ = 0, missing_ = 0;
  bool camera_logged_ = false;
  std::ofstream stick_;
  std::filesystem::path stick_path_;
  double largest_zeroed_ = 0, smallest_read_ = 2, max_ratio_error_ = 0;
  uint64_t xam_same_ = 0, xam_differs_ = 0;
};
HeroTrace g_hero_trace;

uint64_t g_swaps = 0;  // swaps seen by OnUiFrame: a dump's frame

// Whether the file was written.
bool WriteDump(PPCContext& ctx, uint8_t* base, uint32_t game_ui, const std::string& name) {
  const std::string folder = rex::cvar::Query<std::string>("capture_dir");
  if (folder.empty()) {
    REXLOG_ERROR("dev input script: dump_ui {} needs --capture_dir; not written", name);
    return false;
  }
  const uint32_t sheet = Sheet(base, game_ui);
  if (!InGuestHeap(sheet)) {
    REXLOG_ERROR("dev input script: dump_ui {}: no GUI sheet", name);
    return false;
  }
  UiDump dump;
  dump.name = name;
  dump.frame = g_swaps;
  dump.menus = OpenMenus(base, sheet);
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
    return false;
  }
  REXLOG_INFO("dev input script: dump_ui {}: {} windows, display {}x{}, frame {}, to {}", name, count,
              dump.display_width, dump.display_height, dump.frame, path.string());
  return true;
}

// The run's start in UTC (yyyymmdd-hhmmss; taken at its first frame), in the names of the dumps
// captures get: two runs into one capture folder do not overwrite each other's.
const std::string& RunStamp() {
  static const std::string stamp = [] {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y%m%d-%H%M%S", &utc);
    return std::string(text);
  }();
  return stamp;
}

}  // namespace

void OnUiFrame(PPCContext& ctx, uint8_t* base) {
  if (++g_swaps == 1) RunStamp();  // taken at the first frame
  // Every capture (F9 included, with or without a script) gets the UI dump of the first frame it
  // records.
  // A capture starts at the swap after the request and records the next frame; here, before this
  // swap's capture work, it is still armed at the end of that frame: the dump waits until then (an
  // F9 from the host thread is seen one swap before its capture starts).
  static uint64_t captures_seen = 0;
  static uint64_t pending_since = 0;  // swaps; 0: no dump pending
  const capture::Session& session = capture::Session::Get();
  if (const uint64_t captures = session.capture_requests(); captures != captures_seen) {
    captures_seen = captures;
    pending_since = g_swaps;
  }
  if (pending_since && session.armed()) {
    pending_since = 0;
    const uint32_t ui = abi::ReadU32(base, ui_abi::kGameUiGlobal);
    if (InGuestHeap(ui)) {
      // The capture's own swap number (its file's "swapK"): it started at the previous swap.
      WriteDump(ctx, base, ui, "ui-capture-" + RunStamp() + "-swap" + std::to_string(g_swaps - 1));
    }
  } else if (pending_since && g_swaps - pending_since > 4) {
    pending_since = 0;
    REXLOG_ERROR("dev input script: capture {} never started; no UI dump for it", captures_seen);
  }
  if (!g_runner) return;
  if (g_watch_target) WatchInteractTarget(base);
  bool replaying = false;
  if (const auto trace = g_runner->TakeTraceFrame(replaying)) g_hero_trace.Write(base, *trace);
  if (!replaying) g_hero_trace.Close();
  const uint32_t game_ui = abi::ReadU32(base, ui_abi::kGameUiGlobal);
  if (!InGuestHeap(game_ui)) {
    while (auto name = g_runner->TakeDump()) {
      REXLOG_ERROR("dev input script: dump_ui {}: no game UI yet", *name);
      g_runner->OnDumpDone(false);
    }
    return;
  }
  if (g_runner->ReadsUi()) {
    std::vector<std::string> focused, shown;
    for (const UiMenu& m : OpenMenus(base, Sheet(base, game_ui))) {
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
  while (auto name = g_runner->TakeDump()) g_runner->OnDumpDone(WriteDump(ctx, base, game_ui, *name));
}

void OnPadStateRead(const uint8_t* base, uint32_t user, uint32_t state, uint32_t result) {
  namespace xi = abi::xinput;
  if (!g_watch_stick || !state || result != xi::kSuccess) return;
  g_xam_lx = int16_t(abi::ReadU16(base, state + xi::kStateLeftX));
  g_xam_ly = int16_t(abi::ReadU16(base, state + xi::kStateLeftY));
  g_xam_user = user;
  ++g_xam_reads;
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
  g_script_folder = std::filesystem::path(path).parent_path();
  const ScriptFileReader read = [](std::string_view name) -> std::optional<std::string> {
    std::ifstream in(ScriptPath(name), std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream content;
    content << in.rdbuf();
    return content.str();
  };
  std::string error;
  auto script = InputScript::Parse(text.str(), error, read);
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
// The stick getter (guest_abi hero_motion::kReadStick): only while a replay writes its trace, the
// vector it gave the hero's input handler, for the trace's side file. It does not change what the
// game reads. Development builds only.
REX_EXTERN(__imp__sub_82191018);
REX_FUNC(sub_82191018) {
  const uint32_t out = ctx.r3.u32;
  const uint32_t from = static_cast<uint32_t>(ctx.lr);
  __imp__sub_82191018(ctx, base);
  namespace hm = torchlight::guest_abi::hero_motion;
  if (!torchlight::dev::g_watch_stick || from != hm::kHeroInputReadStickReturn) return;
  torchlight::dev::g_stick_x = torchlight::guest_abi::ReadF32(base, out);
  torchlight::dev::g_stick_y = torchlight::guest_abi::ReadF32(base, out + 4);
  ++torchlight::dev::g_stick_reads;
}

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
