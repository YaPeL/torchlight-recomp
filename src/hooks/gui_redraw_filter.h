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
// window asks to be filled again, when a window's render slots are not CEGUI's own, or when the
// tree cannot be walked within bounds. Otherwise the rebuild would queue the same quads.
//
// A stale rect cache (its flag cleared: Window::setArea_impl clears all four on every call, moved
// or not, and some HUD windows are set every frame) is computed by the reader's refresh(window),
// which in the game calls the same two getters drawSelf calls (they fill the caches as the rebuild
// would at that point of the frame), and then compared like any other; a reader that cannot
// refresh makes it a rebuild.
//
// What the rule assumes. The render cache holds pointers the draw follows on every rebuild: each
// image's CEGUI Image (Image::draw reads its area and offset, scaled by its Imageset), each text's
// Font (its glyphs' Images), and through them the CEGUI texture (addQuad reads its +29) and the
// Ogre texture it shares. A change of a texture's content (pixels written in place, a render
// target drawn again) is drawn the same without a rebuild: the quads only reference the texture.
// What would need a rebuild is a change of the quads themselves (their UVs, geometry or colours)
// coming from those objects without the game asking for a redraw: an Image's area or offset, an
// Imageset's native resolution or auto-scaling, a Font's glyph layout re-packed. The rule does not
// compare them; the game changes them together with a redraw request (a resolution change goes
// through the display area, which is compared), and the shadow mode compares the queued quads byte
// for byte (UVs, positions, colours, texture) against the kept list, which found no such change in
// any scene (gui_redraw_filter_hooks.cpp). torchlight-nx's mode 3 makes the same assumption. A
// font's glyphs are rasterised on demand, a 256-code-point page at a time: Font::getGlyphData
// (sub_821B3C88) tests the page in the loaded-pages bitmap (font +720), sets it and calls the
// font's slot 3 (rasterise, `ori r5,r4,255` then `lwz r11,12(r6)`), which in CEGUI 0.6 adds
// imagesets for the new page and leaves the glyphs already drawn where they are; new code points
// come with new text, which is a redraw.
//
// The idea (rebuild only when a drawn window changed) is torchlight-nx's mode 3, whose method of
// validation (mode 2: rebuild always and compare the quad lists byte for byte) the shadow mode
// follows: torchlight-nx 1.0.5 (https://github.com/liartes/torchlight-nx, commit 27b6b20,
// src/torchlight_ogre_natif.cpp; GPL-3.0). No code of theirs is used: they hook Window::onMoved to
// see a drawn window move, this rule compares the rect caches the draw reads.
//
// Pure over a reader of guest memory (u8(address), u32(address), refresh(window): true when it
// computed the window's two rect caches); tested by gui_redraw_filter_test.

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
  kRectStale,     // a drawn window with content has a rect cache that could not be computed
  kOverridden,    // a drawn window's render slots are not CEGUI's own
  kUnwalkable,    // too deep (a parent chain or the walk), too many windows, a child count out of
                  // bounds, or a null child
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
  uint32_t quads = 0;                  // the quad list's size
  std::vector<Drawn> drawn;
  bool operator==(const State&) const = default;
};

// Bounds of the walk: the game's layouts are far below them.
inline constexpr int kMaxDepth = 64;
inline constexpr size_t kMaxDrawn = 8192;
inline constexpr uint32_t kMaxChildren = 4096;

namespace detail {

namespace ui = guest_abi::cegui;

// Window::isVisible(false): the window's flag and its parents'. A parent chain longer than the
// walk's bound (or a cycle) is not decided: the rule then rebuilds.
enum class Visibility { kHidden, kVisible, kTooDeep };
template <typename Reader>
Visibility Visible(const Reader& r, uint32_t w) {
  for (int depth = 0; w != 0; ++depth) {
    if (depth >= kMaxDepth) return Visibility::kTooDeep;
    if (r.u8(w + ui::kWindowVisible.offset) == 0) return Visibility::kHidden;
    w = r.u32(w + ui::kWindowParent.offset);
  }
  return Visibility::kVisible;
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
  auto valid = [&] {
    return r.u8(w + ui::kWindowUnclippedRectValid.offset) != 0 &&
           r.u8(w + ui::kWindowClippedRectValid.offset) != 0;
  };
  if (!valid() && (!r.refresh(w) || !valid())) return Reason::kRectStale;
  for (uint32_t i = 0; i < 4; ++i) {
    out.rects[i] = r.u32(w + ui::kWindowUnclippedRect.offset + 4 * i);
    out.rects[4 + i] = r.u32(w + ui::kWindowClippedRect.offset + 4 * i);
  }
  return Reason::kSkip;
}

}  // namespace detail

// What the next rebuild would read, from the System at `system`, into `out`; kSkip when it could
// all be collected, else why the rebuild cannot be skipped (`out` is then partial, and `culprit`,
// if given, the window that stopped it).
template <typename Reader>
Reason Collect(const Reader& r, uint32_t system, State& out, uint32_t* culprit = nullptr) {
  namespace ui = guest_abi::cegui;
  out.drawn.clear();
  out.renderer = r.u32(system + ui::kSystemRenderer.offset);
  out.sheet = r.u32(system + ui::kSystemActiveSheet.offset);
  for (uint32_t i = 0; i < 4; ++i)
    out.display[i] = r.u32(out.renderer + ui::kRendererDisplayArea.offset + 4 * i);
  for (uint32_t i = 0; i < 2; ++i)
    out.display[4 + i] = r.u32(out.renderer + ui::kRendererTexelOffset.offset + 4 * i);
  out.quads = r.u32(out.renderer + ui::kRendererQuadListSize.offset);
  if (out.sheet == 0) return Reason::kSkip;  // nothing drawn
  switch (detail::Visible(r, out.sheet)) {
    case detail::Visibility::kHidden: return Reason::kSkip;  // nothing drawn
    case detail::Visibility::kTooDeep: return Reason::kUnwalkable;
    case detail::Visibility::kVisible: break;
  }

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
    if (Reason why = detail::Window(r, w, d); why != Reason::kSkip) {
      if (culprit) *culprit = w;
      return why;
    }
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
    const detail::Visibility visible = detail::Visible(r, child);
    if (visible == detail::Visibility::kTooDeep) {
      if (culprit) *culprit = child;
      return Reason::kUnwalkable;
    }
    if (visible == detail::Visibility::kHidden) continue;
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
