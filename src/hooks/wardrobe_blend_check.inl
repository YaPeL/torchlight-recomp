// --diag_wardrobe_blend_check (diagnostics builds; included by wardrobe_blend_hooks.cpp): each item
// image's loop computed on the host into a copy (the game's memory read, the writes kept aside),
// then the recompiled loop runs on the game's memory; each of its packs is compared, in order, with
// the host's (the destination and the bytes written), and after the last one the stack
// temporaries the host loop leaves (they are final before that pack: nothing in the loop writes
// them after it). Counted per wardrobe build (LogWardrobeBlendCheck).

#include <algorithm>
#include <array>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct ExpectedPack {
  uint32_t dest = 0;
  std::array<uint8_t, 4> bytes{};
  uint8_t size = 0;
};

struct Pending {
  bool active = false;
  std::vector<ExpectedPack> packs;
  size_t next = 0;
  std::vector<std::pair<uint32_t, uint8_t>> temporaries;
};

struct Counts {
  uint64_t layers = 0;
  uint64_t fallbacks = 0;
  uint64_t pixels = 0;
  uint64_t pixel_differences = 0;
  uint64_t temporary_differences = 0;
  uint64_t incomplete = 0;
};

std::mutex g_check_mutex;
Pending g_pending;
Counts g_counts;

constexpr int kLoggedDifferences = 10;

// The host loop over the game's memory with its writes kept aside.
struct CheckMemory {
  uint8_t* base;
  PPCFPSCRRegister& fpscr;
  uint8_t pixel_size;
  std::vector<ExpectedPack>& packs;
  std::unordered_map<uint32_t, uint8_t> written;
  uint8_t Load8(uint32_t a) const {
    const auto it = written.find(a);
    return it != written.end() ? it->second : *Guest(base, a);
  }
  void Store8(uint32_t a, uint8_t v) { written[a] = v; }
  void BeforeBlend() { fpscr.disableFlushMode(); }
  void Packed(uint32_t dest) {
    ExpectedPack pack;
    pack.dest = dest;
    pack.size = pixel_size;
    for (uint8_t i = 0; i < pack.size; ++i) pack.bytes[i] = Load8(dest + i);
    packs.push_back(pack);
  }
};

// Under g_check_mutex.
void FinishPending() {
  if (g_pending.active && g_pending.next < g_pending.packs.size()) {
    ++g_counts.incomplete;
    REXLOG_ERROR("diag wardrobe blend check: the recompiled loop packed {} pixels, the host {}", g_pending.next,
                 g_pending.packs.size());
  }
  g_pending = Pending{};
}

bool CheckLayer(uint8_t* base, const WardrobeBlendLayer* layer, PPCFPSCRRegister& fpscr) {
  std::lock_guard lock(g_check_mutex);
  FinishPending();
  if (!layer) {
    ++g_counts.fallbacks;
    return false;
  }
  ++g_counts.layers;
  CheckMemory memory{base, fpscr, static_cast<uint8_t>(std::min<uint32_t>(layer->base_format.elem_bytes, 4)),
                     g_pending.packs, {}};
  torchlight::hooks::BlendWardrobeLayer(memory, *layer);
  for (const auto& [address, value] : memory.written)
    if (address - layer->sp < blend::stack::kFrameSize) g_pending.temporaries.emplace_back(address, value);
  g_pending.active = true;
  return true;
}

}  // namespace

void torchlight::hooks::WardrobeBlendCheckPacked(uint8_t* base, uint32_t dest) {
  if (!REXCVAR_GET(diag_wardrobe_blend_check)) return;
  std::lock_guard lock(g_check_mutex);
  if (!g_pending.active) return;
  const ExpectedPack& want = g_pending.packs[g_pending.next++];
  ++g_counts.pixels;
  bool same = want.dest == dest;
  for (uint8_t i = 0; same && i < want.size; ++i) same = *Guest(base, dest + i) == want.bytes[i];
  if (!same && g_counts.pixel_differences++ < kLoggedDifferences)
    REXLOG_ERROR("diag wardrobe blend check: pixel {} at {:08X} (host {:08X}): {:02X}{:02X}{:02X}{:02X} vs host "
                 "{:02X}{:02X}{:02X}{:02X}",
                 g_pending.next - 1, dest, want.dest, *Guest(base, dest), *Guest(base, dest + 1),
                 *Guest(base, dest + 2), *Guest(base, dest + 3), want.bytes[0], want.bytes[1], want.bytes[2],
                 want.bytes[3]);
  if (g_pending.next < g_pending.packs.size()) return;
  for (const auto& [address, value] : g_pending.temporaries)
    if (*Guest(base, address) != value && g_counts.temporary_differences++ < kLoggedDifferences)
      REXLOG_ERROR("diag wardrobe blend check: stack temporary at {:08X}: {:02X} vs host {:02X}", address,
                   *Guest(base, address), value);
  g_pending = Pending{};
}

void torchlight::hooks::LogWardrobeBlendCheck() {
  if (!REXCVAR_GET(diag_wardrobe_blend_check)) return;
  std::lock_guard lock(g_check_mutex);
  FinishPending();
  if (!g_counts.layers && !g_counts.fallbacks) return;
  REXLOG_INFO("diag wardrobe blend check: {} layers ({} left to the recompiled loop), {} pixels, {} pixel "
              "differences, {} stack temporary differences, {} incomplete (native_pixel_util={})",
              g_counts.layers, g_counts.fallbacks, g_counts.pixels, g_counts.pixel_differences,
              g_counts.temporary_differences, g_counts.incomplete, NativePixelUtilEnabled());
  g_counts = Counts{};
}
