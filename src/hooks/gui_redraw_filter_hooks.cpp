// The GUI redraw filter (--native_gui_redraw_filter, off by default): System::renderGUI
// (sub_821FBC60, guest_abi cegui) wrapped so that a redraw CEGUI asks for, which would queue the
// same quads the renderer holds (the rule in hooks/gui_redraw_filter.h), is skipped: the System's
// flag is cleared before the guest's function runs, which then only draws the list it has (doRender
// reuses its vertex buffers while the list is unchanged), the mouse cursor and the rest, as on a
// frame without a request. Most such requests come from windows that do not change what is drawn
// (the menus' animations of windows CEGUI does not draw, a setPosition to the same place), and the
// rebuild walks the whole window tree each time. The idea is torchlight-nx's (1.0.5, mode 3; see
// gui_redraw_filter.h for the reference and what differs).
//
// The shadow mode (--diag_gui_redraw_shadow, diagnostics builds; torchlight-nx's mode 2): the rule
// runs on every request and nothing is skipped. When it would skip, the rebuild runs anyway, and
// is checked twice: the quad list it queues is compared byte for byte with the list of the
// rebuild the filter would have kept (the last one the rule did not skip; the three padding bytes
// of each quad left out), and its guest writes, recorded by the store trace from renderGUI's entry
// to doRender's, must change nothing but the quad list's nodes and the renderer's fields a
// rebuild sets (diag/store_trace_api.h BeginSection). Every 10 s it logs the frames, requests,
// those it would skip, lists that differ and unexpected writes; the criterion is zero of both.
//
// renderGUI runs on the guest's render thread only: the state here is that thread's.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/cegui.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/gui_redraw_filter.h"
#include "hooks/gui_redraw_filter_hooks.h"

#if TORCHLIGHT_DIAGNOSTICS
#include "diag/store_trace_api.h"
#endif

REXCVAR_DEFINE_BOOL(native_gui_redraw_filter, false, "Torchlight",
                    "Skip the GUI's redraws that would queue the same quads it already draws "
                    "(after torchlight-nx; false: every redraw CEGUI asks for)");
#if TORCHLIGHT_DIAGNOSTICS
REXCVAR_DEFINE_BOOL(diag_gui_redraw_shadow, false, "Torchlight",
                    "Diagnostics: run the GUI redraw filter's rule without skipping; when it "
                    "would skip, compare the rebuild's quads with the kept list byte for byte and "
                    "record its writes, logged every 10 s");
#endif

namespace {

namespace abi = torchlight::guest_abi;
namespace ui = torchlight::guest_abi::cegui;
namespace gr = torchlight::hooks::gui_redraw;

struct GuestReader {
  const uint8_t* base;
  uint8_t u8(uint32_t a) const { return abi::ReadU8(base, a); }
  uint32_t u32(uint32_t a) const { return abi::ReadU32(base, a); }
};

bool FilterEnabled() {
  static const bool enabled = REXCVAR_GET(native_gui_redraw_filter);
  return enabled;
}

// The state after the last rebuild the rule did not skip.
struct Filter {
  gr::State last;
  bool have_last = false;
  gr::State now;  // reused
};
Filter g_filter;

#if TORCHLIGHT_DIAGNOSTICS

bool ShadowEnabled() {
  static const bool enabled = REXCVAR_GET(diag_gui_redraw_shadow);
  return enabled;
}

// The renderer's quad list in order (the multiset's in-order walk), each QuadInfo's bytes with the
// padding zeroed, and the nodes' addresses (the head's included). False when the walk does not
// match the list's size.
bool ReadQuads(const uint8_t* base, uint32_t renderer, std::vector<uint8_t>* quads,
               std::vector<uint32_t>* nodes) {
  namespace qn = ui::quad_node;
  const uint32_t head = abi::ReadU32(base, renderer + ui::kRendererQuadListHead.offset);
  const uint32_t count = abi::ReadU32(base, renderer + ui::kRendererQuadListSize.offset);
  if (quads) quads->clear();
  if (nodes) {
    nodes->clear();
    nodes->push_back(head);
  }
  if (head == 0 || count > 1u << 20) return false;
  auto nil = [&](uint32_t n) { return abi::ReadU8(base, n + qn::kIsNil.offset) != 0; };
  uint32_t node = abi::ReadU32(base, head + qn::kLeft.offset);
  for (uint32_t i = 0; i < count; ++i) {
    if (node == 0 || nil(node)) return false;
    if (quads) {
      const size_t at = quads->size();
      quads->resize(at + ui::kQuadBytes);
      for (uint32_t b = 0; b < ui::kQuadBytes; ++b) {
        const bool padding = b >= ui::kQuadPaddingBegin && b < ui::kQuadPaddingEnd;
        (*quads)[at + b] = padding ? 0 : abi::ReadU8(base, node + qn::kValue.offset + b);
      }
    }
    if (nodes) nodes->push_back(node);
    // The successor.
    uint32_t right = abi::ReadU32(base, node + qn::kRight.offset);
    if (!nil(right)) {
      node = right;
      for (uint32_t l; !nil(l = abi::ReadU32(base, node + qn::kLeft.offset));) node = l;
    } else {
      uint32_t parent = abi::ReadU32(base, node + qn::kParent.offset);
      while (!nil(parent) && node == abi::ReadU32(base, parent + qn::kRight.offset)) {
        node = parent;
        parent = abi::ReadU32(base, node + qn::kParent.offset);
      }
      node = parent;
    }
  }
  return node == head;
}

struct Shadow {
  std::vector<uint8_t> kept;  // the quads of the last rebuild the rule did not skip
  bool have_kept = false;
  // The rebuild being checked: the nodes before it, and what doRender's entry found.
  bool section = false;
  uint32_t renderer = 0;
  std::vector<uint32_t> old_nodes, new_nodes;
  std::vector<uint8_t> quads;
  bool quads_read = false;
  std::vector<torchlight::diag::store_trace::ChangedRun> runs;

  // Counted since the last log.
  uint64_t frames = 0, requests = 0, would_skip = 0, compared = 0, lists_differ = 0,
           unreadable = 0, not_traced = 0, unexpected = 0, unexpected_frames = 0, node_bytes = 0,
           renderer_bytes = 0;
  std::array<uint64_t, size_t(gr::Reason::kCount)> reasons{};
  std::map<std::string, uint64_t> unexpected_by_function;
  // Shown in detail for the whole run.
  uint32_t shown = 0;
  std::chrono::steady_clock::time_point last_log = std::chrono::steady_clock::now();
};
Shadow g_shadow;

bool InNodes(const std::vector<uint32_t>& sorted, uint32_t address) {
  auto it = std::upper_bound(sorted.begin(), sorted.end(), address);
  return it != sorted.begin() && address - *(it - 1) < ui::quad_node::kBytes;
}

bool InRendererFields(uint32_t renderer, uint32_t address) {
  struct Range {
    uint32_t offset, size;
  };
  static constexpr Range kFields[] = {
      {ui::kRendererZ.offset, 4},
      {ui::kRendererQuadListSize.offset, 4},
      {ui::kRendererBufferIndex.offset, 4},
      {ui::kRendererSorted.offset, 1},
  };
  for (const Range& f : kFields) {
    if (address - (renderer + f.offset) < f.size) return true;
  }
  return false;
}

// The checked rebuild's writes, against what a rebuild may change.
void Classify(Shadow& s) {
  std::sort(s.old_nodes.begin(), s.old_nodes.end());
  std::sort(s.new_nodes.begin(), s.new_nodes.end());
  bool unexpected = false;
  for (const auto& run : s.runs) {
    for (uint32_t i = 0; i < run.size; ++i) {
      const uint32_t a = run.address + i;
      if (InNodes(s.old_nodes, a) || InNodes(s.new_nodes, a)) {
        ++s.node_bytes;
      } else if (InRendererFields(s.renderer, a)) {
        ++s.renderer_bytes;
      } else {
        ++s.unexpected;
        ++s.unexpected_by_function[run.function ? run.function : "?"];
        if (!unexpected && s.shown < 20) {
          ++s.shown;
          REXLOG_WARN("gui redraw shadow: unexpected write at {:08X} (+{} bytes of a run of {}) "
                      "by {}",
                      a, run.size - i, run.size, run.function ? run.function : "?");
        }
        unexpected = true;
      }
    }
  }
  s.unexpected_frames += unexpected;
}

void LogShadow(Shadow& s) {
  const auto now = std::chrono::steady_clock::now();
  if (now - s.last_log < std::chrono::seconds(10)) return;
  std::string reasons;
  for (size_t i = 1; i < s.reasons.size(); ++i) {
    if (s.reasons[i] == 0) continue;
    reasons += fmt::format("{}{} {}", reasons.empty() ? "" : ", ", s.reasons[i],
                           gr::ReasonName(gr::Reason(i)));
  }
  std::string writers;
  for (const auto& [function, n] : s.unexpected_by_function) {
    writers += fmt::format("{}{} {}", writers.empty() ? "" : ", ", function, n);
  }
  REXLOG_INFO("gui redraw shadow: {} frames, {} requests, {} would skip ({} compared, {} lists "
              "differ, {} unreadable, {} not traced), {} frames with unexpected writes ({} bytes{}{}); "
              "rebuilds: {}; bytes written to quad nodes {}, renderer fields {}",
              s.frames, s.requests, s.would_skip, s.compared, s.lists_differ, s.unreadable,
              s.not_traced, s.unexpected_frames, s.unexpected, writers.empty() ? "" : ": ",
              writers, reasons.empty() ? "none" : reasons, s.node_bytes, s.renderer_bytes);
  s.frames = s.requests = s.would_skip = s.compared = s.lists_differ = s.unreadable =
      s.not_traced = s.unexpected = s.unexpected_frames = s.node_bytes = s.renderer_bytes = 0;
  s.reasons = {};
  s.unexpected_by_function.clear();
  s.last_log = now;
}

// At doRender's entry: the checked rebuild ends there.
void EndShadowSection(const uint8_t* base) {
  Shadow& s = g_shadow;
  if (!s.section) return;
  s.section = false;
  s.runs = torchlight::diag::store_trace::EndSection(base);
  s.quads_read = ReadQuads(base, s.renderer, &s.quads, &s.new_nodes);
}

// A request the rule would skip: rebuild anyway and check it.
void ShadowRebuild(PPCContext& ctx, uint8_t* base, uint32_t system,
                   void (*original)(PPCContext&, uint8_t*)) {
  Shadow& s = g_shadow;
  s.renderer = abi::ReadU32(base, system + ui::kSystemRenderer.offset);
  const bool old_read = ReadQuads(base, s.renderer, nullptr, &s.old_nodes);
  s.section = torchlight::diag::store_trace::BeginSection(ui::kSystemRenderGui.address,
                                                          ctx.r1.u32);
  const bool traced = s.section;
  s.runs.clear();
  s.quads_read = false;
  original(ctx, base);
  EndShadowSection(base);  // when doRender was not reached
  if (!traced) ++s.not_traced;
  if (!old_read || !s.quads_read || !s.have_kept) {
    ++s.unreadable;
  } else {
    ++s.compared;
    if (s.quads != s.kept) {
      ++s.lists_differ;
      if (s.shown < 20) {
        ++s.shown;
        size_t at = 0;
        const size_t n = std::min(s.quads.size(), s.kept.size());
        while (at < n && s.quads[at] == s.kept[at]) ++at;
        REXLOG_WARN("gui redraw shadow: the rebuild's quads differ from the kept list ({} "
                    "against {} quads, first at quad {} byte {})",
                    s.quads.size() / ui::kQuadBytes, s.kept.size() / ui::kQuadBytes,
                    at / ui::kQuadBytes, at % ui::kQuadBytes);
      }
    }
  }
  if (traced) Classify(s);
}

#endif  // TORCHLIGHT_DIAGNOSTICS

void Clear8(uint8_t* base, uint32_t address) {
  const uint8_t zero = 0;
  abi::WriteBytes(base, address, &zero, 1);
}

}  // namespace

namespace torchlight::hooks {

void LogGuiRedrawFilterMode() {
  bool shadow = false;
#if TORCHLIGHT_DIAGNOSTICS
  shadow = ShadowEnabled();
#endif
  REXLOG_INFO("GUI redraws: {} (--native_gui_redraw_filter={}{})",
              shadow ? "every one, the filter's rule checked in shadow"
              : FilterEnabled() ? "only those that change what is drawn (after torchlight-nx)"
                                : "every one CEGUI asks for",
              FilterEnabled(), shadow ? ", --diag_gui_redraw_shadow" : "");
}

}  // namespace torchlight::hooks

extern "C" {

REX_EXTERN(__imp__sub_821FBC60);
REX_FUNC(sub_821FBC60) {
  static_assert(ui::kSystemRenderGui.address == 0x821FBC60u);
  bool shadow = false;
#if TORCHLIGHT_DIAGNOSTICS
  shadow = ShadowEnabled();
  if (shadow) ++g_shadow.frames;
#endif
  if (!FilterEnabled() && !shadow) return __imp__sub_821FBC60(ctx, base);
  const uint32_t system = ctx.r3.u32;
  if (abi::ReadU8(base, system + ui::kSystemGuiRedraw.offset) == 0) {
    __imp__sub_821FBC60(ctx, base);  // no request: the game draws its list
#if TORCHLIGHT_DIAGNOSTICS
    if (shadow) LogShadow(g_shadow);
#endif
    return;
  }
  Filter& f = g_filter;
  const GuestReader reader{base};
  const gr::Reason collected = gr::Collect(reader, system, f.now);
  const gr::Reason reason = gr::Decide(f.have_last ? &f.last : nullptr, f.now, collected);
#if TORCHLIGHT_DIAGNOSTICS
  if (shadow) {
    ++g_shadow.requests;
    ++g_shadow.reasons[size_t(reason)];
    if (reason == gr::Reason::kSkip) {
      ++g_shadow.would_skip;
      ShadowRebuild(ctx, base, system, &__imp__sub_821FBC60);
      LogShadow(g_shadow);
      return;
    }
  }
#endif
  if (reason == gr::Reason::kSkip) {
    Clear8(base, system + ui::kSystemGuiRedraw.offset);
    return __imp__sub_821FBC60(ctx, base);
  }
  __imp__sub_821FBC60(ctx, base);
  f.have_last = gr::Collect(reader, system, f.last) == gr::Reason::kSkip;
#if TORCHLIGHT_DIAGNOSTICS
  if (shadow) {
    g_shadow.have_kept = ReadQuads(base, f.last.renderer, &g_shadow.kept, nullptr);
    LogShadow(g_shadow);
  }
#endif
}

#if TORCHLIGHT_DIAGNOSTICS
// OgreCEGUIRenderer::doRender: the shadow mode's checked rebuild ends at its entry.
REX_EXTERN(__imp__sub_821D2F80);
REX_FUNC(sub_821D2F80) {
  static_assert(ui::kRendererDoRender.address == 0x821D2F80u);
  if (g_shadow.section) EndShadowSection(base);
  __imp__sub_821D2F80(ctx, base);
}
#endif

}  // extern "C"
