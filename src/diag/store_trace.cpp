// Diagnostics: the store trace's recorder (diag/store_trace.h, the generated code's side). Wraps the guest functions whose
// call trees are traced (SceneManager::renderSingleObject 0x821C5FA0 and SceneManager::_setPass
// 0x821C5730, guest_abi render_queue; everything they call, virtual calls and the CRT included,
// runs inside the trace) and, inside them, records every guest write the generated code makes:
// address, size, the value before and after, and the recompiled function that made it. Writes
// below the traced entry's stack pointer (the dead frames of its callees) are counted, not kept.
// Logs with every 10 s frame summary the calls, writes and the functions that write most ("store
// trace: ..."); --diag_store_trace_dump writes every write of the first traced calls to a file.
// Not traced yet: guest memory written by SDK kernel exports and by our own hooks (abi::Write*),
// the next step's. Reads only, apart from what the guest writes itself. Compiled only with
// TORCHLIGHT_DIAGNOSTICS (CMakeLists.txt), off by default (--diag_store_trace).

#include "diag/store_trace_api.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/ogre_layout.h"
#include "live/frame_timing.h"

REXCVAR_DEFINE_BOOL(diag_store_trace, false, "Torchlight",
                    "Diagnostics: record every guest write inside renderSingleObject and _setPass "
                    "(and everything they call) and log a summary every 10 s");
REXCVAR_DEFINE_STRING(diag_store_trace_dump, "", "Torchlight",
                      "Diagnostics: write every write of the first traced calls (see "
                      "--diag_store_trace_dump_calls) to this file");
REXCVAR_DEFINE_UINT32(diag_store_trace_dump_calls, 20, "Torchlight",
                      "Diagnostics: how many traced calls --diag_store_trace_dump writes");

namespace torchlight::diag::store_trace {

std::atomic<bool> g_active{false};

namespace {

namespace abi = torchlight::guest_abi;

bool Enabled() {
  static const bool enabled = REXCVAR_GET(diag_store_trace);
  return enabled;
}

// How far below the traced entry's stack pointer a write counts as a dead callee frame.
constexpr uint32_t kStackWindow = 1u << 20;
constexpr uint32_t kLine = 128;

struct Write {
  uint32_t address;
  uint32_t size;  // 1, 2, 4, 8; 0 for a raw line's changed byte run (old/new hold its first word)
  uint64_t before, after;
  const char* function;
};

struct Call {
  uint32_t entry = 0;  // the traced guest function
  uint32_t sp = 0;     // its stack pointer at entry
  int depth = 0;
  std::vector<Write> writes;
  uint64_t dead_stack_writes = 0;
  std::unordered_map<uint32_t, std::array<uint8_t, kLine>> lines;  // raw lines at first touch
  std::unordered_map<uint32_t, const char*> line_function;
};
thread_local Call t_call;

std::atomic<int> g_threads_inside{0};

struct Totals {
  uint64_t calls = 0, writes = 0, dead_stack_writes = 0, raw_lines_changed = 0;
  std::map<uint32_t, uint64_t> calls_by_entry;
  std::map<std::string, uint64_t> writes_by_function;
};
std::mutex g_mutex;
Totals g_totals;
uint32_t g_dumped_calls = 0;

uint64_t ReadGuest(const uint8_t* base, uint32_t address, uint32_t size) {
  switch (size) {
    case 1: return abi::ReadU8(base, address);
    case 2: return abi::ReadU16(base, address);
    case 4: return abi::ReadU32(base, address);
    default: return abi::ReadU64(base, address);
  }
}

void Begin(uint32_t entry, uint32_t sp) {
  if (t_call.depth++ > 0) return;
  t_call.entry = entry;
  t_call.sp = sp;
  t_call.writes.clear();
  t_call.dead_stack_writes = 0;
  t_call.lines.clear();
  t_call.line_function.clear();
  if (g_threads_inside.fetch_add(1) == 0) g_active.store(true, std::memory_order_relaxed);
}

void Dump(const Call& call) {
  const std::string path = REXCVAR_GET(diag_store_trace_dump);
  if (path.empty()) return;
  std::FILE* f = std::fopen(path.c_str(), g_dumped_calls == 0 ? "w" : "a");
  if (!f) return;
  std::fprintf(f, "call %u: entry %08X sp %08X, %zu writes, %llu to dead stack frames\n",
               g_dumped_calls, call.entry, call.sp, call.writes.size(),
               (unsigned long long)call.dead_stack_writes);
  for (const Write& w : call.writes) {
    if (w.size == 0) {
      std::fprintf(f, "  %08X raw  %016llX -> %016llX  %s\n", w.address,
                   (unsigned long long)w.before, (unsigned long long)w.after, w.function);
    } else {
      std::fprintf(f, "  %08X u%-2u %016llX -> %016llX  %s\n", w.address, w.size * 8,
                   (unsigned long long)w.before, (unsigned long long)w.after, w.function);
    }
  }
  std::fclose(f);
}

void End(const uint8_t* base) {
  if (--t_call.depth > 0) return;
  if (g_threads_inside.fetch_sub(1) == 1) g_active.store(false, std::memory_order_relaxed);
  // Raw lines (vector stores, dcbz, atomics): the 8-byte words that changed since the first touch.
  uint64_t raw_changed = 0;
  for (const auto& [line, before] : t_call.lines) {
    const uint8_t* now = abi::xbox_memory::HostAddress(base, line);
    if (std::memcmp(now, before.data(), kLine) == 0) continue;
    ++raw_changed;
    for (uint32_t i = 0; i < kLine; i += 8) {
      if (std::memcmp(now + i, before.data() + i, 8) == 0) continue;
      uint64_t b, a;
      std::memcpy(&b, before.data() + i, 8);
      std::memcpy(&a, now + i, 8);
      t_call.writes.push_back({line + i, 0, b, a, t_call.line_function[line]});
    }
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  ++g_totals.calls;
  ++g_totals.calls_by_entry[t_call.entry];
  g_totals.writes += t_call.writes.size();
  g_totals.dead_stack_writes += t_call.dead_stack_writes;
  g_totals.raw_lines_changed += raw_changed;
  for (const Write& w : t_call.writes) ++g_totals.writes_by_function[w.function];
  if (g_dumped_calls < REXCVAR_GET(diag_store_trace_dump_calls)) {
    Dump(t_call);
    ++g_dumped_calls;
  }
}

void LogTotals() {
  Totals t;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    t = std::move(g_totals);
    g_totals = {};
  }
  if (!t.calls) return;
  std::vector<std::pair<uint64_t, std::string>> top;
  for (const auto& [function, n] : t.writes_by_function) top.push_back({n, function});
  std::sort(top.rbegin(), top.rend());
  std::string line;
  char buf[200];
  std::snprintf(buf, sizeof buf,
                "store trace: %llu traced calls, %llu writes kept (%.1f a call), %llu to dead "
                "stack frames, %llu raw lines changed; calls:",
                (unsigned long long)t.calls, (unsigned long long)t.writes,
                double(t.writes) / double(t.calls), (unsigned long long)t.dead_stack_writes,
                (unsigned long long)t.raw_lines_changed);
  line = buf;
  for (const auto& [entry, n] : t.calls_by_entry) {
    std::snprintf(buf, sizeof buf, " %08X %llu", entry, (unsigned long long)n);
    line += buf;
  }
  line += "\n  most writes:";
  for (size_t i = 0; i < top.size() && i < 12; ++i) {
    std::snprintf(buf, sizeof buf, " %s %llu", top[i].second.c_str(),
                  (unsigned long long)top[i].first);
    line += buf;
  }
  REXLOG_INFO("{}", line);
}

struct Install {
  Install() {
    if (!torchlight::live::AddSummaryHook([] { if (Enabled()) LogTotals(); }))
      std::fprintf(stderr, "store trace: no room for its frame summary hook\n");
  }
} g_install;

}  // namespace

void Store(const uint8_t* base, uint32_t address, uint32_t size, uint64_t value,
           const char* function) {
  if (t_call.depth == 0) return;  // another thread is the one inside a traced call
  if (address < t_call.sp && t_call.sp - address <= kStackWindow) {
    ++t_call.dead_stack_writes;
    return;
  }
  const uint64_t mask = size == 8 ? ~0ull : ((1ull << (size * 8)) - 1);
  t_call.writes.push_back({address, size, ReadGuest(base, address, size), value & mask, function});
}

void RawTouch(const uint8_t* base, uint32_t address, const char* function) {
  if (t_call.depth == 0) return;
  const uint32_t line = address & ~(kLine - 1);
  if (line < t_call.sp && t_call.sp - line <= kStackWindow) return;
  auto [it, inserted] = t_call.lines.try_emplace(line);
  if (!inserted) return;
  std::memcpy(it->second.data(), abi::xbox_memory::HostAddress(base, line), kLine);
  t_call.line_function[line] = function;
}

}  // namespace torchlight::diag::store_trace

extern "C" {

#define TORCHLIGHT_STORE_TRACE_ENTRY(addr)                                    \
  REX_EXTERN(__imp__sub_##addr);                                              \
  REX_FUNC(sub_##addr) {                                                      \
    namespace st = torchlight::diag::store_trace;                             \
    if (!st::Enabled()) return __imp__sub_##addr(ctx, base);                  \
    st::Begin(0x##addr, ctx.r1.u32);                                          \
    __imp__sub_##addr(ctx, base);                                             \
    st::End(base);                                                            \
  }
// SceneManager::renderSingleObject and SceneManager::_setPass (guest_abi render_queue).
TORCHLIGHT_STORE_TRACE_ENTRY(821C5FA0)
TORCHLIGHT_STORE_TRACE_ENTRY(821C5730)
#undef TORCHLIGHT_STORE_TRACE_ENTRY

}  // extern "C"
