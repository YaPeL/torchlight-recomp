// Diagnostics: stage 0 of the native render queue (hooks/queue_census.h). Wraps
// SceneManager::renderObjects (0x821DD1C0, the native render queue's cut) to time the guest's walk
// of each collection and classify it, and renderBasicQueueGroupObjects (0x821DD098) to know which
// of its three calls is which. Logs the totals with every 10 s frame summary ("queue census: ...").
// Reads only: the guest's walk runs unchanged. Compiled only with TORCHLIGHT_DIAGNOSTICS
// (CMakeLists.txt); then off by default (--diag_queue_census), each wrapper only calls the guest.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/ogre_layout.h"
#include "guest_abi/render_queue.h"
#include "guest_abi/static_geometry.h"
#include "hooks/queue_census.h"
#include "live/frame_timing.h"

REXCVAR_DEFINE_BOOL(diag_queue_census, false, "Torchlight",
                    "Diagnostics: classify every render queue collection against the native render "
                    "queue's stage 1 criteria and log, every 10 s, the share of draws and of the "
                    "walk's time a native path would keep (docs/perf/native-render-queue.md)");

namespace {

namespace abi = torchlight::guest_abi;
namespace rq = abi::render_queue;
using torchlight::hooks::QueueCensusTotals;
using torchlight::hooks::QueueCollection;
using torchlight::hooks::QueueReason;

bool Enabled() {
  static const bool enabled = REXCVAR_GET(diag_queue_census);
  return enabled;
}

std::mutex g_mutex;
QueueCensusTotals g_totals;
uint64_t g_other_calls = 0;  // renderObjects called from outside renderBasicQueueGroupObjects

// renderBasicQueueGroupObjects calls renderObjects three times per priority group, in this order.
// -1: not inside renderBasicQueueGroupObjects. Per thread, saved and restored around each call, so
// a nested walk (a render to texture inside a walk) does not disturb the outer one.
thread_local int g_call_in_group = -1;

double Pct(double part, double whole) { return whole > 0 ? 100.0 * part / whole : 0.0; }

void LogTotals() {
  QueueCensusTotals t;
  uint64_t other;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    t = g_totals;
    other = g_other_calls;
    g_totals = {};
    g_other_calls = 0;
  }
  char head[512];
  std::snprintf(head, sizeof head,
                "queue census: collections=%llu (mixed %llu) groups=%llu (mixed %llu, from -1.0 "
                "%llu) draws=%llu walk=%.1f ms | native whole collection: %.1f%% draws %.1f%% ms | "
                "native per group: %.1f%% draws %.1f%% ms | other calls=%llu |",
                (unsigned long long)t.collections, (unsigned long long)t.mixed_collections,
                (unsigned long long)t.groups, (unsigned long long)t.mixed_groups,
                (unsigned long long)t.groups_from_minus_one, (unsigned long long)t.draws, t.ms,
                Pct(double(t.draws_native_whole), double(t.draws)), Pct(t.ms_native_whole, t.ms),
                Pct(double(t.draws_native_group), double(t.draws)), Pct(t.ms_native_group, t.ms),
                (unsigned long long)other);
  std::string line = head;
  for (size_t r = 0; r < t.draws_by_reason.size(); ++r) {
    line += " ";
    line += torchlight::hooks::QueueReasonName(static_cast<QueueReason>(r));
    line += "=" + std::to_string(t.draws_by_reason[r]);
  }
  REXLOG_INFO("{}", line);
}

struct Install {
  Install() {
    if (!torchlight::live::AddSummaryHook([] { if (Enabled()) LogTotals(); }))
      std::fprintf(stderr, "queue census: no room for its frame summary hook\n");
  }
} g_install;

// Whether the scene manager's current viewport draws to the window (the main scene) rather than a
// render texture (the projector passes).
bool MainScene(const uint8_t* m, uint32_t scene_manager) {
  const uint32_t viewport =
      abi::ReadU32(m, scene_manager, abi::static_geometry::scene_manager::kCurrentViewport);
  if (!viewport) return false;
  const uint32_t target = abi::ReadU32(m, viewport, abi::ogre::viewport::kTarget);
  return target && abi::ReadU32(m, target) == rq::viewport_target::kRenderWindowVtable;
}

}  // namespace

extern "C" {

REX_EXTERN(__imp__sub_821DD098);
REX_FUNC(sub_821DD098) {
  static_assert(rq::kRenderBasicQueueGroupObjects == 0x821DD098u, "guest_abi mismatch");
  if (!Enabled()) {
    __imp__sub_821DD098(ctx, base);
    return;
  }
  const int outer = g_call_in_group;
  g_call_in_group = 0;
  __imp__sub_821DD098(ctx, base);
  g_call_in_group = outer;
}

REX_EXTERN(__imp__sub_821DD1C0);
REX_FUNC(sub_821DD1C0) {
  static_assert(rq::kRenderObjects == 0x821DD1C0u, "guest_abi mismatch");
  if (!Enabled()) {
    __imp__sub_821DD1C0(ctx, base);
    return;
  }
  const uint32_t scene_manager = ctx.r3.u32, collection = ctx.r4.u32, om = ctx.r5.u32;
  const int call = g_call_in_group;
  if (call >= 0) g_call_in_group = call + 1;
  const auto t0 = std::chrono::steady_clock::now();
  __imp__sub_821DD1C0(ctx, base);
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  if (call < 0) {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_other_calls;
    return;
  }
  // The walk leaves the collection as it was (Runic's grouped walk sorts a copy), so it is read
  // after the guest's walk, outside the timed part.
  const auto kind = static_cast<QueueCollection>(call % 3);
  QueueCensusTotals t;
  torchlight::hooks::ClassifyCollection(base, collection, om, MainScene(base, scene_manager), kind,
                                        ms, t);
  std::lock_guard<std::mutex> lock(g_mutex);
  g_totals.Add(t);
}

}  // extern "C"
