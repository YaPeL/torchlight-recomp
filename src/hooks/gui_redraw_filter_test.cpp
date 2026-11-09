// The GUI redraw filter's rule (gui_redraw_filter.h) over a made-up window tree.

#include <cstdio>
#include <map>
#include <vector>

#include "guest_abi/cegui.h"
#include "hooks/gui_redraw_filter.h"

namespace {

namespace ui = torchlight::guest_abi::cegui;
namespace gr = torchlight::hooks::gui_redraw;
using gr::Reason;

int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}

// Guest memory as a sparse map of bytes and words (unwritten reads are zero). refresh() stands for
// the rect getters: when `computable`, it sets the window's two cache flags (the rects stay as
// written, as if computed to those values).
struct Memory {
  std::map<uint32_t, uint8_t> bytes;
  std::map<uint32_t, uint32_t> words;
  bool computable = false;
  mutable int refreshes = 0;
  bool refresh(uint32_t w) const {
    ++refreshes;
    if (!computable) return false;
    auto& b = const_cast<std::map<uint32_t, uint8_t>&>(bytes);
    b[w + ui::kWindowUnclippedRectValid.offset] = 1;
    b[w + ui::kWindowClippedRectValid.offset] = 1;
    return true;
  }
  uint8_t u8(uint32_t a) const {
    auto it = bytes.find(a);
    return it == bytes.end() ? 0 : it->second;
  }
  uint32_t u32(uint32_t a) const {
    auto it = words.find(a);
    return it == words.end() ? 0 : it->second;
  }
};

constexpr uint32_t kSystem = 0x100;
constexpr uint32_t kRenderer = 0x200;
constexpr uint32_t kVtable = 0x1000;       // CEGUI's own slots
constexpr uint32_t kVtableEmpty = 0x1400;  // a container's empty drawSelf
constexpr uint32_t kVtableOther = 0x1800;  // a drawSelf that is not CEGUI's

struct Tree {
  Memory m;
  std::map<uint32_t, std::vector<uint32_t>> children;
  uint32_t next_list = 0x80000;

  Tree() {
    m.words[kSystem + ui::kSystemRenderer.offset] = kRenderer;
    for (uint32_t vt : {kVtable, kVtableEmpty, kVtableOther}) {
      m.words[vt + ui::kVtOnRenderingStarted] = ui::kOnRenderingStarted;
      m.words[vt + ui::kVtOnRenderingEnded] = ui::kOnRenderingEnded;
    }
    m.words[kVtable + ui::kVtDrawSelf] = ui::kDrawSelf;
    m.words[kVtableEmpty + ui::kVtDrawSelf] = ui::kDrawSelfEmpty;
    m.words[kVtableOther + ui::kVtDrawSelf] = 0x82000000;
    m.words[kRenderer + ui::kRendererDisplayArea.offset + 12] = 0x44A00000;  // 1280.0f
  }
  // A window under `parent` with content (its render cache holds an image) or without.
  void Window(uint32_t w, uint32_t parent, bool content, uint32_t vt = kVtable) {
    m.words[w] = vt;
    m.words[w + ui::kWindowParent.offset] = parent;
    m.bytes[w + ui::kWindowVisible.offset] = 1;
    if (content) {
      m.words[w + ui::kWindowCacheImagesBegin.offset] = 0x90000 + w;
      m.words[w + ui::kWindowCacheImagesEnd.offset] = 0x90000 + w + 0x40;
    }
    m.bytes[w + ui::kWindowUnclippedRectValid.offset] = 1;
    m.bytes[w + ui::kWindowClippedRectValid.offset] = 1;
    m.words[w + ui::kWindowUnclippedRect.offset] = w;  // some rect
    m.words[w + ui::kWindowClippedRect.offset] = w;
    if (parent) {
      children[parent].push_back(w);
      Lists(parent);
    }
  }
  // The child vector and the draw list, the same windows (the draw list in `order` if given).
  void Lists(uint32_t w, std::vector<uint32_t> order = {}) {
    const std::vector<uint32_t>& c = children[w];
    if (order.empty()) order = c;
    const uint32_t list = next_list;
    next_list += 0x1000;
    for (size_t i = 0; i < order.size(); ++i) m.words[list + 4 * uint32_t(i)] = order[i];
    m.words[w + ui::kWindowChildrenBegin.offset] = list + 0x800;
    m.words[w + ui::kWindowChildrenEnd.offset] = list + 0x800 + 4 * uint32_t(c.size());
    m.words[w + ui::kWindowDrawListBegin.offset] = list;
  }
  void Sheet(uint32_t w) { m.words[kSystem + ui::kSystemActiveSheet.offset] = w; }
};

// A rebuild as drawSelf does it: every drawn window with CEGUI's drawSelf fills its cache and
// clears its d_needsRedraw (the rects are not computed: a stale one stays stale).
void Rebuild(Tree& t, uint32_t w) {
  if (w == 0 || !t.m.u8(w + ui::kWindowVisible.offset)) return;
  if (t.m.u32(w) == kVtable) t.m.bytes[w + ui::kWindowNeedsRedraw.offset] = 0;
  for (uint32_t c : t.children[w]) {
    if (t.m.u32(c + ui::kWindowParent.offset) == w) Rebuild(t, c);
  }
}

// The rule's decision for the tree's state against `last`; when it rebuilds, the rebuild, and
// `last` collected after it.
Reason Step(Tree& t, gr::State& last, bool& have_last) {
  gr::State now;
  const Reason collected = gr::Collect(t.m, kSystem, now);
  const Reason r = gr::Decide(have_last ? &last : nullptr, now, collected);
  if (r != Reason::kSkip) {
    Rebuild(t, t.m.u32(kSystem + ui::kSystemActiveSheet.offset));
    have_last = gr::Collect(t.m, kSystem, last) == Reason::kSkip;
  }
  return r;
}

}  // namespace

int main() {
  // sheet 0x10000 > panel 0x11000 (container, no content) > icon 0x12000, label 0x13000;
  // sheet > closed 0x14000 (hidden) > arrow 0x15000.
  Tree t;
  t.Window(0x10000, 0, true);
  t.Window(0x11000, 0x10000, false);
  t.Window(0x12000, 0x11000, true);
  t.Window(0x13000, 0x11000, true);
  t.Window(0x14000, 0x10000, true);
  t.Window(0x15000, 0x14000, true);
  t.m.bytes[0x14000 + ui::kWindowVisible.offset] = 0;
  t.Sheet(0x10000);

  gr::State last;
  bool have = false;
  Check(Step(t, last, have) == Reason::kNoSnapshot, "first request: rebuild");
  Check(have && last.drawn.size() == 4, "the drawn windows: sheet, panel, icon, label");
  Check(Step(t, last, have) == Reason::kSkip, "nothing changed: skip");

  // A drawn window asks to be filled again.
  t.m.bytes[0x13000 + ui::kWindowNeedsRedraw.offset] = 1;
  Check(Step(t, last, have) == Reason::kNeedsRedraw, "needs redraw: rebuild");
  Check(Step(t, last, have) == Reason::kSkip, "filled: skip");

  // The same on a window that is not drawn, or on the container's (it would be filled, but its
  // cache was empty and drawSelf reads it; it counts).
  t.m.bytes[0x15000 + ui::kWindowNeedsRedraw.offset] = 1;
  Check(Step(t, last, have) == Reason::kSkip, "needs redraw, not drawn: skip");
  t.m.bytes[0x11000 + ui::kWindowNeedsRedraw.offset] = 1;
  Check(Step(t, last, have) == Reason::kNeedsRedraw, "needs redraw, no content yet: rebuild");
  Check(Step(t, last, have) == Reason::kSkip, "filled: skip");

  // Moved: the rect caches are invalidated (notifyScreenAreaChanged); for a container without
  // content they do not count.
  t.m.bytes[0x11000 + ui::kWindowUnclippedRectValid.offset] = 0;
  t.m.bytes[0x11000 + ui::kWindowClippedRectValid.offset] = 0;
  Check(Step(t, last, have) == Reason::kSkip, "a container's stale rects: skip");
  t.m.bytes[0x12000 + ui::kWindowClippedRectValid.offset] = 0;
  Check(Step(t, last, have) == Reason::kRectStale, "stale rect with content: rebuild");
  Check(Step(t, last, have) == Reason::kRectStale, "still stale (the fake does not compute it)");
  t.m.bytes[0x12000 + ui::kWindowClippedRectValid.offset] = 1;
  Check(Step(t, last, have) == Reason::kNoSnapshot, "valid again, after a stale snapshot: rebuild");
  // Stale, and computed by the getters: compared like any rect.
  t.m.computable = true;
  t.m.bytes[0x12000 + ui::kWindowClippedRectValid.offset] = 0;
  Check(Step(t, last, have) == Reason::kSkip, "stale, computed to the same rect: skip");
  Check(t.m.refreshes > 0, "the getters were called");
  t.m.bytes[0x13000 + ui::kWindowUnclippedRectValid.offset] = 0;
  t.m.words[0x13000 + ui::kWindowUnclippedRect.offset] = 0x42000000;  // what they compute now
  Check(Step(t, last, have) == Reason::kChanged, "stale, computed to another rect: rebuild");
  Check(Step(t, last, have) == Reason::kSkip, "then: skip");
  t.m.computable = false;
  t.m.words[0x12000 + ui::kWindowUnclippedRect.offset + 8] = 0x41200000;  // computed elsewhere
  Check(Step(t, last, have) == Reason::kChanged, "a rect differs: rebuild");
  Check(Step(t, last, have) == Reason::kSkip, "then: skip");

  // The render cache changed without a request (its vectors).
  t.m.words[0x13000 + ui::kWindowCacheTextsEnd.offset] = 0x7000;
  Check(Step(t, last, have) == Reason::kChanged, "a render cache's vectors differ: rebuild");
  Step(t, last, have);

  // Shown, hidden, reordered.
  t.m.bytes[0x14000 + ui::kWindowVisible.offset] = 1;
  Check(Step(t, last, have) == Reason::kNeedsRedraw, "a panel shown, its arrow to fill: rebuild");
  Check(last.drawn.size() == 6, "and its arrow drawn");
  t.m.bytes[0x15000 + ui::kWindowVisible.offset] = 0;
  Check(Step(t, last, have) == Reason::kChanged, "a window hidden: rebuild");
  t.Lists(0x11000, {0x13000, 0x12000});
  Check(Step(t, last, have) == Reason::kChanged, "draw order changed: rebuild");
  Check(last.drawn[2].window == 0x13000, "the draw list's order");
  Check(Step(t, last, have) == Reason::kSkip, "then: skip");

  // The display.
  t.m.words[kRenderer + ui::kRendererDisplayArea.offset + 12] = 0x44F00000;
  Check(Step(t, last, have) == Reason::kChanged, "display area: rebuild");
  t.m.words[kRenderer + ui::kRendererTexelOffset.offset] = 0x3F000000;
  Check(Step(t, last, have) == Reason::kChanged, "texel offset: rebuild");
  Step(t, last, have);

  // Slots that are not CEGUI's; a container's empty drawSelf is.
  t.m.words[0x12000] = kVtableOther;
  Check(Step(t, last, have) == Reason::kOverridden, "another drawSelf: rebuild");
  Check(Step(t, last, have) == Reason::kOverridden, "every time");
  t.m.words[0x12000] = kVtableEmpty;
  Check(Step(t, last, have) == Reason::kNoSnapshot, "an empty drawSelf: collected");
  Check(Step(t, last, have) == Reason::kSkip, "then: skip");
  t.m.bytes[0x12000 + ui::kWindowNeedsRedraw.offset] = 1;
  Check(Step(t, last, have) == Reason::kSkip, "an empty drawSelf never fills its cache");
  t.m.words[0x12000] = kVtable;
  t.m.bytes[0x12000 + ui::kWindowNeedsRedraw.offset] = 0;
  Step(t, last, have);

  // A window whose parent pointer is hidden though it sits in a drawn list: isVisible follows the
  // parent pointer, as the guest's.
  t.m.words[0x13000 + ui::kWindowParent.offset] = 0x14000;
  t.m.bytes[0x14000 + ui::kWindowVisible.offset] = 0;
  Check(Step(t, last, have) == Reason::kChanged, "a hidden parent pointer: not drawn");
  t.m.words[0x13000 + ui::kWindowParent.offset] = 0x11000;
  Step(t, last, have);

  // A cycle, a null child, a child count out of bounds: not walked.
  {
    // A cycle through the parent pointers (each the other's parent, both visible): isVisible's walk
    // never reaches a root, so the bound stops it and the tree is not decided.
    Tree c = t;
    c.m.words[0x13000 + ui::kWindowParent.offset] = 0x14000;
    c.m.words[0x14000 + ui::kWindowParent.offset] = 0x13000;
    c.m.bytes[0x14000 + ui::kWindowVisible.offset] = 1;
    gr::State s;
    Check(gr::Collect(c.m, kSystem, s) == Reason::kUnwalkable, "a parent cycle: unwalkable");
  }
  {
    Tree c = t;
    c.children[0x13000].push_back(0x11000);
    c.Lists(0x13000);
    c.m.words[0x11000 + ui::kWindowParent.offset] = 0;  // visible on its own
    gr::State s;
    Check(gr::Collect(c.m, kSystem, s) == Reason::kUnwalkable, "a cycle: unwalkable");
  }
  {
    Tree c = t;
    c.m.words[c.m.u32(0x11000 + ui::kWindowDrawListBegin.offset)] = 0;
    gr::State s;
    Check(gr::Collect(c.m, kSystem, s) == Reason::kUnwalkable, "a null child: unwalkable");
  }
  {
    Tree c = t;
    c.m.words[0x11000 + ui::kWindowChildrenEnd.offset] =
        c.m.u32(0x11000 + ui::kWindowChildrenBegin.offset) - 4;
    gr::State s;
    Check(gr::Collect(c.m, kSystem, s) == Reason::kUnwalkable, "a negative count: unwalkable");
  }

  // Deep trees: a chain of visible windows as deep as the walk's bound is collected; one deeper is
  // not decided and rebuilds (a window past the bound must never count as hidden).
  for (const int length : {gr::kMaxDepth, gr::kMaxDepth + 1, gr::kMaxDepth + 6}) {
    Tree c;
    uint32_t parent = 0;
    for (int i = 0; i < length; ++i) {
      const uint32_t w = 0x100000 + 0x1000 * uint32_t(i);
      c.Window(w, parent, true);
      parent = w;
    }
    c.Sheet(0x100000);
    gr::State s;
    const Reason r = gr::Collect(c.m, kSystem, s);
    if (length <= gr::kMaxDepth) {
      Check(r == Reason::kSkip && s.drawn.size() == size_t(length),
            "a chain as deep as the bound: every window collected");
    } else {
      Check(r == Reason::kUnwalkable, "a chain deeper than the bound: unwalkable, so rebuilt");
    }
  }
  {
    // The sheet itself under a parent chain longer than the bound.
    Tree c;
    uint32_t parent = 0;
    for (int i = 0; i < gr::kMaxDepth + 2; ++i) {
      const uint32_t w = 0x100000 + 0x1000 * uint32_t(i);
      c.Window(w, parent, false);
      parent = w;
    }
    c.Sheet(parent);
    gr::State s;
    Check(gr::Collect(c.m, kSystem, s) == Reason::kUnwalkable,
          "a sheet under a chain deeper than the bound: unwalkable");
  }

  // The quad list's size is part of the state.
  t.m.words[kRenderer + ui::kRendererQuadListSize.offset] = 7;
  Check(Step(t, last, have) == Reason::kChanged, "the quad list's size differs: rebuild");
  Check(Step(t, last, have) == Reason::kSkip, "then: skip");

  // No active sheet, or a hidden one: nothing drawn, which is a state too.
  t.Sheet(0);
  Check(Step(t, last, have) == Reason::kChanged, "the sheet gone: rebuild (the list clears)");
  Check(last.drawn.empty(), "nothing drawn");
  Check(Step(t, last, have) == Reason::kSkip, "still none: skip");
  t.Sheet(0x10000);
  t.m.bytes[0x10000 + ui::kWindowVisible.offset] = 0;
  Check(Step(t, last, have) == Reason::kChanged, "a hidden sheet is another state");
  Check(Step(t, last, have) == Reason::kSkip, "then: skip");
  t.m.bytes[0x10000 + ui::kWindowVisible.offset] = 1;
  Check(Step(t, last, have) == Reason::kChanged, "shown again: rebuild");

  // A first state that cannot be collected leaves no snapshot.
  {
    Tree c = t;
    gr::State s;
    bool h = false;
    c.m.words[0x13000] = kVtableOther;  // a rebuild after which the walk still stops
    Check(Step(c, s, h) == Reason::kOverridden && !h, "no snapshot after an incomplete walk");
    c.m.words[0x13000] = kVtable;
    Check(Step(c, s, h) == Reason::kNoSnapshot, "so the next request rebuilds");
  }

  if (g_failures) return 1;
  std::puts("gui_redraw_filter_test: ok");
  return 0;
}
