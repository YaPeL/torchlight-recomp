// The GUI redraw filter's rule (--native_gui_redraw_filter, gui_redraw_filter_hooks.cpp): whether a
// redraw CEGUI asked for (the System's flag, guest_abi cegui kSystemGuiRedraw) would queue the
// same quads the renderer already holds, so that System::renderGUI can skip the rebuild and draw
// the list it has.
//
// The rebuild walks the windows Window::render draws (the active sheet and, in draw list order,
// every visible descendant), and each of them queues its render cache at its unclipped rect's
// position, clipped by its clipped rect, with the renderer's z that steps down once per window.
// The quads therefore depend on: the sequence of drawn windows; each one's render cache (filled
// again only when its d_needsRedraw is set); its two rect caches (when the cache holds anything:
// drawSelf reads them only then); and the renderer's display area and texel offset (addQuad). The
// rule collects these as the last rebuild left them and rebuilds when any differs, when a drawn
// window asks to be filled again or has a stale rect cache (moved or resized: the cache's flag is
// cleared and the getter would compute it again), when a window's render slots are not CEGUI's
// own, or when the tree cannot be walked within bounds. Otherwise the rebuild would queue the same
// quads.
//
// The idea (rebuild only when a drawn window changed) is torchlight-nx's mode 3, whose method of
// validation (mode 2: rebuild always and compare the quad lists byte for byte) the shadow mode
// follows: torchlight-nx 1.0.5 (https://github.com/liartes/torchlight-nx, commit 27b6b20,
// src/torchlight_ogre_natif.cpp; GPL-3.0). No code of theirs is used: they hook Window::onMoved to
// see a drawn window move, this rule compares the rect caches the draw reads.
//
// Pure over a reader of guest memory (u8(address), u32(address)); tested by
// gui_redraw_filter_test.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "guest_abi/cegui.h"

namespace torchlight::hooks::gui_redraw {

// Why the rule rebuilds (kSkip: it does not).
enum class Reason : uint8_t {
  kSkip,
  kNoSnapshot,    // no rebuild seen yet, or the last one's state could not be collected
  kNeedsRedraw,   // a drawn window's render cache is to be filled again
  kRectStale,     // a drawn window with content has a rect cache to compute again
  kOverridden,    // a drawn window's render slots are not CEGUI's own
  kUnwalkable,    // too deep, too many windows, or a child count out of bounds
  kChanged,       // the drawn windows, their caches or rects, or the display differ
  kCount,
};

inline const char* ReasonName(Reason r) {
  switch (r) {
    case Reason::kSkip: return "skip";
    case Reason::kNoSnapshot: return "no snapshot";
    case Reason::kNeedsRedraw: return "needs redraw";
    case Reason::kRectStale: return "rect stale";
    case Reason::kOverridden: return "overridden";
    case Reason::kUnwalkable: return "unwalkable";
    case Reason::kChanged: return "changed";
    default: return "?";
  }
}

// One drawn window as the draw reads it: its render cache's two vectors (begin and end of each)
// and, when the cache holds anything, its unclipped and clipped rects (the floats' bits).
struct Drawn {
  uint32_t window = 0;
  std::array<uint32_t, 4> cache{};
  std::array<uint32_t, 8> rects{};
  bool operator==(const Drawn&) const = default;
};

struct State {
  uint32_t renderer = 0;
  uint32_t sheet = 0;
  std::array<uint32_t, 6> display{};  // the display area (4 floats) and texel offset (2)
  std::vector<Drawn> drawn;
  bool operator==(const State&) const = default;
};

// Bounds of the walk: the game's layouts are far below them.
inline constexpr int kMaxDepth = 64;
inline constexpr size_t kMaxDrawn = 8192;
inline constexpr uint32_t kMaxChildren = 4096;

namespace detail {

namespace ui = guest_abi::cegui;

// Window::isVisible(false): the window's flag and its parents'.
template <typename Reader>
bool Visible(const Reader& r, uint32_t w) {
  for (int depth = 0; w != 0; ++depth) {
    if (depth >= kMaxDepth) return false;
    if (r.u8(w + ui::kWindowVisible.offset) == 0) return false;
    w = r.u32(w + ui::kWindowParent.offset);
  }
  return true;
}

template <typename Reader>
Reason Window(const Reader& r, uint32_t w, Drawn& out) {
  const uint32_t vt = r.u32(w);
  const uint32_t draw_self = r.u32(vt + ui::kVtDrawSelf);
  if (r.u32(vt + ui::kVtOnRenderingStarted) != ui::kOnRenderingStarted ||
      r.u32(vt + ui::kVtOnRenderingEnded) != ui::kOnRenderingEnded ||
      (draw_self != ui::kDrawSelf && draw_self != ui::kDrawSelfEmpty)) {
    return Reason::kOverridden;
  }
  out.window = w;
  if (draw_self == ui::kDrawSelfEmpty) return Reason::kSkip;  // draws nothing
  if (r.u8(w + ui::kWindowNeedsRedraw.offset) != 0) return Reason::kNeedsRedraw;
  out.cache = {r.u32(w + ui::kWindowCacheImagesBegin.offset),
               r.u32(w + ui::kWindowCacheImagesEnd.offset),
               r.u32(w + ui::kWindowCacheTextsBegin.offset),
               r.u32(w + ui::kWindowCacheTextsEnd.offset)};
  if (out.cache[0] == out.cache[1] && out.cache[2] == out.cache[3]) return Reason::kSkip;
  if (r.u8(w + ui::kWindowUnclippedRectValid.offset) == 0 ||
      r.u8(w + ui::kWindowClippedRectValid.offset) == 0) {
    return Reason::kRectStale;
  }
  for (uint32_t i = 0; i < 4; ++i) {
    out.rects[i] = r.u32(w + ui::kWindowUnclippedRect.offset + 4 * i);
    out.rects[4 + i] = r.u32(w + ui::kWindowClippedRect.offset + 4 * i);
  }
  return Reason::kSkip;
}

}  // namespace detail

// What the next rebuild would read, from the System at `system`, into `out`; kSkip when it could
// all be collected, else why the rebuild cannot be skipped (`out` is then partial).
template <typename Reader>
Reason Collect(const Reader& r, uint32_t system, State& out) {
  namespace ui = guest_abi::cegui;
  out.drawn.clear();
  out.renderer = r.u32(system + ui::kSystemRenderer.offset);
  out.sheet = r.u32(system + ui::kSystemActiveSheet.offset);
  for (uint32_t i = 0; i < 4; ++i)
    out.display[i] = r.u32(out.renderer + ui::kRendererDisplayArea.offset + 4 * i);
  for (uint32_t i = 0; i < 2; ++i)
    out.display[4 + i] = r.u32(out.renderer + ui::kRendererTexelOffset.offset + 4 * i);
  if (out.sheet == 0 || !detail::Visible(r, out.sheet)) return Reason::kSkip;  // nothing drawn

  // Window::render's recursion, in its order: a window, then its draw list's visible windows.
  struct Frame {
    uint32_t window;
    uint32_t next, count;
  };
  std::array<Frame, kMaxDepth> stack;
  int depth = 0;
  auto enter = [&](uint32_t w) -> Reason {
    if (depth >= kMaxDepth || out.drawn.size() >= kMaxDrawn) return Reason::kUnwalkable;
    Drawn d;
    if (Reason why = detail::Window(r, w, d); why != Reason::kSkip) return why;
    out.drawn.push_back(d);
    const int32_t bytes = int32_t(r.u32(w + ui::kWindowChildrenEnd.offset) -
                                  r.u32(w + ui::kWindowChildrenBegin.offset));
    if (bytes < 0 || uint32_t(bytes) / 4 > kMaxChildren) return Reason::kUnwalkable;
    stack[depth++] = {w, 0, uint32_t(bytes) >> 2};
    return Reason::kSkip;
  };
  if (Reason why = enter(out.sheet); why != Reason::kSkip) return why;
  while (depth > 0) {
    Frame& f = stack[depth - 1];
    if (f.next == f.count) {
      --depth;
      continue;
    }
    const uint32_t child =
        r.u32(r.u32(f.window + ui::kWindowDrawListBegin.offset) + 4 * f.next++);
    if (child == 0) return Reason::kUnwalkable;
    if (!detail::Visible(r, child)) continue;
    if (Reason why = enter(child); why != Reason::kSkip) return why;
  }
  return Reason::kSkip;
}

// The rule: `collected` is Collect's result for `now`; `last` the state after the last rebuild
// (null when there is none to compare with).
inline Reason Decide(const State* last, const State& now, Reason collected) {
  if (collected != Reason::kSkip) return collected;
  if (last == nullptr) return Reason::kNoSnapshot;
  return now == *last ? Reason::kSkip : Reason::kChanged;
}

}  // namespace torchlight::hooks::gui_redraw
