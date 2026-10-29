// Diagnostics: who asks CEGUI to redraw the whole GUI, frame by frame. Wraps every function that
// sets the System's redraw flag (guest_abi gui_redraw) and records each call's writer with the
// three return addresses above it (the caller of the writer and two more: the game's call site is
// usually among them, since the writers are CEGUI's event handlers reached through setArea_impl,
// setVisible and the like); wraps System::renderGUI as the frame boundary, where the flag says
// whether this frame re-queues the tree. Logs with every 10 s frame summary the share of frames
// each writer and call chain was seen in ("gui redraw census: ..."). Reads only: every wrapper
// calls the guest's function unchanged. Compiled only with TORCHLIGHT_DIAGNOSTICS
// (CMakeLists.txt); then off by default (--diag_gui_redraw_census).

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/game_ui.h"
#include "guest_abi/gui_redraw.h"
#include "guest_abi/ogre_layout.h"
#include "live/frame_timing.h"

REXCVAR_DEFINE_BOOL(diag_gui_redraw_census, false, "Torchlight",
                    "Diagnostics: count, per frame, which guest functions asked CEGUI to redraw "
                    "the whole GUI and from where, and log the shares every 10 s");

namespace {

namespace abi = torchlight::guest_abi;

bool Enabled() {
  static const bool enabled = REXCVAR_GET(diag_gui_redraw_census);
  return enabled;
}

// A writer and the three return addresses above it.
using Key = std::array<uint32_t, 4>;

std::mutex g_mutex;
std::set<Key> g_frame;               // seen since the last renderGUI
std::map<Key, uint64_t> g_frames_with;  // frames each key was seen in
uint64_t g_frames = 0, g_dirty = 0, g_dirty_unattributed = 0;
// Re-queued frames by the set of writer groups seen in them (bit 0 moving text, 1 enable toggle,
// 2 text and child, 3 any other writer).
std::array<uint64_t, 16> g_dirty_by_groups{};

template <size_t N>
bool InRange(uint32_t address, const abi::gui_redraw::CodeRange (&ranges)[N]) {
  for (const auto& r : ranges) {
    if (address >= r.begin && address < r.end) return true;
  }
  return false;
}

unsigned Group(const Key& key) {
  namespace gr = abi::gui_redraw;
  for (size_t i = 1; i < key.size(); ++i) {
    if (InRange(key[i], gr::kGroupMovingText)) return 1u << 0;
    if (InRange(key[i], gr::kGroupEnableToggle)) return 1u << 1;
    if (InRange(key[i], gr::kGroupTextAndChild)) return 1u << 2;
  }
  return 1u << 3;
}

// The saved return address of the frame whose stack pointer is `sp`: the generated prologues store
// LR at -8 from the caller's stack pointer before `stwu r1` (e.g. sub_821A41A0), so the caller's
// frame (back chain at 0(sp)) holds it at its own -8.
uint32_t ReturnAbove(const uint8_t* base, uint32_t sp, uint32_t* next_sp) {
  if (sp == 0) return 0;
  const uint32_t back = abi::ReadU32(base, sp);
  *next_sp = back;
  return back ? abi::ReadU32(base, back - 8) : 0;
}

void Record(const uint8_t* base, uint32_t writer, const PPCContext& ctx) {
  Key key{writer, uint32_t(ctx.lr), 0, 0};
  uint32_t sp = ctx.r1.u32, next = 0;
  key[2] = ReturnAbove(base, sp, &next);
  sp = next;
  key[3] = ReturnAbove(base, sp, &next);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_frame.insert(key);
}

void Frame(const uint8_t* base) {
  const uint32_t system = abi::ReadU32(base, abi::game_ui::kCeguiSystemGlobal);
  const bool dirty =
      system && abi::ReadU8(base, system + abi::game_ui::kSystemGuiRedraw.offset) != 0;
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_frames;
  if (dirty) {
    ++g_dirty;
    if (g_frame.empty()) ++g_dirty_unattributed;
    unsigned groups = 0;
    for (const Key& key : g_frame) groups |= Group(key);
    ++g_dirty_by_groups[groups];
  }
  for (const Key& key : g_frame) ++g_frames_with[key];
  g_frame.clear();
}

void LogTotals() {
  std::map<Key, uint64_t> with;
  uint64_t frames, dirty, unattributed;
  std::array<uint64_t, 16> by_groups;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    by_groups = g_dirty_by_groups;
    g_dirty_by_groups = {};
    with.swap(g_frames_with);
    frames = g_frames;
    dirty = g_dirty;
    unattributed = g_dirty_unattributed;
    g_frames = g_dirty = g_dirty_unattributed = 0;
  }
  if (!frames) return;
  auto pct = [frames](uint64_t n) { return 100.0 * double(n) / double(frames); };
  // The call chains, most frequent first.
  std::vector<std::pair<uint64_t, Key>> chains;
  for (const auto& [key, n] : with) chains.push_back({n, key});
  std::sort(chains.rbegin(), chains.rend());
  std::string line;
  char buf[160];
  std::snprintf(buf, sizeof buf,
                "gui redraw census: %llu frames, %.1f%% re-queued the tree (%.1f%% with no "
                "writer seen)",
                (unsigned long long)frames, pct(dirty), pct(unattributed));
  line = buf;
  // M moving text, E enable toggle, T text and child, O other; "-" none seen.
  line += "\n  re-queued frames by writer groups:";
  for (unsigned g = 0; g < by_groups.size(); ++g) {
    if (!by_groups[g]) continue;
    std::string name;
    if (g & 1) name += 'M';
    if (g & 2) name += 'E';
    if (g & 4) name += 'T';
    if (g & 8) name += 'O';
    if (name.empty()) name = "-";
    std::snprintf(buf, sizeof buf, " %s %.1f%%", name.c_str(), pct(by_groups[g]));
    line += buf;
  }
  for (size_t i = 0; i < chains.size() && i < 16; ++i) {
    const Key& k = chains[i].second;
    std::snprintf(buf, sizeof buf, "\n  %5.1f%% of frames: writer %08X from %08X < %08X < %08X",
                  pct(chains[i].first), k[0], k[1], k[2], k[3]);
    line += buf;
  }
  REXLOG_INFO("{}", line);
}

struct Install {
  Install() {
    if (!torchlight::live::AddSummaryHook([] { if (Enabled()) LogTotals(); }))
      std::fprintf(stderr, "gui redraw census: no room for its frame summary hook\n");
  }
} g_install;

}  // namespace

extern "C" {

#define TORCHLIGHT_GUI_REDRAW_WRAP(addr)                         \
  REX_EXTERN(__imp__sub_##addr);                                 \
  REX_FUNC(sub_##addr) {                                         \
    if (Enabled()) Record(base, 0x##addr, ctx);                  \
    __imp__sub_##addr(ctx, base);                                \
  }
TORCHLIGHT_GUI_REDRAW_WRITERS(TORCHLIGHT_GUI_REDRAW_WRAP)
#undef TORCHLIGHT_GUI_REDRAW_WRAP

// System::renderGUI (abi::gui_redraw::kRenderGui): the flag as this frame's render finds it.
REX_EXTERN(__imp__sub_821FBC60);
REX_FUNC(sub_821FBC60) {
  if (Enabled()) Frame(base);
  __imp__sub_821FBC60(ctx, base);
}

}  // extern "C"
