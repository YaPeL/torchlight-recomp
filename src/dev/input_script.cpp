#include "dev/input_script.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace torchlight::dev {

namespace {

std::vector<std::string_view> Words(std::string_view line) {
  std::vector<std::string_view> words;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    size_t j = i;
    while (j < line.size() && line[j] != ' ' && line[j] != '\t') ++j;
    if (j > i) words.push_back(line.substr(i, j - i));
    i = j;
  }
  return words;
}

std::optional<double> Number(std::string_view text) {
  // std::from_chars for double is not everywhere yet: strtod on a copy.
  std::string copy(text);
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end != copy.c_str() + copy.size() || !std::isfinite(value)) return std::nullopt;
  return value;
}

struct Button {
  std::string_view name;
  uint16_t bit;
  bool left_trigger, right_trigger;
};
constexpr Button kButtons[] = {
    {"A", pad::kA, false, false},          {"B", pad::kB, false, false},
    {"X", pad::kX, false, false},          {"Y", pad::kY, false, false},
    {"START", pad::kStart, false, false},  {"BACK", pad::kBack, false, false},
    {"LB", pad::kLeftShoulder, false, false}, {"RB", pad::kRightShoulder, false, false},
    {"LT", 0, true, false},                {"RT", 0, false, true},
    {"UP", pad::kUp, false, false},        {"DOWN", pad::kDown, false, false},
    {"LEFT", pad::kLeft, false, false},    {"RIGHT", pad::kRight, false, false},
    {"LS", pad::kLeftThumb, false, false}, {"RS", pad::kRightThumb, false, false},
};

bool AddButton(std::string_view word, PadState& pad) {
  for (const auto& b : kButtons) {
    if (word != b.name) continue;
    pad.buttons |= b.bit;
    if (b.left_trigger) pad.left_trigger = 255;
    if (b.right_trigger) pad.right_trigger = 255;
    return true;
  }
  return false;
}

int16_t Axis(double value) { return int16_t(std::lround(std::clamp(value, -1.0, 1.0) * 32767)); }

}  // namespace

std::optional<InputScript> InputScript::Parse(std::string_view text, std::string& error) {
  InputScript script;
  int number = 0;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    ++number;
    if (const size_t hash = line.find('#'); hash != std::string_view::npos) line = line.substr(0, hash);
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const auto w = Words(line);
    if (w.empty()) continue;
    auto fail = [&](std::string why) {
      error = "line " + std::to_string(number) + ": " + why;
      return std::nullopt;
    };
    auto duration = [&](std::string_view word, Duration& out) {
      Duration d;
      std::string_view digits = word;
      double scale = 1;
      if (digits.ends_with("ms")) {
        digits.remove_suffix(2);
        scale = 0.001;
      } else if (digits.ends_with("s")) {
        digits.remove_suffix(1);
      } else if (digits.ends_with("f")) {
        digits.remove_suffix(1);
        d.frames = true;
      }
      const auto value = Number(digits);
      if (!value || *value < 0) return false;
      d.value = *value * scale;
      out = d;
      return true;
    };
    Command c;
    c.line = number;
    const std::string_view verb = w[0];
    size_t i = 1;
    // Optional "timeout DUR" at w[i].
    auto timeout = [&]() -> bool {
      if (i == w.size()) return true;
      if (w[i] != "timeout" || i + 2 != w.size()) return false;
      return duration(w[i + 1], c.timeout);
    };
    if (verb == "press" || verb == "hold") {
      while (i < w.size() && AddButton(w[i], c.pad)) ++i;
      if (i == 1) return fail("a button is needed (A B X Y START BACK LB RB LT RT UP DOWN LEFT RIGHT LS RS)");
      if (verb == "hold") {
        if (i + 2 != w.size() || w[i] != "for" || !duration(w[i + 1], c.duration))
          return fail("hold BUTTON... for DURATION");
        c.op = Op::kHold;
      } else if (i == w.size()) {
        c.op = Op::kPress;
      } else if (w[i] == "frames") {
        const auto n = i + 2 == w.size() ? Number(w[i + 1]) : std::nullopt;
        if (!n || *n < 1) return fail("press BUTTON... frames N");
        c.press_frames = uint32_t(*n);
        c.op = Op::kPress;
      } else if (w[i] == "every" && i + 4 == w.size() && w[i + 2] == "for") {
        if (!duration(w[i + 1], c.every) || !duration(w[i + 3], c.duration))
          return fail("press BUTTON... every DURATION for DURATION");
        c.op = Op::kPressFor;
      } else if (w[i] == "every") {
        if (i + 4 > w.size() || !duration(w[i + 1], c.every) || w[i + 2] != "until")
          return fail("press BUTTON... every DURATION until EVENT [timeout DURATION] | "
                      "every DURATION for DURATION");
        i += 3;
        if (w[i] == "level_loaded") {
          c.event = Event::kLevelLoaded;
          ++i;
        } else if (w[i] == "menu" && i + 1 < w.size()) {
          c.event = Event::kMenu;
          c.text = std::string(w[i + 1]);
          i += 2;
        } else {
          return fail("unknown event (level_loaded, menu NAME)");
        }
        if (!timeout()) return fail("expected timeout DURATION");
        c.op = Op::kPressUntil;
      } else {
        return fail("unknown button or option '" + std::string(w[i]) + "'");
      }
    } else if (verb == "stick") {
      if (w.size() != 6 || (w[1] != "left" && w[1] != "right") || w[4] != "for")
        return fail("stick left|right X Y for DURATION");
      const auto x = Number(w[2]), y = Number(w[3]);
      if (!x || !y || std::abs(*x) > 1 || std::abs(*y) > 1) return fail("X and Y go from -1 to 1");
      if (!duration(w[5], c.duration)) return fail("bad duration");
      (w[1] == "left" ? c.pad.left_x : c.pad.right_x) = Axis(*x);
      (w[1] == "left" ? c.pad.left_y : c.pad.right_y) = Axis(*y);
      c.op = Op::kStick;
    } else if (verb == "idle") {
      if (w.size() != 2 || !duration(w[1], c.duration)) return fail("idle DURATION");
      c.op = Op::kIdle;
    } else if (verb == "wait") {
      if (i < w.size() && w[i] == "level_loaded") {
        c.event = Event::kLevelLoaded;
        ++i;
      } else if (i + 1 < w.size() && w[i] == "menu") {
        c.event = Event::kMenu;
        c.text = std::string(w[i + 1]);
        i += 2;
      } else {
        return fail("wait level_loaded | wait menu NAME [timeout DURATION]");
      }
      if (!timeout()) return fail("expected timeout DURATION");
      c.op = Op::kWait;
    } else if (verb == "mark" || verb == "command") {
      if (w.size() < 2) return fail(std::string(verb) + " TEXT");
      const size_t from = line.find(w[1]);
      std::string_view rest = line.substr(from);
      while (!rest.empty() && (rest.back() == ' ' || rest.back() == '\t')) rest.remove_suffix(1);
      c.text = std::string(rest);
      c.op = verb == "mark" ? Op::kMark : Op::kCommand;
    } else if (verb == "capture" || verb == "quit") {
      if (w.size() != 1) return fail(std::string(verb) + " takes nothing");
      c.op = verb == "capture" ? Op::kCapture : Op::kQuit;
    } else {
      return fail("unknown command '" + std::string(verb) + "'");
    }
    script.commands_.push_back(std::move(c));
  }
  if (script.commands_.empty()) {
    error = "the script has no commands";
    return std::nullopt;
  }
  return script;
}

bool InputScript::Elapsed(const Duration& d, const ScriptEvents& e, const Start& since) const {
  return d.frames ? double(e.frame - since.frame) >= d.value : e.seconds - since.seconds >= d.value;
}

bool InputScript::EventSeen(const Command& c, const ScriptEvents& e) const {
  if (c.event == Event::kLevelLoaded) return e.level_loads > start_.level_loads;
  for (size_t i = start_.menus; i < e.menus_opened.size(); ++i) {
    if (e.menus_opened[i].find(c.text) != std::string::npos) return true;
  }
  return false;
}

void InputScript::Begin(const ScriptEvents& e) {
  start_ = {e.frame, e.seconds, e.level_loads, e.menus_opened.size()};
  press_ = start_;
  started_ = true;
}

PadState InputScript::Step(const ScriptEvents& e, std::vector<ScriptAction>& actions) {
  while (next_ < commands_.size()) {
    const Command& c = commands_[next_];
    if (!started_) Begin(e);
    auto advance = [&] {
      ++next_;
      started_ = false;
    };
    const uint64_t frames = e.frame - start_.frame;
    switch (c.op) {
      case Op::kMark:
        actions.push_back({ScriptAction::Kind::kMark, c.text});
        advance();
        continue;
      case Op::kCapture:
        actions.push_back({ScriptAction::Kind::kCapture, {}});
        advance();
        continue;
      case Op::kCommand:
        actions.push_back({ScriptAction::Kind::kCommand, c.text});
        advance();
        continue;
      case Op::kQuit:
        actions.push_back({ScriptAction::Kind::kQuit, {}});
        advance();
        continue;
      case Op::kPress:
        if (frames < 2 * uint64_t(c.press_frames)) {
          return frames < c.press_frames ? c.pad : PadState{};
        }
        advance();
        continue;
      case Op::kHold:
      case Op::kStick:
        if (!Elapsed(c.duration, e, start_)) return c.pad;
        advance();
        continue;
      case Op::kIdle:
        if (!Elapsed(c.duration, e, start_)) return {};
        advance();
        continue;
      case Op::kPressFor:
        if (Elapsed(c.duration, e, start_)) {
          advance();
          continue;
        }
        if (Elapsed(c.every, e, press_)) press_ = {e.frame, e.seconds, 0, 0};
        return e.frame - press_.frame < c.press_frames ? c.pad : PadState{};
      case Op::kWait:
      case Op::kPressUntil:
        if (EventSeen(c, e)) {
          advance();
          continue;
        }
        if (Elapsed(c.timeout, e, start_)) {
          actions.push_back({ScriptAction::Kind::kFailed,
                             "line " + std::to_string(c.line) + ": timed out waiting for " +
                                 (c.event == Event::kLevelLoaded ? std::string("a level load")
                                                                 : "menu " + c.text)});
          next_ = commands_.size();
          ended_ = true;
          return {};
        }
        if (c.op == Op::kWait) return {};
        // A press now and then: down for press_frames, up as long, again when `every` elapsed.
        if (Elapsed(c.every, e, press_)) press_ = {e.frame, e.seconds, 0, 0};
        return e.frame - press_.frame < c.press_frames ? c.pad : PadState{};
    }
  }
  if (!ended_) {
    actions.push_back({ScriptAction::Kind::kFinished, {}});
    ended_ = true;
  }
  return {};
}

std::vector<PadKeystroke> PadKeystrokes(const PadState& before, const PadState& after) {
  static constexpr std::pair<uint16_t, uint16_t> kKeys[] = {
      {pad::kA, 0x5800},         {pad::kB, 0x5801},           {pad::kX, 0x5802},
      {pad::kY, 0x5803},         {pad::kRightShoulder, 0x5804}, {pad::kLeftShoulder, 0x5805},
      {pad::kUp, 0x5810},        {pad::kDown, 0x5811},        {pad::kLeft, 0x5812},
      {pad::kRight, 0x5813},     {pad::kStart, 0x5814},       {pad::kBack, 0x5815},
      {pad::kLeftThumb, 0x5816}, {pad::kRightThumb, 0x5817},
  };
  std::vector<PadKeystroke> keys;
  for (const auto& [bit, vk] : kKeys) {
    const bool was = before.buttons & bit, is = after.buttons & bit;
    if (was != is) keys.push_back({vk, is});
  }
  const auto trigger = [&](uint8_t was, uint8_t is, uint16_t vk) {
    if ((was > 0) != (is > 0)) keys.push_back({vk, is > 0});
  };
  trigger(before.left_trigger, after.left_trigger, 0x5806);
  trigger(before.right_trigger, after.right_trigger, 0x5807);
  return keys;
}

}  // namespace torchlight::dev
