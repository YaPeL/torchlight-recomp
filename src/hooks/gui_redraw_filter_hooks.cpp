// The GUI redraw filter (--native_gui_redraw_filter, on by default): System::renderGUI
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
// is checked twice. The quad list it queues is compared byte for byte with the list of the rebuild
// the filter would have kept (the last one the rule did not skip; the three padding bytes of each
// quad left out). Its guest writes, recorded by the store trace from renderGUI's entry to
// doRender's (diag/store_trace_api.h BeginSection), may change only: the chunks of the list nodes
// the rebuild allocated (the nodes of the final list) and the list's head node, the renderer's
// fields a rebuild sets and the System's flag, heap blocks the rebuild freed and did not allocate
// again (dead memory), and the heap allocator's own state; and every block it allocates and keeps
// must be a node of the list. Every 10 s it logs the frames, requests, those it would skip, lists
// that differ and unexpected writes; the criterion is zero of both.
//
// renderGUI runs on the guest's render thread only: the state here is that thread's.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "game_menu/guest_call.h"
#include "guest_abi/cegui.h"
#include "guest_abi/game_ui.h"
#include "guest_abi/guest_heap.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/gui_redraw_filter.h"
#include "hooks/gui_redraw_filter_hooks.h"

#if TORCHLIGHT_DIAGNOSTICS
#include "diag/store_trace_api.h"
#endif

REXCVAR_DEFINE_BOOL(native_gui_redraw_filter, true, "Torchlight",
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
namespace heap = torchlight::guest_abi::guest_heap;
namespace gr = torchlight::hooks::gui_redraw;

// The rule's reader: guest memory, and the rect getters for a stale cache (called as drawSelf
// calls them, on the game's thread below the hooked call's stack pointer; null `ctx`: no refresh).
struct GuestReader {
  uint8_t* base;
  PPCContext* ctx;
  uint8_t u8(uint32_t a) const { return abi::ReadU8(base, a); }
  uint32_t u32(uint32_t a) const { return abi::ReadU32(base, a); }
  bool refresh(uint32_t window) const {
    if (ctx == nullptr) return false;
    torchlight::game_menu::GuestCall call(*ctx, base, 0x40);
    const uint32_t rect = call.Reserve(ui::kRectBytes);
    if (rect == 0) return false;
    call.Call(ui::kWindowGetUnclippedPixelRect.address, {rect, window});
    call.Call(ui::kWindowGetPixelRect.address, {rect, window});
    return true;
  }
};

bool FilterEnabled() {
  static const bool enabled = REXCVAR_GET(native_gui_redraw_filter);
  return enabled;
}

// The state after the last rebuild the rule did not skip, collected at doRender's entry: what the
// rebuild queued from (what renderGUI does after it, the cursor and cleanDeadPool, does not count).
struct Filter {
  gr::State last;
  bool have_last = false;
  bool collect_at_render = false;  // set for the rebuild under way
  uint32_t system = 0;
  gr::State now;  // reused
};
Filter g_filter;

// The state the rebuild queued from (drawSelf computed every rect it reads: no refresh).
void CollectLast(uint8_t* base) {
  Filter& f = g_filter;
  f.collect_at_render = false;
  f.have_last = gr::Collect(GuestReader{base, nullptr}, f.system, f.last) == gr::Reason::kSkip;
}

#if TORCHLIGHT_DIAGNOSTICS

bool ShadowEnabled() {
  static const bool enabled = REXCVAR_GET(diag_gui_redraw_shadow);
  return enabled;
}

// The renderer's quad list in order (the multiset's in-order walk), each QuadInfo's bytes with the
// padding zeroed, and the nodes' addresses (the head's first). False when the walk does not match
// the list's size.
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
  // The rebuild being checked.
  bool section = false;
  uint32_t system = 0, renderer = 0, sp = 0;
  std::thread::id thread;
  // Guest heap blocks (their dlmalloc chunks, header included) freed on this thread during the
  // checked rebuild and not allocated again: start -> end.
  std::map<uint32_t, uint32_t> freed;
  // Blocks allocated on this thread during the checked rebuild and still allocated: start -> end,
  // and kAlloc's caller.
  std::map<uint32_t, uint32_t> allocated;
  std::map<uint32_t, uint32_t> allocated_by;
  // After the checked rebuild, until renderGUI returns: frees of blocks it allocated and kept.
  bool after = false;
  std::set<uint32_t> freed_after;
  // The final list's nodes (the head's first); the chunks of the head node and of the nodes the
  // rebuild allocated (start -> end: a node's allocation is larger than the node, the thread cache
  // serving 92 bytes from its 128-byte bin, and its slack belongs to the list too).
  std::vector<uint32_t> new_nodes;
  std::map<uint32_t, uint32_t> list_chunks;
  // Blocks the rebuild allocated and kept that are not nodes of the list (their chunk starts).
  std::vector<uint32_t> kept_not_nodes;
  std::vector<uint8_t> quads;
  bool quads_read = false;
  std::vector<torchlight::diag::store_trace::ChangedRun> runs;

  // Counted since the last log.
  uint64_t frames = 0, requests = 0, would_skip = 0, compared = 0, lists_differ = 0,
           unreadable = 0, not_traced = 0, unexpected = 0, unexpected_frames = 0, node_bytes = 0,
           renderer_bytes = 0, freed_bytes = 0, allocator_bytes = 0, allocs = 0, frees = 0,
           kept_blocks = 0;
  std::array<uint64_t, size_t(gr::Reason::kCount)> reasons{};
  std::map<std::string, uint64_t> unexpected_by_function;
  // Shown in detail for the whole run: the first unexpected write of each writer.
  uint32_t shown = 0;
  std::set<std::string> shown_writers;
  uint32_t detailed_frames = 0;  // frames whose unexpected runs were all logged
  uint32_t kept_logged = 0;
  std::chrono::steady_clock::time_point last_log = std::chrono::steady_clock::now();
};
Shadow g_shadow;

bool InRendererFields(uint32_t renderer, uint32_t address) {
  struct Range {
    uint32_t offset, size;
  };
  static constexpr Range kFields[] = {
      {ui::kRendererZ.offset, 4},
      {ui::kRendererQuadListSize.offset, 4},
      {ui::kRendererBufferIndex.offset, 4},
      {ui::kRendererSorted.offset, 1},
      // Set by renderGUI before the rebuild and cleared after doRender, in either path.
      {ui::kRendererQueueing.offset, 1},
  };
  for (const Range& f : kFields) {
    if (address - (renderer + f.offset) < f.size) return true;
  }
  return false;
}

// A window's name (a CEGUI String at guest_abi game_ui kWindowName, its fields in
// game_ui cegui_string), ASCII only, for the shadow mode's details.
std::string WindowName(const uint8_t* base, uint32_t window) {
  namespace cs = abi::game_ui::cegui_string;
  const uint32_t str = window + abi::game_ui::kWindowName.offset;
  const uint32_t length = std::min(abi::ReadU32(base, str + cs::kLength.offset), 64u);
  const uint32_t chars = abi::ReadU32(base, str + cs::kReserve.offset) > cs::kInlineCapacity
                             ? abi::ReadU32(base, str + cs::kHeapBuffer.offset)
                             : str + cs::kInlineBuffer.offset;
  std::string name;
  for (uint32_t i = 0; i < length; ++i) {
    const uint32_t c = abi::ReadU32(base, chars + 4 * i);
    name += c >= 0x20 && c < 0x7F ? char(c) : '?';
  }
  return name;
}

bool InRanges(const std::map<uint32_t, uint32_t>& ranges, uint32_t address) {
  auto it = ranges.upper_bound(address);
  return it != ranges.begin() && address < std::prev(it)->second;
}

// Takes [begin, end) out of the ranges, keeping what is left of each on either side (dlmalloc
// carves an allocation out of a larger free chunk).
void Subtract(std::map<uint32_t, uint32_t>& ranges, uint32_t begin, uint32_t end) {
  auto it = ranges.upper_bound(begin);
  if (it != ranges.begin() && std::prev(it)->second > begin) --it;
  while (it != ranges.end() && it->first < end) {
    const uint32_t first = it->first, last = it->second;
    it = ranges.erase(it);
    if (first < begin) ranges[first] = begin;
    if (last > end) it = ranges.emplace(end, last).first, ++it;
  }
}

// A heap block's dlmalloc chunk (guest_abi guest_heap), its header included; the header and 8
// bytes when the size word is not that of a chunk in use.
std::pair<uint32_t, uint32_t> Chunk(const uint8_t* base, uint32_t ptr) {
  const uint32_t word = abi::ReadU32(base, ptr - heap::kSizeWordBelow);
  const uint32_t size =
      (word & heap::kChunkInUse) ? (word & heap::kChunkSizeMask) : 2 * heap::kChunkHeaderBytes;
  return {ptr - heap::kChunkHeaderBytes, ptr - heap::kChunkHeaderBytes + size};
}

// A write by the guest heap's allocator (guest_abi guest_heap kAllocatorFunctions), from the store
// trace's writer name (the generated function's, "__imp__sub_" and its address).
bool AllocatorWriter(const char* function) {
  constexpr std::string_view kPrefix = "__imp__sub_";
  if (!function || std::strncmp(function, kPrefix.data(), kPrefix.size()) != 0) return false;
  const uint32_t address = uint32_t(std::strtoul(function + kPrefix.size(), nullptr, 16));
  for (uint32_t f : heap::kAllocatorFunctions) {
    if (f == address) return true;
  }
  return false;
}

bool Allowed(const Shadow& s, uint32_t a, const char* function) {
  return InRanges(s.list_chunks, a) || InRendererFields(s.renderer, a) ||
         a == s.system + ui::kSystemGuiRedraw.offset || InRanges(s.freed, a) ||
         AllocatorWriter(function);
}

// The checked rebuild's writes, against what a rebuild may change: the chunks of the list nodes it
// allocated and the head node (list_chunks), the renderer's fields a rebuild sets, the System's
// redraw flag (cleared in either path), heap blocks freed during the rebuild and still free (dead
// memory: the old nodes, temporary strings), and the heap allocator's own state. A write into an
// old node's chunk that the rebuild allocated again for something else falls in none of them: it
// is unexpected. So is every block the rebuild allocated and kept that is not a node of the list.
void Classify(const uint8_t* base, Shadow& s) {
  bool unexpected = !s.kept_not_nodes.empty();
  s.kept_blocks += s.kept_not_nodes.size();
  for (uint32_t begin : s.kept_not_nodes) {
    if (s.kept_logged >= 20) break;
    ++s.kept_logged;
    REXLOG_WARN("gui redraw shadow: the rebuild allocated and kept a block that is not a node of "
                "the list ({:08X}, {} bytes, allocated from {:08X}, {} before renderGUI returned)",
                begin + heap::kChunkHeaderBytes,
                s.allocated[begin] - begin - heap::kChunkHeaderBytes, s.allocated_by[begin],
                s.freed_after.count(begin) ? "freed" : "not freed");
  }
  bool unexpected_write = false;
  for (const auto& run : s.runs) {
    for (uint32_t i = 0; i < run.size; ++i) {
      const uint32_t a = run.address + i;
      if (InRanges(s.list_chunks, a)) {
        ++s.node_bytes;
      } else if (InRendererFields(s.renderer, a) || a == s.system + ui::kSystemGuiRedraw.offset) {
        ++s.renderer_bytes;
      } else if (InRanges(s.freed, a)) {
        ++s.freed_bytes;
      } else if (AllocatorWriter(run.function)) {
        ++s.allocator_bytes;
      } else {
        ++s.unexpected;
        const std::string writer = run.function ? run.function : "?";
        ++s.unexpected_by_function[writer];
        if (!unexpected_write && s.shown < 60 && s.shown_writers.insert(writer).second) {
          ++s.shown;
          auto in = s.allocated.upper_bound(a);
          const bool kept = in != s.allocated.begin() && a < std::prev(in)->second;
          REXLOG_WARN("gui redraw shadow: unexpected write at {:08X} (+{} bytes of a run of {}) "
                      "by {}, {}",
                      a, run.size - i, run.size, writer,
                      kept ? fmt::format("in a block the rebuild allocated and kept ({:08X}, {} "
                                         "bytes, allocated from {:08X}, {} before renderGUI "
                                         "returned)",
                                         std::prev(in)->first + heap::kChunkHeaderBytes,
                                         std::prev(in)->second - std::prev(in)->first -
                                             heap::kChunkHeaderBytes,
                                         s.allocated_by[std::prev(in)->first],
                                         s.freed_after.count(std::prev(in)->first) ? "freed"
                                                                                   : "not freed")
                           : fmt::format("in memory older than the rebuild (its stack pointer "
                                         "{:08X})",
                                         s.sp));
        }
        unexpected_write = true;
      }
    }
  }
  unexpected = unexpected || unexpected_write;
  s.unexpected_frames += unexpected;
  // The first frames with unexpected writes: every such run, with the 48 bytes around it as they
  // are now (hex, and as UTF-16 text), and the bytes it had before.
  if (unexpected_write && s.detailed_frames < 3) {
    ++s.detailed_frames;
    for (const auto& run : s.runs) {
      const uint32_t a = run.address;
      if (Allowed(s, a, run.function)) continue;
      const uint32_t from = (a & ~15u) - 16;
      std::string hex, text;
      for (uint32_t i = 0; i < 48; ++i) hex += fmt::format("{:02X}", abi::ReadU8(base, from + i));
      for (uint32_t i = 0; i < 48; i += 2) {
        const uint16_t c = abi::ReadU16(base, from + i);
        text += c >= 0x20 && c < 0x7F ? char(c) : '.';
      }
      std::string before;
      for (uint32_t i = 0; i < std::min(run.size, 8u); ++i)
        before += fmt::format("{:02X}", reinterpret_cast<const uint8_t*>(&run.before)[i]);
      // Inside a drawn window's render cache (its image or text vector)?
      std::string owner = "not in a drawn window's render cache";
      for (const gr::Drawn& d : g_filter.last.drawn) {
        if (a - d.cache[0] < d.cache[1] - d.cache[0] || a - d.cache[2] < d.cache[3] - d.cache[2]) {
          owner = fmt::format("in the render cache ({}) of window {:08X} '{}'",
                              a - d.cache[2] < d.cache[3] - d.cache[2] ? "texts" : "images",
                              d.window, WindowName(base, d.window));
          break;
        }
      }
      REXLOG_WARN("gui redraw shadow: frame {} detail: {:08X} +{} by {} (was {}), {}; {:08X}: {} "
                  "\"{}\"",
                  s.detailed_frames, a, run.size, run.function ? run.function : "?", before, owner,
                  from, hex, text);
    }
  }
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
              "differ, {} unreadable, {} not traced), {} frames with unexpected writes ({} "
              "bytes{}{}, {} kept blocks not list nodes); rebuilds: {}; bytes written to list "
              "nodes {}, renderer and System fields {}, freed blocks {}, allocator state {}; heap "
              "calls tracked {} allocations, {} frees",
              s.frames, s.requests, s.would_skip, s.compared, s.lists_differ, s.unreadable,
              s.not_traced, s.unexpected_frames, s.unexpected, writers.empty() ? "" : ": ",
              writers, s.kept_blocks, reasons.empty() ? "none" : reasons, s.node_bytes,
              s.renderer_bytes, s.freed_bytes, s.allocator_bytes, s.allocs, s.frees);
  s.frames = s.requests = s.would_skip = s.compared = s.lists_differ = s.unreadable =
      s.not_traced = s.unexpected = s.unexpected_frames = s.node_bytes = s.renderer_bytes =
          s.freed_bytes = s.allocator_bytes = s.allocs = s.frees = s.kept_blocks = 0;
  s.reasons = {};
  s.unexpected_by_function.clear();
  s.last_log = now;
}

// At doRender's entry: the checked rebuild ends there. The list's chunks are the head node's and
// those of the nodes the rebuild allocated, which must be every other node (the list was cleared);
// every other block it allocated and kept is reported.
void EndShadowSection(const uint8_t* base) {
  Shadow& s = g_shadow;
  if (!s.section) return;
  s.section = false;
  s.after = true;  // until renderGUI returns: frees of the blocks the rebuild kept
  s.runs = torchlight::diag::store_trace::EndSection(base);
  s.quads_read = ReadQuads(base, s.renderer, &s.quads, &s.new_nodes);
  s.list_chunks.clear();
  std::set<uint32_t> nodes;
  for (size_t i = 0; i < s.new_nodes.size(); ++i) {
    const uint32_t node = s.new_nodes[i];
    const auto [begin, end] = Chunk(base, node);
    if (i == 0) {  // the head node, kept by the list
      s.list_chunks[begin] = std::max(end, node + ui::quad_node::kBytes);
      continue;
    }
    // A node this rebuild did not allocate, or not through addQuad's node allocation, is not
    // allowed (and reported below if the rebuild allocated it).
    auto it = s.allocated.find(begin);
    if (it == s.allocated.end() || s.allocated_by[begin] != heap::kQuadNodeAllocReturn) continue;
    s.list_chunks[begin] = it->second;
    nodes.insert(begin);
  }
  s.kept_not_nodes.clear();
  for (const auto& [begin, end] : s.allocated) {
    if (!nodes.count(begin)) s.kept_not_nodes.push_back(begin);
  }
}

// A request the rule would skip: rebuild anyway and check it.
void ShadowRebuild(PPCContext& ctx, uint8_t* base, uint32_t system,
                   void (*original)(PPCContext&, uint8_t*)) {
  Shadow& s = g_shadow;
  s.system = system;
  s.sp = ctx.r1.u32;
  s.renderer = abi::ReadU32(base, system + ui::kSystemRenderer.offset);
  s.thread = std::this_thread::get_id();
  s.freed.clear();
  s.allocated.clear();
  s.allocated_by.clear();
  s.freed_after.clear();
  s.kept_not_nodes.clear();
  s.list_chunks.clear();
  s.section = torchlight::diag::store_trace::BeginSection(ui::kSystemRenderGui.address,
                                                          ctx.r1.u32);
  const bool traced = s.section;
  s.runs.clear();
  s.quads_read = false;
  original(ctx, base);
  s.after = false;
  EndShadowSection(base);  // when doRender was not reached
  if (!traced) ++s.not_traced;
  if (!s.quads_read || !s.have_kept) {
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
  if (traced) Classify(base, s);
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
  const GuestReader reader{base, &ctx};
  uint32_t culprit = 0;
  const gr::Reason collected = gr::Collect(reader, system, f.now, &culprit);
  const gr::Reason reason = gr::Decide(f.have_last ? &f.last : nullptr, f.now, collected);
#if TORCHLIGHT_DIAGNOSTICS
  if (shadow) {
    ++g_shadow.requests;
    ++g_shadow.reasons[size_t(reason)];
    // The windows that make the rule rebuild: each one's first time, by name.
    if (culprit != 0) {
      static std::set<std::pair<uint32_t, int>> seen;
      if (seen.size() < 100 && seen.insert({culprit, int(reason)}).second) {
        REXLOG_INFO("gui redraw shadow: rebuild ({}) for window {:08X} '{}'",
                    gr::ReasonName(reason), culprit, WindowName(base, culprit));
      }
    }
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
  f.collect_at_render = true;
  f.system = system;
  __imp__sub_821FBC60(ctx, base);
  if (f.collect_at_render) CollectLast(base);  // doRender was not reached
#if TORCHLIGHT_DIAGNOSTICS
  if (shadow) {
    g_shadow.have_kept = ReadQuads(base, f.last.renderer, &g_shadow.kept, nullptr);
    LogShadow(g_shadow);
  }
#endif
}

#if TORCHLIGHT_DIAGNOSTICS
// The guest heap's free (guest_abi game_ui kFree) and allocation (kAlloc), while a checked rebuild
// runs on this thread: a freed block is dead memory until allocated again; an allocated one is
// tracked until freed. Each by its dlmalloc chunk (guest_abi guest_heap), header included.
REX_EXTERN(__imp__sub_821CD9A8);
REX_FUNC(sub_821CD9A8) {
  static_assert(abi::game_ui::kFree.address == 0x821CD9A8u);
  Shadow& s = g_shadow;
  const uint32_t chunk = ctx.r3.u32 - heap::kChunkHeaderBytes;
  if (s.after && ctx.r3.u32 != 0 && s.thread == std::this_thread::get_id() &&
      s.allocated.count(chunk)) {
    s.freed_after.insert(chunk);
  }
  if (s.section && ctx.r3.u32 != 0 && s.thread == std::this_thread::get_id()) {
    ++s.frees;
    if (abi::ReadU32(base, ctx.r3.u32 - heap::kSizeWordBelow) & heap::kChunkInUse) {
      const auto [begin, end] = Chunk(base, ctx.r3.u32);
      Subtract(s.allocated, begin, end);
      Subtract(s.freed, begin, end);
      s.freed[begin] = end;
    }
  }
  __imp__sub_821CD9A8(ctx, base);
}
REX_EXTERN(__imp__sub_821CD7F8);
REX_FUNC(sub_821CD7F8) {
  static_assert(abi::game_ui::kAlloc.address == 0x821CD7F8u);
  Shadow& s = g_shadow;
  const bool track = s.section && s.thread == std::this_thread::get_id();
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_821CD7F8(ctx, base);
  if (!track || ctx.r3.u32 == 0) return;
  ++s.allocs;
  const auto [begin, end] = Chunk(base, ctx.r3.u32);
  Subtract(s.freed, begin, end);
  Subtract(s.allocated, begin, end);
  s.allocated[begin] = end;
  s.allocated_by[begin] = caller;
}
#endif

// OgreCEGUIRenderer::doRender: at its entry the rebuild has queued everything. The filter takes its
// state there, and the shadow mode's checked rebuild ends there.
REX_EXTERN(__imp__sub_821D2F80);
REX_FUNC(sub_821D2F80) {
  static_assert(ui::kRendererDoRender.address == 0x821D2F80u);
#if TORCHLIGHT_DIAGNOSTICS
  if (g_shadow.section) EndShadowSection(base);
#endif
  if (g_filter.collect_at_render) CollectLast(base);
  __imp__sub_821D2F80(ctx, base);
}

}  // extern "C"
