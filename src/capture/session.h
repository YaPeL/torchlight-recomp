// Frame capture session: resource registry, state shadow, slot counters and the command stream.
//
// Hooks call into the session from the guest render thread (state, draws, swap) and from any
// thread (resource construction/destruction). While no capture is armed the hooks only update
// the counters, the registry and the state shadow; commands are appended only while armed.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <unordered_set>
#include <span>
#include <set>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "capture/resource_registry.h"
#include "commands/types.h"
#include "live/frame_queue.h"
#include "live/measured_mutex.h"
#include "live/snapshot_store.h"

namespace torchlight::capture {

inline constexpr uint32_t kRenderSystemSlotCount = 127;

// Time the guest threads spend recording (live mode and armed captures), by section. Sections
// nest: kCommands is all recording done in the RenderSystem hooks, including kConstants and the
// render thread's part of kResources.
enum class ProducerSection : uint32_t {
  kConstants,  // bindGpuProgramParameters: reading and recording the uploaded constants
  kCommands,   // every RenderSystem hook's recording, draws included
  kResources,  // content snapshots: version lookup and copy (RecordContent)
  kMarking,    // content versions: lock / unlock / blit (any thread)
  kCut,        // cutting and queueing the live frame
  kCount
};
struct ProducerTimes {
  std::array<std::atomic<uint64_t>, size_t(ProducerSection::kCount)> ns{};
  std::atomic<uint64_t> frames{0};    // live frames cut
  std::atomic<uint64_t> measured{0};  // of those, measured (Session::measuring)
  // The sections that do not nest (kCommands, kMarking, kCut), never reset: the per-frame
  // recording time of the slow frame report is its difference between cuts.
  std::atomic<uint64_t> top_level_ns{0};
};
// The recording cost is timed on one frame in this many (the timers themselves cost ~0.1 us per
// hook, ~0.7 ms per frame when every hook of every frame was timed); summaries divide by the
// frames measured.
inline constexpr uint64_t kMeasureEveryFrames = 8;
// Adds the scope's duration to a section when `enabled`.
class ProducerTimer {
 public:
  ProducerTimer(ProducerTimes& times, ProducerSection section, bool enabled)
      : times_(times), section_(section), enabled_(enabled) {
    if (enabled_) start_ = std::chrono::steady_clock::now();
  }
  ~ProducerTimer() {
    if (!enabled_) return;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - start_)
                  .count();
    times_.ns[size_t(section_)].fetch_add(uint64_t(ns), std::memory_order_relaxed);
    if (section_ == ProducerSection::kCommands || section_ == ProducerSection::kMarking ||
        section_ == ProducerSection::kCut) {
      times_.top_level_ns.fetch_add(uint64_t(ns), std::memory_order_relaxed);
    }
  }

 private:
  ProducerTimes& times_;
  ProducerSection section_;
  bool enabled_;
  std::chrono::steady_clock::time_point start_;
};

// Recording cost per hook (live mode and armed captures): the hook's own work, without the
// original guest function. RenderSystem hooks are indexed by vtable slot; the others follow.
enum class Hook : uint32_t {
  kLock = kRenderSystemSlotCount,  // HardwareBuffer::lock
  kUnlock,                         // HardwareBuffer::unlock
  kBlitFromMemory,                 // D3D9HardwarePixelBuffer::blitFromMemory
  kBlitToMemory,                   // D3D9HardwarePixelBuffer::blitToMemory
  kTextureLoad,                    // D3D9Texture::loadImpl
  kResourceLifetime,               // buffer/texture/declaration constructors and destructors
  kCreateGpuProgram,               // RTSS createGpuProgram
  kSwap,                           // the device swap (frame cut included)
  kEnd
};
inline constexpr uint32_t kHookCount = uint32_t(Hook::kEnd);
const char* HookName(uint32_t hook);
struct HookCost {
  std::atomic<uint64_t> calls{0}, ns{0};
};
using HookCosts = std::array<HookCost, kHookCount>;
// Adds the scope's duration to a hook when `enabled`.
class HookTimer {
 public:
  HookTimer(HookCosts& costs, uint32_t hook, bool enabled)
      : cost_(costs[hook]), enabled_(enabled) {
    if (enabled_) start_ = std::chrono::steady_clock::now();
  }
  ~HookTimer() {
    if (!enabled_) return;
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                  std::chrono::steady_clock::now() - start_)
                  .count();
    cost_.calls.fetch_add(1, std::memory_order_relaxed);
    cost_.ns.fetch_add(uint64_t(ns), std::memory_order_relaxed);
  }

 private:
  HookCost& cost_;
  bool enabled_;
  std::chrono::steady_clock::time_point start_;
};

// What comes back from the GPU side, counted for the live mode's summary (any thread, always).
struct GpuEventCounts {
  // CPU reads of render targets: locks that can read (not discard / no-overwrite) of a render
  // target's pixel buffer, and blitToMemory from one. Nothing renders guest render targets in
  // --native_live=only, so these would read stale memory.
  std::atomic<uint64_t> render_target_locks{0}, render_target_blits_to_memory{0};
};

using ReferenceImageProvider = std::function<bool(commands::ReferenceImage& image)>;


class Session {
 public:
  static Session& Get();

  // Configuration (main thread, before the game runs).
  void Configure(std::string output_dir, uint32_t frames);
  void SetReferenceImageProvider(ReferenceImageProvider provider);
  // F9 (any thread). Ignored while a capture is in progress or without an output directory.
  void RequestCapture();

  bool armed() const { return armed_.load(std::memory_order_relaxed); }
  // Live mode (--native_live): every frame is also recorded into a LiveFrame, cut at the swap and
  // pushed to `queue`; content snapshots go to `store`. Main thread, before the game runs.
  void EnableLive(live::FrameQueue* queue, live::SnapshotStore* store);
  bool live() const { return live_queue_ != nullptr; }
  // Commands and resource descriptions are being recorded (a capture is armed or live is on).
  bool recording() const { return armed() || live(); }
  // Recording, on a frame whose recording cost is timed (kMeasureEveryFrames).
  bool measuring() const {
    return recording() && measure_frame_.load(std::memory_order_relaxed);
  }
  ProducerTimes& producer_times() { return producer_times_; }
  HookCosts& hook_costs() { return hook_costs_; }
  GpuEventCounts& gpu_events() { return gpu_events_; }

  // Slot counters (any thread, always).
  void CountSlot(uint32_t slot) {
    slot_counts_[slot].fetch_add(1, std::memory_order_relaxed);
  }

  // State: always kept in the shadow under `key`; appended to the capture while armed and to the
  // live frame while live.
  // `object` is the guest texture/declaration/program the state refers to, re-read when a
  // capture starts so the baseline carries full resource descriptions.
  void State(uint32_t key, commands::CommandPayload payload, uint32_t object = 0);
  void set_membase(const uint8_t* membase) { membase_ = membase; }
  // Drops a shadow entry (e.g. a program stage that was unbound).
  void EraseState(uint32_t key);

  // Early out for state hooks whose command depends only on raw words: register values and words
  // read from guest memory, never pointers to resources (the guest can free one and create
  // another at the same address). When the words equal the last call's for (slot, sub) and the
  // shadow entry that call produced has not been written since (by any hook), the call would
  // produce the same command the shadow drops: the hook returns before building it.
  static constexpr uint32_t kMaxRawArgs = 20;
  static constexpr uint32_t kMemoSubs = 128;
  bool Unchanged(uint32_t slot, uint32_t sub, std::span<const uint32_t> raw) const;
  // After State(key, ...) for that call.
  void Remember(uint32_t slot, uint32_t sub, uint32_t key, std::span<const uint32_t> raw);
  // Events: appended to the capture while armed and to the live frame while live.
  void Event(commands::CommandPayload payload);
  // A draw whose content keys differ between the capture (content hashes) and the live stream
  // (snapshot keys, in the order the draw's buffer snapshots were taken: vertex buffers, then the
  // index buffer).
  void DrawEvent(commands::Draw draw, const std::vector<commands::Hash>& live_keys);
  // Index the next appended command will get in the current frame.
  uint32_t next_command_index() const;
  void AddUnresolved(commands::UnresolvedReason reason, std::string detail);
  void AddInstanceCount(uint32_t instance_count);

  // Frame boundary: the guest device swap.
  void OnSwapBegin();
  void OnSwapEnd();

  // Resource registry (any thread). Generation increments per construction at an address.
  void OnCreated(commands::ResourceKind kind, uint32_t guest_address, BufferInfo info = {});
  void OnDestroyed(uint32_t guest_address);
  std::optional<BufferInfo> Lookup(commands::ResourceKind kind, uint32_t guest_address);
  // Programs have no constructor/destructor hooks: the createGpuProgram hook reports the program
  // object the render system binds (the assembler program) with its name; a different program at
  // a reused address gets a new generation (resource_registry.h).
  void OnProgramObject(uint32_t program, const std::string& name);
  // Content versions (any thread, always). A write lock released by unlock, or a blit from memory,
  // bumps the version of a buffer or pixel buffer: whoever copies content does it once per
  // version.
  void OnLock(uint32_t buffer, bool write);
  void OnUnlock(uint32_t buffer);
  void OnContentWritten(uint32_t buffer);
  uint32_t ContentVersion(uint32_t address);
  // A texture finished loading (any thread): name, size and format are valid from here on. In
  // the live mode its description is announced so the host texture is created before it is drawn.
  void OnTextureLoaded(const uint8_t* membase, uint32_t texture);

  // Program sources (any thread, always: programs are created while loading, before F9).
  // Written with the capture for the programs it binds, matched by name.
  bool HasProgramSource(const std::string& name);
  void OnProgramCreated(commands::ProgramSource source);

  // Recorded content (render thread, while recording). The capture gets a content hash (blob
  // added while armed); the live stream gets the snapshot of the content version of
  // `version_address` (copied once per version).
  struct RecordedContent {
    commands::Hash capture = 0, live = 0;
  };
  RecordedContent RecordContent(const commands::ResourceId& id, uint32_t version_address,
                                const uint8_t* data, size_t size, commands::BlobEndian endian,
                                uint8_t endian_raw);
  // Resource descriptions (render thread, while recording).
  commands::Hash AddBlob(const uint8_t* data, size_t size, commands::BlobEndian endian,
                         uint8_t endian_raw);
  void AddVertexBuffer(const BufferInfo& info);
  void AddIndexBuffer(const BufferInfo& info);
  // `live_content` replaces desc.content in the live stream.
  void AddTexture(commands::TextureDesc desc, commands::Hash live_content = 0);
  void AddProgram(commands::ProgramDesc desc);
  void AddVertexDeclaration(commands::VertexDeclarationContent content);
  // Live mode with no capture armed (render thread): whether the live stream already has this
  // texture's description with no content snapshot (a static texture or a render target), or this
  // declaration with this content. The hooks then skip reading the description from guest memory
  // again; AddTexture / AddVertexDeclaration would drop it anyway. Within a generation such a
  // texture's description cannot change: a new texture at the address (its constructor) and every
  // reload after the first are new generations, and the key holds the generation. Declarations are
  // keyed by their content's hash too, which the hook still computes on every bind, so one changed
  // in place is described again. Programs are not skipped: no hook sees their destruction.
  bool LiveTextureDescribed(const commands::ResourceId& id) const;
  bool LiveDeclarationDescribed(const commands::ResourceId& id, commands::Hash content) const;

 private:
  Session() = default;
  void Start();
  void Finish();
  void WriteCapture();

  std::atomic<bool> armed_{false};
  std::atomic<bool> requested_{false};
  std::string output_dir_;
  uint32_t frames_to_capture_ = 1;
  ReferenceImageProvider reference_provider_;

  std::array<std::atomic<uint64_t>, kRenderSystemSlotCount> slot_counts_{};
  std::array<uint64_t, kRenderSystemSlotCount> slot_counts_at_start_{};

  // Render thread only.
  // Entries are never removed (EraseState marks them absent), so a pointer to one stays valid for
  // the early out; `version` changes with every write.
  struct ShadowEntry {
    commands::CommandPayload payload;
    uint32_t object = 0;
    uint64_t version = 0;
    bool present = true;
  };
  // A hash map (looked up on every state hook); its node-based entries keep their address, which
  // the early out's memos rely on. Walked in key order where order shows (ShadowKeysInOrder).
  std::unordered_map<uint32_t, ShadowEntry> shadow_;
  std::vector<uint32_t> ShadowKeysInOrder() const;
  struct ArgsMemo {
    const ShadowEntry* entry = nullptr;
    uint64_t version = 0;
    uint32_t count = 0;
    std::array<uint32_t, kMaxRawArgs> raw{};
  };
  std::vector<ArgsMemo> memos_ = std::vector<ArgsMemo>(kRenderSystemSlotCount * kMemoSubs);
  const uint8_t* membase_ = nullptr;
  uint64_t swap_number_ = 0;
  std::atomic<bool> measure_frame_{true};
  bool finishing_ = false;
  commands::Capture capture_;
  std::unordered_map<commands::Hash, size_t> blob_index_;
  std::map<std::tuple<uint32_t, uint32_t, uint32_t>, size_t> texture_index_;
  std::map<std::tuple<uint32_t, uint32_t>, size_t> buffer_index_;  // (address, generation)
  std::map<std::tuple<uint32_t, uint32_t, uint64_t>, size_t> declaration_index_;
  std::map<uint32_t, size_t> program_index_;

  // Any thread.
  ResourceRegistry registry_;
  // A replaced identity (destroyed, or retired by a new generation): dropped in the live stream.
  void Retire(const commands::ResourceId& id);
  live::MeasuredMutex registry_mutex_{"program sources"};  // program_sources_
  std::unordered_map<std::string, commands::ProgramSource> program_sources_;

  // Any thread: content versions by guest address.
  struct ContentState {
    uint32_t version = 0;
    bool read_only_lock = false;  // the pending lock (HardwareBuffer::lock) does not write
  };
  live::MeasuredMutex versions_mutex_{"content versions"};
  std::unordered_map<uint32_t, ContentState> versions_;

  // Live mode. Render thread: the frame being recorded and what it already announced.
  void LiveAppend(commands::CommandPayload payload);
  // `measured`: the frame being cut had its recording timed (kMeasureEveryFrames).
  void CutLiveFrame(bool measured);
  ProducerTimes producer_times_;
  uint64_t producer_ns_at_cut_ = 0;  // producer_times_ total at the previous cut
  HookCosts hook_costs_;
  GpuEventCounts gpu_events_;
  live::FrameQueue* live_queue_ = nullptr;
  live::SnapshotStore* live_store_ = nullptr;
  live::LiveFrame live_frame_;
  // Live mode, per vertex/index buffer (identity with generation), so the draws' snapshots of a
  // buffer already seen take no lookups: whether its description was sent, its last content
  // version and snapshot (the same version is the same content), and the swap whose live frame
  // already holds that snapshot. Dropped when the buffer is destroyed.
  struct LiveBuffer {
    bool described = false;
    uint32_t version = 0;
    live::SnapshotStore::Content content;
    uint64_t added_at_swap = ~0ull;
  };
  std::unordered_map<uint64_t, LiveBuffer> live_buffers_;
  static uint64_t BufferKey(const commands::ResourceId& id) {
    return uint64_t(id.generation) << 32 | id.guest_address;
  }
  // (address, generation, content): hashed, these are checked on every bind.
  struct SentKeyHash {
    size_t operator()(const std::tuple<uint32_t, uint32_t, uint64_t>& k) const noexcept {
      const uint64_t id = uint64_t(std::get<1>(k)) << 32 | std::get<0>(k);
      return std::hash<uint64_t>{}(id ^ (std::get<2>(k) * 0x9E3779B97F4A7C15ull));
    }
  };
  std::unordered_set<std::tuple<uint32_t, uint32_t, uint64_t>, SentKeyHash> live_sent_declarations_,
      live_sent_textures_;
  // Live mode: textures whose description holds no content snapshot, already sent (BufferKey).
  std::unordered_set<uint64_t> live_described_textures_;
  std::unordered_set<uint64_t> live_sent_programs_;  // BufferKey: (generation, address)
  std::unordered_set<std::string> live_sent_sources_;
  std::unordered_set<commands::Hash> live_frame_contents_;
  // Capture armed during the live mode: live content already written to it (chunk LIVE).
  std::set<commands::Hash> capture_live_blobs_;
  std::set<std::tuple<uint32_t, uint32_t, uint64_t>> capture_live_textures_;
  // Any thread: announcements waiting for the next cut.
  live::MeasuredMutex live_pending_mutex_{"live announcements"};
  std::vector<commands::TextureDesc> live_pending_textures_;
  std::vector<commands::ResourceId> live_pending_destroyed_;
};

// Shadow keys: opcode in the high half, unit/kind/stage in the low half.
inline constexpr uint32_t ShadowKey(const commands::CommandPayload& p, uint32_t sub = 0) {
  return (uint32_t(commands::OpcodeOf(p)) << 16) | (sub & 0xFFFF);
}

}  // namespace torchlight::capture
