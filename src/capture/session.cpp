#include "capture/session.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <string_view>
#include <thread>
#include <tuple>

#include <rex/logging.h>

#include "capture/guest_readers.h"
#include "commands/serialize.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/hooks.h"

namespace torchlight::capture {

const char* HookName(uint32_t hook) {
  if (hook < kRenderSystemSlotCount) return guest_abi::ogre::kRenderSystemSlots[hook].name;
  switch (Hook(hook)) {
    case Hook::kLock: return "HardwareBuffer::lock";
    case Hook::kUnlock: return "HardwareBuffer::unlock";
    case Hook::kBlitFromMemory: return "blitFromMemory";
    case Hook::kBlitToMemory: return "blitToMemory";
    case Hook::kTextureLoad: return "D3D9Texture::loadImpl";
    case Hook::kResourceLifetime: return "resource ctor/dtor";
    case Hook::kCreateGpuProgram: return "createGpuProgram";
    case Hook::kSwap: return "swap";
    case Hook::kEnd: break;
  }
  return "?";
}

namespace {

// The current UTC time in a strftime-style format (std::chrono's, which formats the system clock
// in UTC).
std::string UtcNow(std::string_view format) {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::vformat("{:" + std::string(format) + "}", std::make_format_args(now));
}

uint64_t ImageHash(const commands::ReferenceImage& image) {
  return commands::HashBytes(image.rgbx.data(), image.rgbx.size(),
                             commands::BlobEndian::kGuestCpuBigEndian, 0) ^
         (uint64_t(image.width) << 32 | image.height);
}

}  // namespace

Session& Session::Get() {
  static Session session;
  return session;
}

void Session::Configure(std::string output_dir, uint32_t frames) {
  output_dir_ = std::move(output_dir);
  frames_to_capture_ = frames == 0 ? 1 : frames;
}

void Session::SetReferenceImageProvider(ReferenceImageProvider provider) {
  reference_provider_ = std::move(provider);
}

void Session::RequestCapture() {
  if (output_dir_.empty()) {
    REXLOG_WARN("capture: F9 ignored, --capture_dir is not set");
    return;
  }
  if (armed_.load() || requested_.load()) {
    REXLOG_WARN("capture: F9 ignored, a capture is already in progress");
    return;
  }
  requested_.store(true);
  REXLOG_INFO("capture: requested, starts at the next swap ({} frame(s))", frames_to_capture_);
}

void Session::EnableLive(live::FrameQueue* queue, live::SnapshotStore* store) {
  live_store_ = store;
  live_queue_ = queue;
  live_frame_.cut = std::chrono::steady_clock::now();
  constant_mirror_.Reset();  // the consumer starts with no constants
  // The live consumer starts empty: it gets the state already in the shadow first, as a capture's
  // baseline does, since State skips values that did not change.
  for (uint32_t key : ShadowKeysInOrder()) {
    const ShadowEntry& entry = shadow_.at(key);
    if (entry.present) LiveAppend(entry.payload);
  }
}

namespace {
// State the consumer resets by itself, so it is sent even when it did not change: the backend
// sets the full viewport when the target changes and when a frame begins.
bool AlwaysSent(const commands::CommandPayload& p) {
  return std::holds_alternative<commands::SetViewport>(p) ||
         std::holds_alternative<commands::SetRenderTarget>(p);
}
}  // namespace

void Session::State(uint32_t key, commands::CommandPayload payload, uint32_t object) {
  // A value equal to the shadow's is what every consumer already has (a capture starts from the
  // shadow as its baseline, the live consumer from EnableLive): not sent again. OGRE re-sets every
  // texture unit's state per pass, most of it unchanged.
  auto it = shadow_.find(key);
  if (it != shadow_.end() && it->second.present && it->second.object == object &&
      it->second.payload == payload && !AlwaysSent(payload)) {
    return;
  }
  if (armed()) {
    capture_.frames.back().commands.push_back({0, payload});
  }
  if (live()) LiveAppend(payload);
  ShadowEntry& entry = shadow_[key];
  entry.payload = std::move(payload);
  entry.object = object;
  entry.present = true;
  ++entry.version;
}

std::vector<uint32_t> Session::ShadowKeysInOrder() const {
  std::vector<uint32_t> keys;
  keys.reserve(shadow_.size());
  for (const auto& [key, entry] : shadow_) keys.push_back(key);
  std::sort(keys.begin(), keys.end());
  return keys;
}

void Session::EraseState(uint32_t key) {
  auto it = shadow_.find(key);
  if (it == shadow_.end() || !it->second.present) return;
  it->second.present = false;
  ++it->second.version;
}

bool Session::Unchanged(uint32_t slot, uint32_t sub, std::span<const uint32_t> raw) const {
  if (slot >= kRenderSystemSlotCount || sub >= kMemoSubs || raw.size() > kMaxRawArgs) return false;
  const ArgsMemo& m = memos_[slot * kMemoSubs + sub];
  return m.entry && m.entry->present && m.entry->version == m.version &&
         m.count == raw.size() && std::equal(raw.begin(), raw.end(), m.raw.begin());
}

void Session::Remember(uint32_t slot, uint32_t sub, uint32_t key, std::span<const uint32_t> raw) {
  if (slot >= kRenderSystemSlotCount || sub >= kMemoSubs || raw.size() > kMaxRawArgs) return;
  auto it = shadow_.find(key);
  if (it == shadow_.end()) return;
  ArgsMemo& m = memos_[slot * kMemoSubs + sub];
  m.entry = &it->second;
  m.version = it->second.version;
  m.count = uint32_t(raw.size());
  std::copy(raw.begin(), raw.end(), m.raw.begin());
}

void Session::Event(commands::CommandPayload payload) {
  if (!armed()) {
    if (live()) LiveAppend(std::move(payload));
    return;
  }
  if (live()) LiveAppend(payload);
  capture_.frames.back().commands.push_back({0, std::move(payload)});
}

void Session::DrawEvent(commands::Draw draw, const std::vector<commands::Hash>& live_keys) {
  if (live()) {
    // Copied only when a capture also keeps it.
    commands::Draw d = armed() ? draw : std::move(draw);
    size_t k = 0;
    for (auto& snap : d.vertex_buffers) snap.blob = k < live_keys.size() ? live_keys[k++] : 0;
    if (d.index_buffer) d.index_buffer->blob = k < live_keys.size() ? live_keys[k++] : 0;
    LiveAppend(std::move(d));
  }
  if (armed()) {
    if (live()) draw.live_keys = live_keys;  // what the backend window used (format 1.5)
    capture_.frames.back().commands.push_back({0, std::move(draw)});
  }
}

void Session::LiveAppend(commands::CommandPayload payload) {
  live_frame_.commands.push_back({0, std::move(payload)});
}

void Session::CutLiveFrame(bool measured) {
  ProducerTimer timer(producer_times_, ProducerSection::kCut, measuring());
  producer_times_.frames.fetch_add(1, std::memory_order_relaxed);
  const auto now = std::chrono::steady_clock::now();
  live_frame_.producer.guest_ms = std::chrono::duration<double, std::milli>(now - live_frame_.cut).count();
  const uint64_t producer_ns = producer_times_.top_level_ns.load(std::memory_order_relaxed);
  if (measured) live_frame_.producer.recording_ms = double(producer_ns - producer_ns_at_cut_) / 1e6;
  producer_ns_at_cut_ = producer_ns;
  {
    std::lock_guard<live::MeasuredMutex> lock(live_pending_mutex_);
    for (auto& t : live_pending_textures_) live_frame_.textures.push_back(std::move(t));
    for (const auto& id : live_pending_destroyed_) {
      live_frame_.destroyed.push_back(id);
      live_buffers_.erase(BufferKey(id));
      live_described_textures_.erase(BufferKey(id));
      live_sent_programs_.erase(BufferKey(id));
    }
    live_pending_textures_.clear();
    live_pending_destroyed_.clear();
  }
  live_frame_.swap = swap_number_;
  // The next frame's commands reserved at this one's count and a margin: growing the vector by
  // doubling reallocated and moved every command several times per frame.
  const size_t commands = live_frame_.commands.size();
  live_queue_->Push(std::move(live_frame_));
  live_frame_ = live::LiveFrame{};
  live_frame_.commands.reserve(commands + commands / 8);
  live_frame_.cut = std::chrono::steady_clock::now();
  live_frame_contents_.clear();
}

Session::RecordedContent Session::RecordContent(const commands::ResourceId& id,
                                                uint32_t version_address, const uint8_t* data,
                                                size_t size, commands::BlobEndian endian,
                                                uint8_t endian_raw) {
  ProducerTimer timer(producer_times_, ProducerSection::kResources, measuring());
  RecordedContent r;
  if (armed()) r.capture = AddBlob(data, size, endian, endian_raw);
  if (live()) {
    uint32_t version = ContentVersion(version_address);
    bool buffer = id.kind == commands::ResourceKind::kVertexBuffer ||
                  id.kind == commands::ResourceKind::kIndexBuffer;
    LiveBuffer* cached = buffer ? &live_buffers_[BufferKey(id)] : nullptr;
    live::SnapshotStore::Content content;
    if (cached && cached->content && cached->version == version) {
      content = cached->content;
    } else {
      content = live_store_->Find(id, version);
      if (!content) {
        commands::Blob blob;
        blob.endian = endian;
        blob.endian_raw = endian_raw;
        blob.bytes.assign(data, data + size);
        content = live_store_->Put(id, version, std::move(blob));
        ++live_frame_.producer.snapshot_copies;
        live_frame_.producer.snapshot_bytes += size;
      }
      if (cached) {
        cached->version = version;
        cached->content = content;
        cached->added_at_swap = ~0ull;
      }
    }
    r.live = content->hash;
    if (!cached || cached->added_at_swap != swap_number_) {
      if (live_frame_contents_.insert(r.live).second) live_frame_.contents.push_back({id, content});
      if (cached) cached->added_at_swap = swap_number_;
    }
    if (armed() && capture_live_blobs_.insert(r.live).second) capture_.live_blobs.push_back(*content);
  }
  return r;
}

uint32_t Session::next_command_index() const {
  return capture_.frames.empty() ? 0 : uint32_t(capture_.frames.back().commands.size());
}

void Session::AddUnresolved(commands::UnresolvedReason reason, std::string detail) {
  if (!armed()) return;
  capture_.stats.unresolved.push_back({uint32_t(capture_.frames.size() - 1),
                                       next_command_index(), reason, std::move(detail)});
}

void Session::AddInstanceCount(uint32_t instance_count) {
  if (!armed()) return;
  capture_.stats.instance_counts_above_one.push_back(
      {uint32_t(capture_.frames.size() - 1), next_command_index(), instance_count});
}

void Session::OnSwapBegin() {
  // The frame ending now was measured or not; the one starting is measured one in
  // kMeasureEveryFrames.
  if (live() && measure_frame_.load(std::memory_order_relaxed)) {
    producer_times_.measured.fetch_add(1, std::memory_order_relaxed);
  }
  const bool ended_frame_measured = measure_frame_.load(std::memory_order_relaxed);
  ++swap_number_;
  measure_frame_.store(swap_number_ % kMeasureEveryFrames == 0, std::memory_order_relaxed);
  if (live()) {
    LiveAppend(commands::Present{swap_number_});
    CutLiveFrame(ended_frame_measured);
  }
  if (armed()) {
    capture_.frames.back().commands.push_back({0, commands::Present{swap_number_}});
    if (capture_.frames.size() >= frames_to_capture_) {
      finishing_ = true;  // stop recording before the original swap runs
      armed_.store(false);
      return;
    }
    capture_.frames.push_back({uint32_t(capture_.frames.size()), swap_number_, {}});
    return;
  }
  if (requested_.exchange(false)) Start();
}

void Session::Start() {
  capture_ = commands::Capture{};
  blob_index_.clear();
  texture_index_.clear();
  buffer_index_.clear();
  declaration_index_.clear();
  program_index_.clear();
  capture_live_blobs_.clear();
  capture_live_textures_.clear();
  capture_.meta.first_swap = swap_number_;
  capture_.meta.created_utc = UtcNow("%Y-%m-%dT%H:%M:%SZ");
  for (uint32_t i = 0; i < kRenderSystemSlotCount; ++i) {
    slot_counts_at_start_[i] = slot_counts_[i].load(std::memory_order_relaxed);
  }
  capture_.frames.push_back({0, swap_number_, {}});
  armed_.store(true);
  // Baseline: the last value of every state setter, with the resources they refer to re-read
  // now so their descriptions are part of the capture.
  size_t baseline = 0;
  for (uint32_t key : ShadowKeysInOrder()) {
    ShadowEntry& entry = shadow_.at(key);
    if (!entry.present) continue;
    ++baseline;
    ++entry.version;  // re-read below: early outs recorded against it no longer apply
    if (entry.object != 0 && membase_ != nullptr) {
      if (auto* t = std::get_if<commands::SetTexture>(&entry.payload)) {
        t->texture = CaptureTexture(membase_, entry.object);
      } else if (std::holds_alternative<commands::SetVertexDeclaration>(entry.payload)) {
        entry.payload = CaptureVertexDeclaration(membase_, entry.object);
      } else if (std::holds_alternative<commands::BindProgram>(entry.payload)) {
        entry.payload = CaptureProgram(membase_, entry.object);
      }
    }
    capture_.frames.back().commands.push_back({commands::kFromBaseline, entry.payload});
  }
  REXLOG_INFO("capture: started at swap {} with {} baseline state command(s)", swap_number_,
              baseline);
}

void Session::OnSwapEnd() {
  if (!finishing_) return;
  finishing_ = false;
  Finish();
}

void Session::Finish() {
  for (uint32_t i = 0; i < kRenderSystemSlotCount; ++i) {
    const auto& row = guest_abi::ogre::kRenderSystemSlots[i];
    hooks::SlotAttribution a = hooks::GetSlotAttribution(i);
    uint64_t total = slot_counts_[i].load(std::memory_order_relaxed);
    capture_.stats.slots.push_back(
        {i, row.name, a.attributable, a.note, total, total - slot_counts_at_start_[i]});
  }
  // Reference image: the guest render thread is blocked here, right after it submitted the
  // closing swap, so no newer frame can be queued. Wait until the presented guest output stops
  // changing; the last image is the captured frame unless the wait times out.
  if (reference_provider_) {
    constexpr int kPollMs = 50, kStablePolls = 4, kTimeoutMs = 2000;
    auto start = std::chrono::steady_clock::now();
    commands::ReferenceImage last;
    uint64_t last_hash = 0;
    int stable = 0;
    bool done = false;
    while (true) {
      commands::ReferenceImage image;
      if (reference_provider_(image)) {
        uint64_t h = ImageHash(image);
        stable = (h == last_hash) ? stable + 1 : 0;
        last_hash = h;
        last = std::move(image);
        if (stable >= kStablePolls) {
          done = true;
          break;
        }
      }
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start);
      if (elapsed.count() >= kTimeoutMs) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    if (!last.rgbx.empty()) {
      last.possibly_misaligned = !done;
      last.wait_ms = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
      capture_.reference = std::move(last);
    }
  }
  WriteCapture();
}

void Session::WriteCapture() {
  capture_.meta.frame_count = uint32_t(capture_.frames.size());
  {
    std::lock_guard<live::MeasuredMutex> lock(registry_mutex_);
    for (const auto& p : capture_.programs) {
      auto it = program_sources_.find(p.name);
      if (it != program_sources_.end()) capture_.program_sources.push_back(it->second);
    }
  }
  std::error_code ec;
  std::filesystem::create_directories(output_dir_, ec);
  std::string path = output_dir_ + "/torchlight_" + UtcNow("%Y%m%d_%H%M%S") + "_swap" +
                     std::to_string(capture_.meta.first_swap) + ".tlcap";
  std::string error;
  size_t draws = 0;
  for (const auto& f : capture_.frames) {
    for (const auto& c : f.commands) draws += std::holds_alternative<commands::Draw>(c.payload);
  }
  if (commands::WriteCaptureFile(path, capture_, error)) {
    REXLOG_INFO(
        "capture: wrote {} ({} frame(s), {} draw(s), {} blob(s), {} unresolved, {}/{} program "
        "source(s))",
        path, capture_.frames.size(), draws, capture_.blobs.size(),
        capture_.stats.unresolved.size(), capture_.program_sources.size(),
        capture_.programs.size());
  } else {
    REXLOG_ERROR("capture: {}", error);
  }
  capture_ = commands::Capture{};
}

void Session::Retire(const commands::ResourceId& id) {
  if (!live()) return;
  live_store_->Release(id);
  std::lock_guard<live::MeasuredMutex> pending(live_pending_mutex_);
  live_pending_destroyed_.push_back(id);
}

void Session::OnCreated(commands::ResourceKind kind, uint32_t guest_address, BufferInfo info) {
  if (auto retired = registry_.Create(kind, guest_address, info)) Retire(*retired);
}

void Session::OnDestroyed(uint32_t guest_address) {
  if (auto destroyed = registry_.Destroy(guest_address)) Retire(*destroyed);
  std::lock_guard<live::MeasuredMutex> lock(versions_mutex_);
  versions_.erase(guest_address);
}

void Session::OnProgramObject(uint32_t program, const std::string& name) {
  auto renewal = registry_.Program(program, name);
  if (renewal.retired) Retire(*renewal.retired);
}

void Session::OnLock(uint32_t buffer, bool write) {
  ProducerTimer timer(producer_times_, ProducerSection::kMarking, live() && measuring());
  std::lock_guard<live::MeasuredMutex> lock(versions_mutex_);
  versions_[buffer].read_only_lock = !write;
}

void Session::OnUnlock(uint32_t buffer) {
  ProducerTimer timer(producer_times_, ProducerSection::kMarking, live() && measuring());
  std::lock_guard<live::MeasuredMutex> lock(versions_mutex_);
  ContentState& c = versions_[buffer];
  // Pixel buffers lock through HardwarePixelBuffer::lock(Box), which is not hooked: an unlock
  // without a recorded read-only lock counts as a write.
  if (!c.read_only_lock) ++c.version;
  c.read_only_lock = false;
}

void Session::OnContentWritten(uint32_t buffer) {
  ProducerTimer timer(producer_times_, ProducerSection::kMarking, live() && measuring());
  std::lock_guard<live::MeasuredMutex> lock(versions_mutex_);
  ++versions_[buffer].version;
}

uint32_t Session::ContentVersion(uint32_t address) {
  std::lock_guard<live::MeasuredMutex> lock(versions_mutex_);
  auto it = versions_.find(address);
  return it == versions_.end() ? 0 : it->second.version;
}

void Session::OnTextureLoaded(const uint8_t* membase, uint32_t texture) {
  // A reload after the first load is a new generation (its name, size or content may change).
  auto renewal = registry_.TextureLoaded(texture);
  if (!renewal) return;
  if (renewal->retired) Retire(*renewal->retired);
  if (!live()) return;
  commands::TextureDesc d = ReadTextureDesc(membase, texture);
  d.id = renewal->id;
  // Render targets and dynamic textures are announced when they are bound (their content changes
  // after loading).
  if (d.render_target || d.name.empty() || d.manual) return;
  std::lock_guard<live::MeasuredMutex> lock(live_pending_mutex_);
  live_pending_textures_.push_back(std::move(d));
}

std::optional<BufferInfo> Session::Lookup(commands::ResourceKind kind, uint32_t guest_address) {
  return registry_.Lookup(kind, guest_address);
}

bool Session::HasProgramSource(const std::string& name) {
  std::lock_guard<live::MeasuredMutex> lock(registry_mutex_);
  return program_sources_.count(name) != 0;
}

void Session::OnProgramCreated(commands::ProgramSource source) {
  std::lock_guard<live::MeasuredMutex> lock(registry_mutex_);
  std::string name = source.name;
  program_sources_.emplace(std::move(name), std::move(source));
}

commands::Hash Session::AddBlob(const uint8_t* data, size_t size, commands::BlobEndian endian,
                                uint8_t endian_raw) {
  commands::Hash h = commands::HashBytes(data, size, endian, endian_raw);
  if (blob_index_.emplace(h, capture_.blobs.size()).second) {
    commands::Blob b;
    b.hash = h;
    b.endian = endian;
    b.endian_raw = endian_raw;
    b.bytes.assign(data, data + size);
    capture_.blobs.push_back(std::move(b));
  }
  return h;
}

void Session::AddVertexBuffer(const BufferInfo& info) {
  auto key = std::make_tuple(info.id.guest_address, info.id.generation);
  commands::VertexBufferDesc desc{info.id, info.element_size, info.count, info.usage};
  if (live()) {
    LiveBuffer& cached = live_buffers_[BufferKey(info.id)];
    if (!cached.described) {
      cached.described = true;
      live_frame_.vertex_buffers.push_back(desc);
    }
  }
  if (armed() && buffer_index_.emplace(key, capture_.vertex_buffers.size()).second) {
    capture_.vertex_buffers.push_back(desc);
  }
}

void Session::AddIndexBuffer(const BufferInfo& info) {
  auto key = std::make_tuple(info.id.guest_address, info.id.generation);
  commands::IndexBufferDesc desc{info.id, info.element_size, info.count, info.usage};
  if (live()) {
    LiveBuffer& cached = live_buffers_[BufferKey(info.id)];
    if (!cached.described) {
      cached.described = true;
      live_frame_.index_buffers.push_back(desc);
    }
  }
  if (armed() && buffer_index_.emplace(key, capture_.index_buffers.size()).second) {
    capture_.index_buffers.push_back(desc);
  }
}

void Session::AddTexture(commands::TextureDesc desc, commands::Hash live_content) {
  if (live()) {
    auto sent = std::make_tuple(desc.id.guest_address, desc.id.generation, live_content);
    if (live_sent_textures_.insert(sent).second) {
      commands::TextureDesc d = desc;
      d.content = live_content;
      live_frame_.textures.push_back(std::move(d));
    }
    // No content snapshot (a static texture or a render target): this generation's description
    // is final, the hooks need not read it again (LiveTextureDescribed). Dynamic textures carry
    // their content and keep being read.
    if (live_content == 0 && (desc.render_target || (!desc.name.empty() && !desc.manual))) {
      live_described_textures_.insert(BufferKey(desc.id));
    }
  }
  if (!armed()) return;
  if (live() && live_content &&
      capture_live_textures_.insert({desc.id.guest_address, desc.id.generation, live_content})
          .second) {
    capture_.live_textures.push_back({desc.id, desc.content, live_content});
  }
  auto key = std::make_tuple(desc.id.guest_address, desc.id.generation, desc.width);
  if (texture_index_.emplace(key, capture_.textures.size()).second) {
    capture_.textures.push_back(std::move(desc));
  }
}

void Session::AddProgram(commands::ProgramDesc desc) {
  if (live() && live_sent_programs_.insert(BufferKey(desc.id)).second) {
    live_frame_.programs.push_back(desc);
    if (!live_sent_sources_.count(desc.name)) {
      std::lock_guard<live::MeasuredMutex> lock(registry_mutex_);
      auto it = program_sources_.find(desc.name);
      if (it != program_sources_.end()) {
        live_frame_.program_sources.push_back(it->second);
        live_sent_sources_.insert(desc.name);
      }
    }
  }
  if (armed() && program_index_.emplace(desc.id.guest_address, capture_.programs.size()).second) {
    capture_.programs.push_back(std::move(desc));
  }
}

bool Session::LiveTextureDescribed(const commands::ResourceId& id) const {
  return live() && !armed() && live_described_textures_.count(BufferKey(id)) != 0;
}

bool Session::LiveDeclarationDescribed(const commands::ResourceId& id,
                                       commands::Hash content) const {
  return live() && !armed() &&
         live_sent_declarations_.count({id.guest_address, id.generation, content}) != 0;
}

void Session::AddVertexDeclaration(commands::VertexDeclarationContent content) {
  auto key = std::make_tuple(content.id.guest_address, content.id.generation, content.content);
  if (live() && live_sent_declarations_.insert(key).second) live_frame_.declarations.push_back(content);
  if (armed() && declaration_index_.emplace(key, capture_.vertex_declarations.size()).second) {
    capture_.vertex_declarations.push_back(std::move(content));
  }
}

}  // namespace torchlight::capture
