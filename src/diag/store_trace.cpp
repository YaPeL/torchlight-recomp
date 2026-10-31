// Diagnostics: the store trace's recorder (diag/store_trace.h, the generated code's side). Wraps the guest functions whose
// call trees are traced (SceneManager::renderSingleObject 0x821C5FA0 and SceneManager::_setPass
// 0x821C5730, guest_abi render_queue; everything they call, virtual calls and the CRT included,
// runs inside the trace) and, inside them, records every guest write the generated code makes:
// address, size, the value before and after, and the recompiled function that made it. Writes
// below the traced entry's stack pointer (the dead frames of its callees) are counted, not kept.
// Logs with every 10 s frame summary the calls, writes and the functions that write most ("store
// trace: ..."); --diag_store_trace_dump writes every write of the first traced calls to a file.
// Also recorded: every call the traced code makes to a kernel export (the link's --wrap,
// tools/diag/gen_store_trace_imports.py), with what it changed in the critical section or the
// virtual memory arguments it points to ("kernel:Name"), and the guest writes of our own hooks
// (abi::WriteBytes and its kin, and the host's guest copies, "host"). Reads only, apart from what
// the guest writes itself.
//
// The comparison (--diag_store_compare, for option B's native paths, docs/perf/native-render-
// queue.md section 4): each renderSingleObject runs twice from the same state. First the
// candidate (the guest's own function until a native path takes its place), with
// every write it makes recorded with the bytes it replaced and its commands kept apart
// (capture::Session::CompareLog, diverted); its writes are then undone in reverse and the context
// put back, and the guest's function runs as usual, recorded the same way. The two runs must leave
// the same bytes in every guest address either wrote (the stack below the call's excluded), the
// same return and non-volatile registers, and the same events, constants and draws, each draw
// with the same states set. Differences are counted in the summary and the first ones logged.
//
// Compiled only with TORCHLIGHT_DIAGNOSTICS (CMakeLists.txt), off by default (--diag_store_trace,
// --diag_store_compare).

#include "diag/store_trace_api.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string_view>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "capture/session.h"
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
REXCVAR_DEFINE_BOOL(diag_store_compare, false, "Torchlight",
                    "Diagnostics: run every renderSingleObject twice from the same state (the "
                    "candidate, undone, then the guest's) and log where they differ");
REXCVAR_DEFINE_UINT32(diag_store_compare_shown, 20, "Torchlight",
                      "Diagnostics: how many differing calls --diag_store_compare logs in detail");

namespace torchlight::diag::store_trace {

std::atomic<int> g_threads_inside{0};

namespace {

namespace abi = torchlight::guest_abi;

bool Enabled() {
  static const bool enabled = REXCVAR_GET(diag_store_trace);
  return enabled;
}

bool CompareEnabled() {
  static const bool enabled = REXCVAR_GET(diag_store_compare);
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

// A guest write's replaced bytes, to undo it (the comparison): `size` bytes at `address`, from
// `bytes`, or for a raw line (size kLine) from its snapshot in Call::lines.
struct Undo {
  uint32_t address;
  uint32_t size;
  uint64_t bytes;
};

struct Call {
  uint32_t entry = 0;  // the traced guest function
  uint32_t sp = 0;     // its stack pointer at entry
  int depth = 0;
  bool undo = false;  // the comparison: every write kept in undo_log, the stack's too
  // The comparison: what the undo cannot take back, and where (the kernel export, or the guest
  // function storing to a device register).
  const char* not_undoable = nullptr;
  const char* not_undoable_kind = nullptr;
  std::vector<Undo> undo_log;
  std::vector<Write> writes;
  uint64_t dead_stack_writes = 0;
  std::unordered_map<uint32_t, std::array<uint8_t, kLine>> lines;  // raw lines at first touch
  std::unordered_map<uint32_t, const char*> line_function;
  // Raw lines: the 16-byte granules the call accessed (bits 0-7) and whether it accessed the line's
  // start (bit 8: dcbz zeroes a whole line from it).
  std::unordered_map<uint32_t, uint16_t> line_touch;
  std::map<std::string, uint64_t> kernel_calls;
};
thread_local Call t_call;

// The comparison's race detection. While a call is compared, the render thread enters every
// 16-byte granule its two runs touch (stores, raw accesses, our hooks' writes, critical sections)
// into this table, tagged with the call's number; every other thread looks its own stores and raw
// accesses up in it, and a hit marks the call raced: another thread wrote, or accessed raw, what
// the call had touched. Entries of older calls count as empty (linear probing; an insert takes the
// first such slot, a lookup stops at it), so nothing is cleared between calls. A table too full
// marks the call raced too. Writes another thread makes before the call touches a granule are part
// of the state the runs start from; one that falls between the last check and the undo is the
// residual race (documented).
constexpr uint32_t kTouchedSlots = 1u << 16;
constexpr uint32_t kTouchedProbes = 64;
std::array<std::atomic<uint64_t>, kTouchedSlots> g_touched;  // call << 32 | granule
std::atomic<uint32_t> g_compare_call{0};  // the call being compared; 0: none
std::atomic<bool> g_raced{false};
std::atomic<uint32_t> g_raced_at{0};
std::atomic<const char*> g_raced_by{nullptr};
uint32_t g_next_call = 0;  // render thread

uint32_t TouchedSlot(uint32_t granule) { return (granule * 2654435761u) >> 16; }

void MarkRaced(uint32_t address, const char* by) {
  if (!g_raced.exchange(true, std::memory_order_acq_rel)) {
    g_raced_at.store(address, std::memory_order_relaxed);
    g_raced_by.store(by, std::memory_order_relaxed);
  }
}

// The render thread, while a call is compared: [address, address + size) touched.
void Touch(uint32_t address, uint32_t size) {
  const uint64_t call = g_compare_call.load(std::memory_order_relaxed);
  if (!call || !size) return;
  for (uint32_t g = address >> 4, last = (address + size - 1) >> 4;; ++g) {
    uint32_t slot = TouchedSlot(g);
    bool placed = false;
    for (uint32_t probe = 0; probe < kTouchedProbes; ++probe, slot = (slot + 1) % kTouchedSlots) {
      const uint64_t v = g_touched[slot].load(std::memory_order_relaxed);
      if ((v >> 32) == call && uint32_t(v) == g) {
        placed = true;
        break;
      }
      if ((v >> 32) != call) {
        g_touched[slot].store(call << 32 | g, std::memory_order_release);
        placed = true;
        break;
      }
    }
    if (!placed) MarkRaced(g << 4, "the touched table full");
    if (g == last) break;
  }
}

// Another thread, while a call is compared: its access to [address, address + size).
void Foreign(uint32_t address, uint32_t size, const char* by) {
  const uint64_t call = g_compare_call.load(std::memory_order_acquire);
  if (!call || !size) return;
  for (uint32_t g = address >> 4, last = (address + size - 1) >> 4;; ++g) {
    uint32_t slot = TouchedSlot(g);
    for (uint32_t probe = 0; probe < kTouchedProbes; ++probe, slot = (slot + 1) % kTouchedSlots) {
      const uint64_t v = g_touched[slot].load(std::memory_order_acquire);
      if ((v >> 32) != call) break;
      if (uint32_t(v) == g) {
        MarkRaced(g << 4, by);
        return;
      }
    }
    if (g == last) break;
  }
}

bool Comparing() { return g_compare_call.load(std::memory_order_acquire) != 0; }

struct Totals {
  uint64_t calls = 0, writes = 0, dead_stack_writes = 0, raw_lines_changed = 0;
  // The comparison: calls compared, those with a difference, and the differences by kind.
  uint64_t compared = 0, differing = 0, memory_differences = 0, register_differences = 0,
           command_differences = 0;
  // Calls not compared: another thread wrote what the run did (raced), or the candidate did
  // something the undo cannot take back (a kernel call that changes state, a device register).
  uint64_t raced = 0, not_undoable = 0;
  std::map<std::string, uint64_t> unverified_by;  // "reason: site"
  std::map<std::string, uint64_t> kernel_calls;
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

void HostWrite(const uint8_t* base, uint32_t address, const void* bytes, size_t size);

void Begin(uint32_t entry, uint32_t sp) {
  if (t_call.depth++ > 0) return;
  abi::GuestWriteObserver none = nullptr;
  abi::g_guest_write_observer.compare_exchange_strong(none, &HostWrite);
  t_call.entry = entry;
  t_call.sp = sp;
  t_call.writes.clear();
  t_call.dead_stack_writes = 0;
  t_call.lines.clear();
  t_call.line_function.clear();
  t_call.line_touch.clear();
  t_call.kernel_calls.clear();
  t_call.undo_log.clear();
  t_call.not_undoable = nullptr;
  t_call.not_undoable_kind = nullptr;
  g_threads_inside.fetch_add(1, std::memory_order_relaxed);
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
  g_threads_inside.fetch_sub(1, std::memory_order_relaxed);
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
  for (const auto& [name, n] : t_call.kernel_calls) g_totals.kernel_calls[name] += n;
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
  if (t.compared || t.raced || t.not_undoable) {
    const uint64_t unverified = t.raced + t.not_undoable;
    const uint64_t all = t.compared + unverified;
    std::string sites;
    uint64_t top = 0;
    for (const auto& [site, n] : t.unverified_by) {
      sites += "\n    " + site + " " + std::to_string(n);
      top = std::max(top, n);
    }
    REXLOG_INFO("store compare: {} calls compared, {} differ ({} memory bytes, {} registers, {} "
                "commands); not verified: {} ({:.3f} %: {} raced with another thread, {} not "
                "undoable){}",
                t.compared, t.differing, t.memory_differences, t.register_differences,
                t.command_differences, unverified, all ? 100.0 * double(unverified) / double(all) : 0.0,
                t.raced, t.not_undoable, sites);
    // The owner's limit (2026-10-31): over 0.1 % of the calls, or always the same site.
    if (unverified && (unverified * 1000 > all || (unverified >= 10 && top == unverified)))
      REXLOG_WARN("store compare: unverified calls over the limit (0.1 % or one site)");
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
  line += "\n  kernel calls:";
  for (const auto& [name, n] : t.kernel_calls) {
    std::snprintf(buf, sizeof buf, " %s %llu", name.c_str(), (unsigned long long)n);
    line += buf;
  }
  if (t.kernel_calls.empty()) line += " none";
  line += "\n  most writes:";
  for (size_t i = 0; i < top.size() && i < 12; ++i) {
    std::snprintf(buf, sizeof buf, " %s %llu", top[i].second.c_str(),
                  (unsigned long long)top[i].first);
    line += buf;
  }
  REXLOG_INFO("{}", line);
}

// Our own hooks' guest writes (abi::g_guest_write_observer), recorded as "host" byte runs.
void HostWrite(const uint8_t* base, uint32_t address, const void* bytes, size_t size) {
  if (t_call.depth == 0) {
    if (Comparing()) Foreign(address, uint32_t(size), "host");
    return;
  }
  if (t_call.undo) Touch(address, uint32_t(size));
  for (size_t i = 0; i < size; i += 8) {
    const size_t n = std::min<size_t>(8, size - i);
    uint64_t before = 0, after = 0;
    std::memcpy(&before, abi::xbox_memory::HostAddress(base, address + uint32_t(i)), n);
    std::memcpy(&after, static_cast<const uint8_t*>(bytes) + i, n);
    t_call.writes.push_back({address + uint32_t(i), 0, before, after, "host"});
    if (t_call.undo) t_call.undo_log.push_back({address + uint32_t(i), uint32_t(n), before});
  }
}

struct Install {
  Install() {
    if (!torchlight::live::AddSummaryHook([] { if (Enabled() || CompareEnabled()) LogTotals(); }))
      std::fprintf(stderr, "store trace: no room for its frame summary hook\n");
  }
} g_install;

}  // namespace

void Store(const uint8_t* base, uint32_t address, uint32_t size, uint64_t value,
           const char* function) {
  if (t_call.depth == 0) {  // another thread is the one inside a traced call
    if (Comparing()) Foreign(address, size, function);
    return;
  }
  if (t_call.undo) {
    uint64_t bytes = 0;
    std::memcpy(&bytes, abi::xbox_memory::HostAddress(base, address), size);
    t_call.undo_log.push_back({address, size, bytes});
  }
  if (t_call.undo && !(address < t_call.sp && t_call.sp - address <= kStackWindow))
    Touch(address, size);
  if (address < t_call.sp && t_call.sp - address <= kStackWindow) {
    ++t_call.dead_stack_writes;
    return;
  }
  const uint64_t mask = size == 8 ? ~0ull : ((1ull << (size * 8)) - 1);
  t_call.writes.push_back({address, size, ReadGuest(base, address, size), value & mask, function});
}

// Kernel exports whose effects the comparison's undo takes back: the critical sections (their
// words are recorded) and those that only read.
bool Undoable(std::string_view n) {
  static constexpr std::string_view kReadOnly[] = {
      "KeTlsGetValue", "KeGetCurrentProcessType", "KeQueryPerformanceCounter",
      "KeQueryPerformanceFrequency", "KeQuerySystemTime"};
  if (n.find("CriticalSection") != std::string_view::npos) return true;
  for (std::string_view r : kReadOnly) {
    if (n == r) return true;
  }
  return false;
}

// A kernel export the generated code calls (--wrap): its arguments that point to state it changes
// (a critical section at r3; the base address and size pointers of NtAllocateVirtualMemory and
// NtFreeVirtualMemory at r3 and r4) are compared before and after.
void Import(PPCContext& ctx, uint8_t* base, const char* name,
            void (*real)(PPCContext&, uint8_t*)) {
  if (!g_threads_inside.load(std::memory_order_relaxed)) return real(ctx, base);
  const std::string_view n(name);
  if (t_call.depth == 0) {
    // Another thread's critical section, while a compared call runs: the undo leaves its words.
    const uint32_t cs = ctx.r3.u32;
    const bool critical = n.find("CriticalSection") != std::string_view::npos;
    if (critical && Comparing()) Foreign(cs, 28, name);  // before: it may block inside
    real(ctx, base);
    if (critical && Comparing()) Foreign(cs, 28, name);
    return;
  }
  if (t_call.undo && !t_call.not_undoable && !Undoable(n)) {
    t_call.not_undoable = name;
    t_call.not_undoable_kind = "kernel call";
  }
  std::vector<std::pair<uint32_t, uint32_t>> watched;  // guest address, bytes
  if (n.find("CriticalSection") != std::string_view::npos) {
    watched.push_back({ctx.r3.u32, 28});
    if (t_call.undo) Touch(ctx.r3.u32, 28);
  }
  if (n == "NtAllocateVirtualMemory" || n == "NtFreeVirtualMemory") {
    watched.push_back({ctx.r3.u32, 4});
    watched.push_back({ctx.r4.u32, 4});
  }
  std::vector<std::array<uint8_t, 32>> before(watched.size());
  for (size_t i = 0; i < watched.size(); ++i) {
    std::memcpy(before[i].data(), abi::xbox_memory::HostAddress(base, watched[i].first),
                watched[i].second);
  }
  ++t_call.kernel_calls[name];
  real(ctx, base);
  static thread_local std::unordered_map<std::string, std::string> labels;
  auto [it, inserted] = labels.try_emplace(name, std::string("kernel:") + name);
  for (size_t i = 0; i < watched.size(); ++i) {
    const uint8_t* now = abi::xbox_memory::HostAddress(base, watched[i].first);
    for (uint32_t off = 0; off < watched[i].second; off += 4) {
      if (std::memcmp(now + off, before[i].data() + off, 4) == 0) continue;
      uint64_t b = 0, a = 0;
      std::memcpy(&b, before[i].data() + off, 4);
      std::memcpy(&a, now + off, 4);
      t_call.writes.push_back({watched[i].first + off, 0, b, a, it->second.c_str()});
      if (t_call.undo) t_call.undo_log.push_back({watched[i].first + off, 4, b});
    }
  }
}

void RawTouch(const uint8_t* base, uint32_t address, const char* function) {
  const uint32_t line = address & ~(kLine - 1);
  if (t_call.depth == 0) {
    // Another thread: its 16-byte granule, or the whole line from the line's start (dcbz).
    if (Comparing())
      Foreign(address == line ? line : address & ~15u, address == line ? kLine : 16, function);
    return;
  }
  if (t_call.undo) Touch(address == line ? line : address & ~15u, address == line ? kLine : 16);
  if (!t_call.undo && line < t_call.sp && t_call.sp - line <= kStackWindow) return;
  t_call.line_touch[line] |=
      uint16_t((1u << ((address - line) / 16)) | (address == line ? 0x100u : 0u));
  auto [it, inserted] = t_call.lines.try_emplace(line);
  if (!inserted) return;
  std::memcpy(it->second.data(), abi::xbox_memory::HostAddress(base, line), kLine);
  t_call.line_function[line] = function;
  if (t_call.undo) t_call.undo_log.push_back({line, kLine, 0});
}

void MmioStore(uint32_t, const char* function) {
  if (t_call.depth > 0 && t_call.undo && !t_call.not_undoable) {
    t_call.not_undoable = function;
    t_call.not_undoable_kind = "device register";
  }
}

namespace {

namespace cap = torchlight::capture;
namespace commands = torchlight::commands;

// One run's record: the undo log and its raw line snapshots, the merged address ranges it wrote
// with their bytes as it left them and as they were before it, who wrote what, and the context
// after it.
struct Range {
  uint32_t address, size;
};
struct Run {
  std::vector<Undo> undo_log;
  std::unordered_map<uint32_t, std::array<uint8_t, kLine>> lines;
  // Of each raw line, the bytes the call wrote: those of the granules it accessed, and from a
  // line's start (dcbz) the bytes that changed to zero. Only these are undone and compared; the
  // rest of the line may be another thread's.
  std::unordered_map<uint32_t, std::array<bool, kLine>> line_written;
  std::vector<Range> ranges;      // address order, merged
  std::vector<uint32_t> offsets;  // each range's first byte in `left` and `before`
  std::vector<uint8_t> left, before;
  struct Writer {
    uint32_t address, size;
    const char* function;
  };
  std::vector<Writer> writers;  // in order
  PPCContext out;

  // The index of the byte at `a` in `left` / `before`, or -1.
  int64_t Find(uint32_t a) const {
    auto it = std::upper_bound(ranges.begin(), ranges.end(), a,
                               [](uint32_t v, const Range& r) { return v < r.address; });
    if (it == ranges.begin()) return -1;
    --it;
    if (a - it->address >= it->size) return -1;
    return int64_t(offsets[size_t(it - ranges.begin())]) + (a - it->address);
  }
  // The function that last wrote `a`, or null.
  const char* WriterOf(uint32_t a) const {
    for (auto it = writers.rbegin(); it != writers.rend(); ++it) {
      if (a >= it->address && a - it->address < it->size) return it->function;
    }
    return nullptr;
  }
};

void StartRun(uint32_t entry, uint32_t sp) {
  t_call.undo = true;
  Begin(entry, sp);
}

// Ends the recording and reads the bytes the run left and replaced.
void StopRun(const uint8_t* base, Run& run) {
  --t_call.depth;
  g_threads_inside.fetch_sub(1, std::memory_order_relaxed);
  t_call.undo = false;
  run.undo_log = std::move(t_call.undo_log);
  run.lines = std::move(t_call.lines);
  t_call.undo_log.clear();
  t_call.lines.clear();
  for (const Write& w : t_call.writes) {
    run.writers.push_back({w.address, w.size ? w.size : 8u, w.function});
  }
  for (const auto& [line, function] : t_call.line_function) {
    run.writers.push_back({line, kLine, function});
  }
  std::vector<Range> all;
  all.reserve(run.undo_log.size());
  for (const Undo& u : run.undo_log) {
    if (u.size != kLine) {
      all.push_back({u.address, u.size});
      continue;
    }
    const uint16_t touch = t_call.line_touch[u.address];
    const uint8_t* now = abi::xbox_memory::HostAddress(base, u.address);
    const auto& snapshot = run.lines.at(u.address);
    auto& written = run.line_written[u.address];
    for (uint32_t i = 0; i < kLine; ++i) {
      written[i] = ((touch >> (i / 16)) & 1) ||
                   ((touch & 0x100) && now[i] == 0 && snapshot[i] != 0);
      if (written[i]) all.push_back({u.address + i, 1});
    }
  }
  t_call.line_touch.clear();
  std::sort(all.begin(), all.end(),
            [](const Range& a, const Range& b) { return a.address < b.address; });
  for (const Range& r : all) {
    if (!run.ranges.empty()) {
      Range& last = run.ranges.back();
      if (uint64_t(r.address) <= uint64_t(last.address) + last.size) {
        last.size = uint32_t(std::max<uint64_t>(uint64_t(last.address) + last.size,
                                                uint64_t(r.address) + r.size) -
                             last.address);
        continue;
      }
    }
    run.ranges.push_back(r);
  }
  uint32_t total = 0;
  for (const Range& r : run.ranges) {
    run.offsets.push_back(total);
    total += r.size;
  }
  run.left.resize(total);
  run.before.resize(total);
  for (size_t i = 0; i < run.ranges.size(); ++i) {
    std::memcpy(run.left.data() + run.offsets[i],
                abi::xbox_memory::HostAddress(base, run.ranges[i].address), run.ranges[i].size);
  }
  // Newest first, so the oldest replaced bytes stay.
  for (auto it = run.undo_log.rbegin(); it != run.undo_log.rend(); ++it) {
    if (it->size == kLine) {
      const auto& snapshot = run.lines.at(it->address);
      const auto& written = run.line_written.at(it->address);
      for (uint32_t i = 0; i < kLine; ++i) {
        if (written[i]) run.before[size_t(run.Find(it->address + i))] = snapshot[i];
      }
      continue;
    }
    uint8_t bytes[8];
    std::memcpy(bytes, &it->bytes, 8);
    std::memcpy(run.before.data() + run.Find(it->address), bytes, it->size);
  }
}



// Puts back what the run replaced, newest first (of a raw line only the bytes the run wrote).
void UndoRun(uint8_t* base, const Run& run) {
  for (auto it = run.undo_log.rbegin(); it != run.undo_log.rend(); ++it) {
    uint8_t* to = abi::xbox_memory::HostAddress(base, it->address);
    uint8_t bytes[8];
    std::memcpy(bytes, &it->bytes, 8);
    const bool line = it->size == kLine;
    const uint8_t* from = line ? run.lines.at(it->address).data() : bytes;
    for (uint32_t i = 0; i < it->size; ++i) {
      if (line && !run.line_written.at(it->address)[i]) continue;
      to[i] = from[i];
    }
  }
}

std::string Describe(const cap::Session::CompareValue& v) {
  if (!v.payload) return "absent";
  char buf[48];
  std::snprintf(buf, sizeof buf, "opcode %u object %08X", unsigned(commands::OpcodeOf(*v.payload)),
                v.object);
  return buf;
}

uint32_t g_shown = 0;

// Compares the candidate's run with the guest's; returns the differences, and logs the first
// differing calls.
void CompareRuns(const uint8_t* base, uint32_t entry, uint32_t sp, const Run& candidate,
                 const Run& original, const cap::Session::CompareLog& candidate_log,
                 const cap::Session::CompareLog& original_log) {
  std::vector<std::string> notes;
  uint64_t memory = 0, registers = 0, commands_differ = 0;
  auto note = [&](std::string text) {
    if (notes.size() < 12) notes.push_back(std::move(text));
  };
  auto on_stack = [&](uint32_t a) { return a < sp && sp - a <= kStackWindow; };

  // Memory: every byte either run wrote. The guest's run is what memory holds now; the candidate
  // left `left`, or, where it wrote nothing, the byte from before the call. Differing bytes are
  // reported in runs, with the function that wrote them last in each run ("none": not written in
  // that run, so by another thread or not at all).
  std::vector<Range> ranges = candidate.ranges;
  ranges.insert(ranges.end(), original.ranges.begin(), original.ranges.end());
  std::sort(ranges.begin(), ranges.end(),
            [](const Range& a, const Range& b) { return a.address < b.address; });
  uint32_t next = 0;  // first address not yet compared
  uint32_t run_start = 0, run_size = 0;
  std::string run_want, run_now;
  auto flush = [&] {
    if (!run_size) return;
    const char* cw = candidate.WriterOf(run_start);
    const char* ow = original.WriterOf(run_start);
    char buf[160];
    std::snprintf(buf, sizeof buf, "memory %08X+%u: candidate %s (%s), guest %s (%s)", run_start,
                  run_size, run_want.c_str(), cw ? cw : "none", run_now.c_str(), ow ? ow : "none");
    note(buf);
    run_size = 0;
    run_want.clear();
    run_now.clear();
  };
  for (const Range& r : ranges) {
    uint32_t a = std::max(r.address, next);
    const uint64_t end = uint64_t(r.address) + r.size;
    for (; a < end; ++a) {
      if (on_stack(a)) continue;
      const uint8_t now = *abi::xbox_memory::HostAddress(base, a);
      const int64_t ci = candidate.Find(a);
      const uint8_t want =
          ci >= 0 ? candidate.left[size_t(ci)] : original.before[size_t(original.Find(a))];
      if (want == now) continue;
      ++memory;
      if (run_size && a != run_start + run_size) flush();
      if (!run_size) run_start = a;
      if (run_size < 16) {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%02X", want);
        run_want += hex;
        std::snprintf(hex, sizeof hex, "%02X", now);
        run_now += hex;
      }
      ++run_size;
    }
    next = uint32_t(std::max<uint64_t>(next, end));
  }
  flush();

  // Registers: the stack pointer and the return values. The generated code keeps the non-volatile
  // registers in locals (REX_CONFIG_NON_VOLATILE_AS_LOCAL), never in the context.
  const PPCContext& x = candidate.out;
  const PPCContext& y = original.out;
  auto reg = [&](const char* name, uint64_t a, uint64_t b) {
    if (a == b) return;
    ++registers;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s: candidate %016llX, guest %016llX", name,
                  (unsigned long long)a, (unsigned long long)b);
    note(buf);
  };
#define TL_REG(r) reg(#r, x.r.u64, y.r.u64)
  TL_REG(r1);
  TL_REG(r3);
  TL_REG(f1);
#undef TL_REG

  // Commands: the same events, constants and draws in order; at each draw, and at the end, the
  // same value for every state either run set (a state one run did not set has its value from
  // before the call, which both logs keep).
  auto before_call = [&](uint32_t key) {
    if (auto it = original_log.keys.find(key); it != original_log.keys.end()) return it->second.before;
    return candidate_log.keys.at(key).before;
  };
  auto same_states = [&](const std::map<uint32_t, cap::Session::CompareValue>& a,
                         const std::map<uint32_t, cap::Session::CompareValue>& b,
                         const char* where) {
    std::vector<uint32_t> keys;
    for (const auto& [k, v] : a) keys.push_back(k);
    for (const auto& [k, v] : b) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    for (uint32_t k : keys) {
      auto ia = a.find(k);
      auto ib = b.find(k);
      const auto va = ia != a.end() ? ia->second : before_call(k);
      const auto vb = ib != b.end() ? ib->second : before_call(k);
      if (va == vb) continue;
      ++commands_differ;
      char buf[160];
      std::snprintf(buf, sizeof buf, "state %08X %s: candidate %s, guest %s", k, where,
                    Describe(va).c_str(), Describe(vb).c_str());
      note(buf);
    }
  };
  const auto& ci = candidate_log.items;
  const auto& oi = original_log.items;
  if (ci.size() != oi.size()) {
    ++commands_differ;
    note("commands: candidate " + std::to_string(ci.size()) + ", guest " +
         std::to_string(oi.size()));
  }
  for (size_t i = 0; i < std::min(ci.size(), oi.size()); ++i) {
    if (!(ci[i].payload == oi[i].payload) || ci[i].live_keys != oi[i].live_keys) {
      ++commands_differ;
      note("command " + std::to_string(i) + ": candidate opcode " +
           std::to_string(commands::OpcodeOf(ci[i].payload)) + ", guest opcode " +
           std::to_string(commands::OpcodeOf(oi[i].payload)));
    }
    if (std::holds_alternative<commands::Draw>(oi[i].payload)) {
      same_states(ci[i].state, oi[i].state, ("at command " + std::to_string(i)).c_str());
    }
  }
  std::map<uint32_t, cap::Session::CompareValue> end_c, end_o;
  for (const auto& [k, v] : candidate_log.keys) end_c.emplace(k, v.now);
  for (const auto& [k, v] : original_log.keys) end_o.emplace(k, v.now);
  same_states(end_c, end_o, "at the end");

  const bool differs = memory || registers || commands_differ;
  bool show = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_totals.compared;
    ++g_totals.calls_by_entry[entry];
    g_totals.differing += differs;
    g_totals.memory_differences += memory;
    g_totals.register_differences += registers;
    g_totals.command_differences += commands_differ;
    if (differs && g_shown < REXCVAR_GET(diag_store_compare_shown)) {
      ++g_shown;
      show = true;
    }
  }
  if (!show) return;
  std::string text;
  for (const std::string& n : notes) text += "\n  " + n;
  REXLOG_INFO("store compare: {:08X} differs ({} memory bytes, {} registers, {} commands, sp "
              "{:08X}):{}",
              entry, memory, registers, commands_differ, sp, text);
}

// A call that is not verified: counted by reason and site, the first ones logged.
void Unverified(uint32_t entry, const char* reason, const std::string& site) {
  bool show = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ++g_totals.calls_by_entry[entry];
    if (std::string_view(reason) == "raced") {
      ++g_totals.raced;
    } else {
      ++g_totals.not_undoable;
    }
    show = ++g_totals.unverified_by[std::string(reason) + ": " + site] == 1;
  }
  if (show) REXLOG_INFO("store compare: {:08X} not verified ({}: {})", entry, reason, site);
}


// Where the call raced: the granule and who touched it from another thread.
std::string RaceSite() {
  const char* by = g_raced_by.load(std::memory_order_relaxed);
  char buf[160];
  std::snprintf(buf, sizeof buf, "%08X by %s", g_raced_at.load(std::memory_order_relaxed),
                by ? by : "?");
  return buf;
}

void CompareCall(PPCContext& ctx, uint8_t* base, uint32_t entry, GuestFunction candidate,
                 GuestFunction original) {
  cap::Session& session = cap::Session::Get();
  const PPCContext saved = ctx;
  const uint32_t sp = ctx.r1.u32;
  // Other threads look their accesses up from here to the end of the guest's run (the count of
  // threads inside keeps their stores calling in between the runs too).
  if (++g_next_call == 0) ++g_next_call;
  g_raced.store(false, std::memory_order_relaxed);
  g_compare_call.store(g_next_call, std::memory_order_release);
  g_threads_inside.fetch_add(1, std::memory_order_acq_rel);
  auto finish = [] {
    g_compare_call.store(0, std::memory_order_release);
    g_threads_inside.fetch_sub(1, std::memory_order_acq_rel);
  };

  cap::Session::CompareLog candidate_log;
  candidate_log.divert = true;
  Run candidate_run;
  session.SetCompareLog(&candidate_log);
  StartRun(entry, sp);
  candidate(ctx, base);
  const char* not_undoable = t_call.not_undoable;
  const char* not_undoable_kind = t_call.not_undoable_kind;
  StopRun(base, candidate_run);
  session.SetCompareLog(nullptr);

  // What cannot be undone cleanly is kept: the candidate's memory and context stay and its
  // commands go to the session as if they had not been diverted (nothing runs twice).
  if (not_undoable || g_raced.load(std::memory_order_acquire)) {
    session.ReplayDiverted(candidate_log);
    const std::string site = not_undoable ? std::string(not_undoable) : RaceSite();
    finish();
    Unverified(entry, not_undoable ? not_undoable_kind : "raced", site);
    return;
  }
  candidate_run.out = ctx;
  UndoRun(base, candidate_run);

  ctx = saved;
  cap::Session::CompareLog original_log;
  Run original_run;
  session.SetCompareLog(&original_log);
  StartRun(entry, sp);
  original(ctx, base);
  StopRun(base, original_run);
  session.SetCompareLog(nullptr);
  original_run.out = ctx;
  // The guest's run is kept either way; another thread touching what either run did leaves the
  // comparison unverified.
  const bool raced = g_raced.load(std::memory_order_acquire);
  const std::string site = raced ? RaceSite() : std::string();
  finish();
  if (raced) {
    Unverified(entry, "raced", site);
    return;
  }

  CompareRuns(base, entry, sp, candidate_run, original_run, candidate_log, original_log);
}

}  // namespace

bool RenderSingleObject(PPCContext& ctx, uint8_t* base, GuestFunction candidate,
                        GuestFunction original) {
  if (CompareEnabled() && t_call.depth == 0) {
    CompareCall(ctx, base, 0x821C5FA0, candidate, original);
    return true;
  }
  if (!Enabled()) return false;
  Begin(0x821C5FA0, ctx.r1.u32);
  original(ctx, base);
  End(base);
  return true;
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
// SceneManager::_setPass (guest_abi render_queue).
TORCHLIGHT_STORE_TRACE_ENTRY(821C5730)
#undef TORCHLIGHT_STORE_TRACE_ENTRY

// renderSingleObject's entry is hooks/native_render_object.cpp, which calls RenderSingleObject.

}  // extern "C"
