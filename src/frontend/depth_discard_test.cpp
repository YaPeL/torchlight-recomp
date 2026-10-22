// PlanDepthDiscards (depth_discard.h): which target binds may discard the depth they leave.

#include <cstdio>
#include <optional>
#include <vector>

#include "frontend/depth_discard.h"

namespace {

using namespace torchlight::commands;
using torchlight::frontend::PlanDepthDiscards;
using torchlight::frontend::TargetSize;

int failures = 0;

void Check(bool ok, const char* what) {
  if (ok) return;
  ++failures;
  std::fprintf(stderr, "FAIL: %s\n", what);
}

constexpr uint32_t kMain = 0x1000, kShadow = 0x2000, kLight = 0x3000;
constexpr TargetSize kMainSize{1280, 720};

Command Target(uint32_t address, uint32_t w, uint32_t h) {
  SetRenderTarget t;
  t.guest_address = address;
  t.width = w;
  t.height = h;
  return {0, t};
}
Command Viewport(uint32_t target, int32_t l, int32_t t, int32_t w, int32_t h) {
  SetViewport v;
  v.target_guest_address = target;
  v.left = l;
  v.top = t;
  v.width = w;
  v.height = h;
  return {0, v};
}
Command ClearOf(uint32_t buffers) {
  Clear c;
  c.buffers = buffers;
  return {0, c};
}
Command DrawCommand() { return {0, Draw{}}; }

std::vector<size_t> Plan(const std::vector<Command>& list,
                         std::optional<TargetSize> bound = kMainSize) {
  return PlanDepthDiscards(list, std::nullopt, bound);
}

}  // namespace

int main() {
  // A frame as the game draws it: shadow, light, then the main scene, each pass cleared first.
  // Leaving main (1280x720) for a 512x512 pass discards nothing: its clear does not cover main.
  // Shadow to light (same size): discarded. Light to main: discarded.
  {
    std::vector<Command> f = {
        Viewport(kShadow, 0, 0, 512, 512), Target(kShadow, 512, 512), ClearOf(3), DrawCommand(),
        Viewport(kLight, 0, 0, 512, 512),  Target(kLight, 512, 512),  ClearOf(3), DrawCommand(),
        Viewport(kMain, 0, 0, 1280, 720),  Target(kMain, 1280, 720),  ClearOf(3), DrawCommand()};
    const auto p = Plan(f);
    Check(p == std::vector<size_t>({5, 9}), "game frame: light and main binds, not the shadow's");
  }
  // The viewport set after the bind counts too.
  {
    std::vector<Command> f = {Target(kMain, 1280, 720), Viewport(kMain, 0, 0, 1280, 720),
                              ClearOf(2)};
    Check(Plan(f) == std::vector<size_t>({0}), "viewport after the bind");
  }
  // A clear of part of the target, a colour-only clear, a draw, a present or another bind before
  // the depth clear: nothing.
  {
    std::vector<Command> partial = {Viewport(kMain, 0, 0, 640, 720), Target(kMain, 1280, 720),
                                    ClearOf(3)};
    Check(Plan(partial).empty(), "partial viewport");
    std::vector<Command> colour = {Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720),
                                   ClearOf(1), DrawCommand(), ClearOf(2)};
    Check(Plan(colour).empty(), "colour-only clear, then a draw");
    std::vector<Command> drawn = {Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720),
                                  DrawCommand(), ClearOf(3)};
    Check(Plan(drawn).empty(), "a draw before the clear");
    std::vector<Command> presented = {Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720),
                                      Command{0, Present{}}, ClearOf(3)};
    Check(Plan(presented).empty(), "a present before the clear");
    std::vector<Command> rebound = {Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720),
                                    Target(kMain, 1280, 720), ClearOf(3)};
    Check(Plan(rebound) == std::vector<size_t>({2}), "only the bind the clear belongs to");
  }
  // A viewport of another target does not count as this pass's.
  {
    std::vector<Command> f = {Viewport(kShadow, 0, 0, 1280, 720), Target(kMain, 1280, 720),
                              ClearOf(3)};
    Check(Plan(f).empty(), "another target's viewport");
  }
  // The first bind of a list with no target known before it: nothing.
  {
    std::vector<Command> f = {Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720),
                              ClearOf(3)};
    Check(Plan(f, std::nullopt).empty(), "nothing known bound before");
  }
  // A list of several frames (dropped frames merged before the one drawn): the sizes follow the
  // binds across them.
  {
    std::vector<Command> f = {
        Viewport(kLight, 0, 0, 512, 512), Target(kLight, 512, 512), ClearOf(3), Command{0, Present{}},
        Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720), ClearOf(3)};
    Check(Plan(f) == std::vector<size_t>({5}), "merged frames");
  }
  // A clear with the scissor on covers only its rectangle: not whole.
  {
    SetScissor on;
    on.enabled = true;
    on.right = 64;
    on.bottom = 64;
    SetScissor off;
    std::vector<Command> f = {Viewport(kMain, 0, 0, 1280, 720), Command{0, on},
                              Target(kMain, 1280, 720), ClearOf(3)};
    Check(Plan(f).empty(), "scissored clear");
    f = {Viewport(kMain, 0, 0, 1280, 720), Command{0, on}, Command{0, off},
         Target(kMain, 1280, 720), ClearOf(3)};
    Check(Plan(f) == std::vector<size_t>({3}), "scissor turned off before the clear");
    // Left on by the previous list.
    f = {Viewport(kMain, 0, 0, 1280, 720), Target(kMain, 1280, 720), ClearOf(3)};
    Check(PlanDepthDiscards(f, std::nullopt, kMainSize, true).empty(), "scissor on from before");
  }
  if (failures == 0) std::printf("depth_discard_test: ok\n");
  return failures == 0 ? 0 : 1;
}
