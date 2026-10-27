#include "dev/pad_replay.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace torchlight::dev {

namespace {

constexpr std::string_view kReplayHeader = "# x360ui pad replay v1";
constexpr std::string_view kStartMs = "# start_ms ";

std::string_view Trimmed(std::string_view line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.remove_suffix(1);
  while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
  return line;
}

// Whitespace-separated integers, all of the line; nullopt when anything else is in it.
std::optional<std::vector<int64_t>> Integers(std::string_view line) {
  std::vector<int64_t> values;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i == line.size()) break;
    int64_t value = 0;
    const auto [end, ec] = std::from_chars(line.data() + i, line.data() + line.size(), value);
    if (ec != std::errc() || (end != line.data() + line.size() && *end != ' ' && *end != '\t')) return std::nullopt;
    values.push_back(value);
    i = size_t(end - line.data());
  }
  return values;
}

int64_t Micro(float value) { return std::llround(double(value) * 1000.0); }

}  // namespace

std::optional<PadReplay> PadReplay::Parse(std::string_view text, std::string& error) {
  PadReplay replay;
  bool header = false;
  int number = 0;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    const std::string_view line = Trimmed(text.substr(start, end - start));
    start = end + 1;
    ++number;
    auto fail = [&](std::string why) {
      error = "line " + std::to_string(number) + ": " + why;
      return std::nullopt;
    };
    if (line.empty()) continue;
    if (!header) {
      if (line != kReplayHeader) return fail("expected \"" + std::string(kReplayHeader) + "\"");
      header = true;
      continue;
    }
    if (line.starts_with(kStartMs)) {
      const auto value = Integers(line.substr(kStartMs.size()));
      if (!value || value->size() != 1 || (*value)[0] < 0) return fail("start_ms takes one count of ms, 0 or more");
      replay.start_ms = (*value)[0];
      continue;
    }
    if (line.front() == '#') continue;
    const auto v = Integers(line);
    if (!v || v->size() != 6) return fail("expected t_ms lx ly lt rt buttons, integers");
    const int64_t t = (*v)[0], lx = (*v)[1], ly = (*v)[2], lt = (*v)[3], rt = (*v)[4], buttons = (*v)[5];
    if (t < 0) return fail("t_ms below 0");
    if (!replay.samples.empty() && t < replay.samples.back().t_ms) return fail("t_ms goes back");
    if (lx < -32768 || lx > 32767 || ly < -32768 || ly > 32767) return fail("lx and ly go from -32768 to 32767");
    if (lt < 0 || lt > 255 || rt < 0 || rt > 255) return fail("lt and rt go from 0 to 255");
    if (buttons < 0 || buttons > 0xFFFF) return fail("buttons is a 16-bit mask");
    Sample s;
    s.t_ms = t;
    s.pad.left_x = int16_t(lx);
    s.pad.left_y = int16_t(ly);
    s.pad.left_trigger = uint8_t(lt);
    s.pad.right_trigger = uint8_t(rt);
    s.pad.buttons = uint16_t(buttons);
    replay.samples.push_back(s);
  }
  if (!header) {
    error = "empty: expected \"" + std::string(kReplayHeader) + "\"";
    return std::nullopt;
  }
  if (replay.samples.empty()) {
    error = "the recording has no samples";
    return std::nullopt;
  }
  return replay;
}

input::PadState PadReplay::At(double t_ms) const {
  // The first sample after t_ms; the one before it holds.
  const auto after = std::upper_bound(samples.begin(), samples.end(), t_ms,
                                      [](double t, const Sample& s) { return t < double(s.t_ms); });
  return after == samples.begin() ? input::PadState{} : std::prev(after)->pad;
}

std::string HeroTraceHeader() {
  return "# x360ui pad trace v1 recomp\nt_us,frame,x_mu,z_mu,fx_mu,fz_mu,scale_mu,speed_mu,lx,ly\n";
}

std::string HeroTraceLine(const HeroTraceRow& r) {
  return std::format("{},{},{},{},{},{},{},{},{},{}\n", r.t_us, r.frame, Micro(r.x), Micro(r.z), Micro(r.fx),
                     Micro(r.fz), Micro(r.scale), Micro(r.speed), r.lx, r.ly);
}

}  // namespace torchlight::dev
