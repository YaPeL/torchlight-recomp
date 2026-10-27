// Development only (TORCHLIGHT_DEV_INPUT): a recorded controller played back by time, and the hero's
// per-frame trace written while it plays, for comparing how the hero moves under the same stick on
// this build and on another (the input script's "replay", input_script.h; docs/dev-input-script.md).
// Pure: the files' text in and out; the game reads are in the app (script_input.cpp).
//
// The recording ("x360ui pad replay v1"): a first line `# x360ui pad replay v1`, an optional
// `# start_ms N` (the time held at rest before t=0), other lines starting with '#' ignored, then one
// sample per line, `t_ms lx ly lt rt buttons`: t_ms from t=0 (never decreasing), the left stick as
// XInput's int16 (+y up), the triggers 0-255 and the XINPUT_GAMEPAD button mask, in decimal. A
// sample holds until the next one's t_ms; the last one holds to the end. Lines may end in CRLF.
//
// The trace ("x360ui pad trace v1"): a first line `# x360ui pad trace v1 recomp`, the column names,
// then one line per guest frame, integers only:
// `t_us,frame,x_mu,z_mu,fx_mu,fz_mu,scale_mu,speed_mu,lx,ly`, the "_mu" values x1000 and rounded.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "input/pad_state.h"

namespace torchlight::dev {

struct PadReplay {
  struct Sample {
    int64_t t_ms = 0;
    input::PadState pad;
  };
  int64_t start_ms = 0;
  std::vector<Sample> samples;  // at least one, t_ms never decreasing

  // Nullopt with `error` ("line N: ...") when the text is not a recording.
  static std::optional<PadReplay> Parse(std::string_view text, std::string& error);
  // The controller at `t_ms` since t=0: the last sample at or before it (at rest before the first).
  input::PadState At(double t_ms) const;
  int64_t last_ms() const { return samples.back().t_ms; }
};

// The trace runs this long after the recording's last sample (the hero's slide after the release).
inline constexpr int64_t kTraceTailMs = 2000;

struct HeroTraceRow {
  int64_t t_us = 0;
  uint64_t frame = 0;
  float x = 0, z = 0;    // world position
  float fx = 0, fz = 0;  // facing: the world transform's third column, rows 0 and 2
  float scale = 0, speed = 0;
  int16_t lx = 0, ly = 0;  // the left stick fed to the game this frame
};

// The first two lines.
std::string HeroTraceHeader();
// One frame's line, with its '\n'.
std::string HeroTraceLine(const HeroTraceRow& row);

}  // namespace torchlight::dev
