// What the backend holds of the shader constants sent so far, to send only what changed.
//
// The frontend (frontend/frontend.cpp, SetConstants) keeps one array of float constants per
// program stage, indexed by the guest's physical index: each SetConstants overwrites the values of
// its ranges and replaces the stage's auto constants and transpose flag. Blocks (guest
// GpuProgramParameters) share that array, so a range can be left out only when the array already
// holds its values, whoever wrote them; that is what this mirrors. Live commands only: a capture
// replays from its first frame and needs every range (Session decides).
//
// When the frontend's arrays start over, this has to start over too (Reset). Checked (2026-10-07):
// the live consumer creates its Frontend once, when its thread starts (live/live_mode.cpp), and
// only SetConstants writes the arrays, so they start over only with a new live session, which
// Session::EnableLive resets this for. Render target changes, recreating the device or the
// context, the render scale or window, reloading programs (the arrays are per stage, not per
// program) leave them alone; a dropped frame keeps its SetConstants (LiveFrame::AbsorbDropped) and
// the consumer executes every command of every frame it takes (live/frame_step.cpp); while an F9
// capture records every range is sent and still stored here; a session recording starts with the
// live session and its replay with a new frontend. Anything that ever recreates or clears the
// frontend mid-session must reset this at the same point of the command stream.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "commands/types.h"

namespace torchlight::capture {

class ConstantMirror {
 public:
  // Whether the stage's array already holds `count` values at `physical` (host order).
  bool Holds(bool floats, uint8_t stage, uint32_t physical, const uint32_t* values,
             uint32_t count) const;
  // The stage's array after sending those values.
  void Store(bool floats, uint8_t stage, uint32_t physical, const uint32_t* values,
             uint32_t count);
  // Whether a command with these auto constants and transpose flag would leave the stage's as
  // they are (the frontend replaces them with every command).
  bool SameTail(uint8_t stage, const std::vector<commands::AutoConstant>& autos,
                const std::optional<bool>& transpose) const;
  void StoreTail(uint8_t stage, const std::vector<commands::AutoConstant>& autos,
                 const std::optional<bool>& transpose);
  // Nothing held: the frontend starts empty (a new live session).
  void Reset();

 private:
  struct Array {
    std::vector<uint32_t> values;
    std::vector<uint8_t> known;
  };
  struct Stage {
    std::array<Array, 2> arrays;  // ints, floats
    bool tail_known = false;
    std::vector<commands::AutoConstant> autos;
    std::optional<bool> transpose;
  };
  static constexpr size_t kStages = 4;
  std::array<Stage, kStages> stages_;
};

}  // namespace torchlight::capture
