#include "commands/serialize.h"

#include <algorithm>
#include <array>
#include <unordered_set>
#include <mutex>
#include <bit>
#include <cstring>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <utility>

namespace torchlight::commands {

namespace {

constexpr char kMagic[8] = {'T', 'L', 'C', 'A', 'P', 0, 0, 0};

constexpr uint32_t Tag(char a, char b, char c, char d) {
  return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 |
         uint32_t(uint8_t(d)) << 24;
}
constexpr uint32_t kTagMeta = Tag('M', 'E', 'T', 'A');
constexpr uint32_t kTagResources = Tag('R', 'S', 'R', 'C');
constexpr uint32_t kTagBlobs = Tag('B', 'L', 'O', 'B');
constexpr uint32_t kTagFrames = Tag('F', 'R', 'M', 'S');
constexpr uint32_t kTagStats = Tag('S', 'T', 'A', 'T');
constexpr uint32_t kTagReference = Tag('R', 'I', 'M', 'G');
constexpr uint32_t kTagProgramSources = Tag('P', 'S', 'R', 'C');  // 1.3
constexpr uint32_t kTagLive = Tag('L', 'I', 'V', 'E');            // 1.5

template <typename T>
T ToLittle(T v) {
  if constexpr (std::endian::native == std::endian::big && sizeof(T) > 1) {
    auto bytes = std::bit_cast<std::array<uint8_t, sizeof(T)>>(v);
    std::reverse(bytes.begin(), bytes.end());
    return std::bit_cast<T>(bytes);
  }
  return v;
}

class Writer {
 public:
  static constexpr bool kReading = false;
  std::vector<uint8_t> out;

  template <typename T>
  void num(T& v) {
    if constexpr (std::is_enum_v<T>) {
      auto u = static_cast<std::underlying_type_t<T>>(v);
      num(u);
    } else if constexpr (std::is_same_v<T, bool>) {
      uint8_t b = v ? 1 : 0;
      num(b);
    } else {
      T le = ToLittle(v);
      const auto* p = reinterpret_cast<const uint8_t*>(&le);
      out.insert(out.end(), p, p + sizeof(T));
    }
  }
  void bytes(std::vector<uint8_t>& v) {
    uint64_t n = v.size();
    num(n);
    out.insert(out.end(), v.begin(), v.end());
  }
  void str(std::string& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    num(n);
    out.insert(out.end(), s.begin(), s.end());
  }
  void name(std::string_view& s) {
    uint32_t n = static_cast<uint32_t>(s.size());
    num(n);
    out.insert(out.end(), s.begin(), s.end());
  }
  bool ok() const { return true; }
};

class Reader {
 public:
  static constexpr bool kReading = true;
  Reader(const uint8_t* begin, const uint8_t* end) : p_(begin), end_(end) {}

  template <typename T>
  void num(T& v) {
    if constexpr (std::is_enum_v<T>) {
      std::underlying_type_t<T> u{};
      num(u);
      v = static_cast<T>(u);
    } else if constexpr (std::is_same_v<T, bool>) {
      uint8_t b = 0;
      num(b);
      v = b != 0;
    } else {
      if (!take(sizeof(T))) {
        v = T{};
        return;
      }
      T le;
      std::memcpy(&le, p_ - sizeof(T), sizeof(T));
      v = ToLittle(le);
    }
  }
  void bytes(std::vector<uint8_t>& v) {
    uint64_t n = 0;
    num(n);
    if (!take(n)) return;
    v.assign(p_ - n, p_);
  }
  void str(std::string& s) {
    uint32_t n = 0;
    num(n);
    if (!take(n)) return;
    s.assign(reinterpret_cast<const char*>(p_ - n), n);
  }
  void name(std::string_view& s) {
    uint32_t n = 0;
    num(n);
    if (!take(n)) return;
    s = InternName(std::string_view(reinterpret_cast<const char*>(p_ - n), n));
  }
  bool take(uint64_t n) {
    if (failed_ || uint64_t(end_ - p_) < n) {
      failed_ = true;
      return false;
    }
    p_ += n;
    return true;
  }
  bool ok() const { return !failed_; }
  bool at_end() const { return p_ == end_; }
  const uint8_t* position() const { return p_; }

 private:
  const uint8_t* p_;
  const uint8_t* end_;
  bool failed_ = false;
};

// Element count guard: a corrupt count must not allocate gigabytes.
constexpr uint64_t kMaxElements = 1u << 26;

template <typename IO, typename T, typename F>
void Vec(IO& io, std::vector<T>& v, F&& each) {
  uint64_t n = v.size();
  io.num(n);
  if constexpr (IO::kReading) {
    if (n > kMaxElements) {
      io.take(~0ull);
      return;
    }
    v.resize(n);
  }
  for (auto& e : v) {
    if (!io.ok()) return;
    each(io, e);
  }
}

template <typename IO, typename T, typename F>
void Opt(IO& io, std::optional<T>& v, F&& each) {
  bool has = v.has_value();
  io.num(has);
  if constexpr (IO::kReading) {
    if (has) v.emplace();
    else v.reset();
  }
  if (has) each(io, *v);
}

template <typename IO, typename T, size_t N>
void Arr(IO& io, std::array<T, N>& a) {
  for (auto& e : a) io.num(e);
}

template <typename IO, typename E>
void S(IO& io, Enum<E>& e) {
  io.num(e.value);
  io.num(e.raw);
}
template <typename IO>
void S(IO& io, ResourceId& id) {
  io.num(id.kind);
  io.num(id.guest_address);
  io.num(id.generation);
}

// ---- command payloads -----------------------------------------------------------------------
template <typename IO> void S(IO&, BeginFrame&) {}
template <typename IO> void S(IO&, EndFrame&) {}
template <typename IO> void S(IO& io, Present& c) { io.num(c.swap_number); }
template <typename IO> void S(IO& io, SetMatrix& c) {
  S(io, c.kind); io.num(c.texture_unit); Arr(io, c.m);
}
template <typename IO> void S(IO& io, SetWorldMatrices& c) {
  Vec(io, c.matrices, [](IO& io, std::array<float, 16>& m) { Arr(io, m); });
}
template <typename IO> void S(IO& io, SetRenderTarget& c) {
  io.num(c.guest_address); io.str(c.name); io.num(c.width); io.num(c.height);
  io.num(c.colour_depth); io.num(c.requires_flipping);
}
template <typename IO> void S(IO& io, SetViewport& c) {
  io.num(c.guest_address); io.num(c.target_guest_address); io.num(c.left); io.num(c.top);
  io.num(c.width); io.num(c.height);
}
template <typename IO> void S(IO& io, Clear& c) {
  io.num(c.buffers); Arr(io, c.colour); io.num(c.depth); io.num(c.stencil);
}
template <typename IO> void S(IO& io, SetTexture& c) {
  io.num(c.unit); io.num(c.enabled); io.num(c.vertex_texture);
  Opt(io, c.texture, [](IO& io, ResourceId& id) { S(io, id); });
}
template <typename IO> void S(IO& io, DisableTextureUnit& c) { io.num(c.unit); }
template <typename IO> void S(IO& io, SetSamplerFilter& c) {
  io.num(c.unit); S(io, c.stage); S(io, c.filter);
}
template <typename IO> void S(IO& io, SetSamplerAddress& c) {
  io.num(c.unit); for (auto& e : c.uvw) S(io, e);
}
template <typename IO> void S(IO& io, SetSamplerAnisotropy& c) {
  io.num(c.unit); io.num(c.max_anisotropy);
}
template <typename IO> void S(IO& io, SetSamplerMipBias& c) { io.num(c.unit); io.num(c.bias); }
template <typename IO> void S(IO& io, SetSamplerBorder& c) { io.num(c.unit); Arr(io, c.colour); }
template <typename IO> void S(IO& io, SetTexCoordSet& c) { io.num(c.unit); io.num(c.index); }
template <typename IO> void S(IO& io, SetTexCoordCalc& c) { io.num(c.unit); io.num(c.raw_method); }
template <typename IO> void S(IO& io, SetBlend& c) {
  io.num(c.separate); S(io, c.src); S(io, c.dst); S(io, c.src_alpha); S(io, c.dst_alpha);
  S(io, c.op); S(io, c.op_alpha);
}
template <typename IO> void S(IO& io, SetDepthCheck& c) { io.num(c.enabled); }
template <typename IO> void S(IO& io, SetDepthWrite& c) { io.num(c.enabled); }
template <typename IO> void S(IO& io, SetDepthFunc& c) { S(io, c.requested); S(io, c.effective); }
template <typename IO> void S(IO& io, SetDepthBias& c) {
  io.num(c.constant); io.num(c.slope_scale);
}
template <typename IO> void S(IO& io, SetCull& c) { S(io, c.mode); }
template <typename IO> void S(IO& io, SetAlphaReject& c) {
  S(io, c.func); io.num(c.reference); io.num(c.alpha_to_coverage);
}
template <typename IO> void S(IO& io, SetColourWrite& c) {
  io.num(c.r); io.num(c.g); io.num(c.b); io.num(c.a);
}
template <typename IO> void S(IO& io, SetPolygonMode& c) { S(io, c.mode); }
template <typename IO> void S(IO& io, SetStencilCheck& c) { io.num(c.enabled); }
template <typename IO> void S(IO& io, SetStencil& c) {
  S(io, c.func); io.num(c.reference); io.num(c.mask); S(io, c.fail); S(io, c.depth_fail);
  S(io, c.pass); io.num(c.two_sided);
}
template <typename IO> void S(IO& io, SetScissor& c) {
  io.num(c.enabled); io.num(c.left); io.num(c.top); io.num(c.right); io.num(c.bottom);
}
template <typename IO> void S(IO& io, SetClipPlanes& c) {
  Vec(io, c.planes, [](IO& io, std::array<float, 4>& p) { Arr(io, p); });
}
template <typename IO> void S(IO& io, SetPointSprites& c) { io.num(c.enabled); }
template <typename IO> void S(IO& io, SetInvertWinding& c) { io.num(c.invert); }
template <typename IO> void S(IO& io, SetDeriveDepthBias& c) {
  io.num(c.derive); io.num(c.base); io.num(c.multiplier); io.num(c.slope_scale);
}
template <typename IO> void S(IO& io, SetPassIterationCount& c) { io.num(c.count); }
template <typename IO> void S(IO& io, SetVertexDeclaration& c) {
  Opt(io, c.declaration, [](IO& io, ResourceId& id) { S(io, id); });
  io.num(c.content);
}
template <typename IO> void S(IO& io, SetVertexBuffers& c) {
  Vec(io, c.streams, [](IO& io, VertexStream& s) {
    io.num(s.stream);
    Opt(io, s.buffer, [](IO& io, ResourceId& id) { S(io, id); });
  });
}
template <typename IO> void S(IO& io, BindProgram& c) { S(io, c.stage); S(io, c.program); }
template <typename IO> void S(IO& io, UnbindProgram& c) { S(io, c.stage); }
template <typename IO> void S(IO& io, ConstantRange& r) {
  io.num(r.logical_index); io.num(r.register_index); io.num(r.physical_index);
  io.num(r.element_count);
  io.num(r.variability);
  Vec(io, r.data, [](IO& io, uint32_t& v) { io.num(v); });
}
template <typename IO> void S(IO& io, AutoConstant& a) {
  io.num(a.raw_type); io.name(a.name); io.num(a.physical_index); io.num(a.element_count);
  io.num(a.data); io.num(a.variability);
}
template <typename IO> void S(IO& io, SetConstants& c) {
  S(io, c.stage); io.num(c.mask); io.num(c.parameters_guest_address);
  Vec(io, c.floats, [](IO& io, ConstantRange& r) { S(io, r); });
  Vec(io, c.ints, [](IO& io, ConstantRange& r) { S(io, r); });
  Vec(io, c.autos, [](IO& io, AutoConstant& a) { S(io, a); });
  // Added in 1.2 at the end of the payload: older payloads simply end before it.
  if constexpr (IO::kReading) {
    if (!io.at_end()) Opt(io, c.transpose_matrices, [](IO& io, bool& b) { io.num(b); });
  } else {
    Opt(io, c.transpose_matrices, [](IO& io, bool& b) { io.num(b); });
  }
}
template <typename IO> void S(IO& io, BufferSnapshot& b) {
  S(io, b.buffer); io.num(b.blob); io.num(b.source); io.num(b.guest_virtual); io.num(b.size);
}
template <typename IO> void S(IO& io, Draw& c) {
  S(io, c.primitive); io.num(c.vertex_start); io.num(c.vertex_count); io.num(c.indexed);
  io.num(c.index_start); io.num(c.index_count); io.num(c.index_size); io.num(c.instance_count);
  io.num(c.invert_winding); io.num(c.target_flipping); io.num(c.pass_iteration_count);
  Vec(io, c.vertex_buffers, [](IO& io, BufferSnapshot& b) { S(io, b); });
  Opt(io, c.index_buffer, [](IO& io, BufferSnapshot& b) { S(io, b); });
  io.num(c.unresolved_mask);
  // Added in 1.5 at the end of the payload.
  if constexpr (IO::kReading) {
    if (!io.at_end()) Vec(io, c.live_keys, [](IO& io, Hash& h) { io.num(h); });
  } else {
    Vec(io, c.live_keys, [](IO& io, Hash& h) { io.num(h); });
  }
}

template <typename IO> void S(IO& io, SetGammaRamp& c) { io.num(c.pwl); Arr(io, c.values); }
template <typename IO> void S(IO& io, SetSceneClip& c) { io.num(c.viewport); }

template <typename IO> void S(IO& io, SetTextureBlend& c) {
  io.num(c.unit); io.num(c.raw_blend_type); io.num(c.raw_operation); io.num(c.raw_source1);
  io.num(c.raw_source2); io.num(c.is_modulate); Arr(io, c.colour_arg1); Arr(io, c.colour_arg2);
  io.num(c.alpha_arg1); io.num(c.alpha_arg2); io.num(c.factor);
}

// ---- commands -------------------------------------------------------------------------------
void WriteCommand(Writer& w, Command& c) {
  uint16_t opcode = OpcodeOf(c.payload);
  w.num(opcode);
  w.num(c.flags);
  Writer body;
  std::visit([&](auto& p) { S(body, p); }, c.payload);
  uint32_t len = static_cast<uint32_t>(body.out.size());
  w.num(len);
  w.out.insert(w.out.end(), body.out.begin(), body.out.end());
}

template <size_t... I>
bool EmplaceByIndex(size_t index, CommandPayload& p, Reader& r, std::index_sequence<I...>) {
  bool found = false;
  ((index == I ? (p.emplace<I>(), S(r, std::get<I>(p)), found = true) : false), ...);
  return found;
}

// Returns false on malformed input; an unknown opcode (newer minor version) is skipped.
bool ReadCommand(Reader& r, std::vector<Command>& out) {
  uint16_t opcode = 0, flags = 0;
  uint32_t len = 0;
  r.num(opcode);
  r.num(flags);
  r.num(len);
  const uint8_t* body = r.position();
  if (!r.take(len)) return false;
  if (opcode == 0 || opcode > std::variant_size_v<CommandPayload>) return true;
  Reader sub(body, body + len);
  Command c;
  c.flags = flags;
  EmplaceByIndex(opcode - 1u, c.payload, sub,
                 std::make_index_sequence<std::variant_size_v<CommandPayload>>{});
  if (!sub.ok()) return false;
  out.push_back(std::move(c));
  return true;
}

// ---- sections -------------------------------------------------------------------------------
template <typename IO> void S(IO& io, Meta& m) {
  io.num(m.version_major); io.num(m.version_minor); io.num(m.first_swap);
  io.num(m.frame_count); io.str(m.created_utc); io.str(m.notes);
}
template <typename IO> void S(IO& io, VertexBufferDesc& d) {
  S(io, d.id); io.num(d.vertex_size); io.num(d.num_vertices); io.num(d.usage);
}
template <typename IO> void S(IO& io, IndexBufferDesc& d) {
  S(io, d.id); io.num(d.index_size); io.num(d.num_indexes); io.num(d.usage);
}
template <typename IO> void S(IO& io, VertexElement& e) {
  io.num(e.source); io.num(e.offset); io.num(e.index); S(io, e.type); S(io, e.semantic);
}
template <typename IO> void S(IO& io, VertexDeclarationContent& d) {
  S(io, d.id); io.num(d.content);
  Vec(io, d.elements, [](IO& io, VertexElement& e) { S(io, e); });
}
template <typename IO> void S(IO& io, TextureDesc& t) {
  S(io, t.id); io.str(t.name); S(io, t.type); io.num(t.width); io.num(t.height);
  io.num(t.depth); io.num(t.num_mipmaps); io.num(t.raw_pixel_format); io.num(t.usage);
  io.num(t.render_target); io.num(t.manual); io.num(t.content); Arr(io, t.fetch_constant);
  io.num(t.unresolved);
}
template <typename IO> void S(IO& io, ProgramDesc& p) { S(io, p.id); io.str(p.name); S(io, p.stage); }
template <typename IO> void S(IO& io, LiveTextureContent& t) {
  S(io, t.id); io.num(t.capture); io.num(t.live);
}
template <typename IO> void S(IO& io, ProgramSource& p) {
  io.str(p.name); io.str(p.language); io.str(p.entry_point); io.str(p.target); io.str(p.source);
}
template <typename IO> void S(IO& io, Blob& b) {
  io.num(b.hash); io.num(b.endian); io.num(b.endian_raw); io.bytes(b.bytes);
}
template <typename IO> void SResources(IO& io, Capture& c) {
  Vec(io, c.vertex_buffers, [](IO& io, VertexBufferDesc& d) { S(io, d); });
  Vec(io, c.index_buffers, [](IO& io, IndexBufferDesc& d) { S(io, d); });
  Vec(io, c.vertex_declarations, [](IO& io, VertexDeclarationContent& d) { S(io, d); });
  Vec(io, c.textures, [](IO& io, TextureDesc& d) { S(io, d); });
  Vec(io, c.programs, [](IO& io, ProgramDesc& d) { S(io, d); });
}
template <typename IO> void S(IO& io, SlotCount& s) {
  io.num(s.slot); io.str(s.name); io.num(s.attributable); io.str(s.note); io.num(s.total);
  io.num(s.captured);
}
template <typename IO> void S(IO& io, UnresolvedEntry& u) {
  io.num(u.frame); io.num(u.command); io.num(u.reason); io.str(u.detail);
}
template <typename IO> void S(IO& io, InstanceCountEntry& e) {
  io.num(e.frame); io.num(e.command); io.num(e.instance_count);
}
template <typename IO> void S(IO& io, Stats& s) {
  Vec(io, s.slots, [](IO& io, SlotCount& e) { S(io, e); });
  Vec(io, s.unresolved, [](IO& io, UnresolvedEntry& e) { S(io, e); });
  Vec(io, s.instance_counts_above_one, [](IO& io, InstanceCountEntry& e) { S(io, e); });
}
template <typename IO> void S(IO& io, ReferenceImage& r) {
  io.num(r.width); io.num(r.height); io.num(r.stride); io.num(r.possibly_misaligned);
  io.num(r.wait_ms); io.bytes(r.rgbx);
}

void WriteChunk(Writer& file, uint32_t tag, const Writer& body) {
  uint32_t t = tag;
  uint64_t len = body.out.size();
  file.num(t);
  file.num(len);
  file.out.insert(file.out.end(), body.out.begin(), body.out.end());
}

}  // namespace

std::string_view InternName(std::string_view s) {
  static std::mutex mutex;
  static std::unordered_set<std::string> names;  // node-based: addresses stay valid
  std::lock_guard lock(mutex);
  return *names.emplace(s).first;
}

Hash HashBytes(const uint8_t* data, size_t size, BlobEndian endian, uint8_t endian_raw) {
  uint64_t h = 0xCBF29CE484222325ull;
  auto mix = [&h](uint8_t b) {
    h ^= b;
    h *= 0x100000001B3ull;
  };
  mix(static_cast<uint8_t>(endian));
  mix(endian_raw);
  for (size_t i = 0; i < size; ++i) mix(data[i]);
  return h == 0 ? 1 : h;  // 0 means "no snapshot"
}

const char* ReasonName(UnresolvedReason reason) {
  switch (reason) {
    case UnresolvedReason::kNone: return "none";
    case UnresolvedReason::kUnknownEnumValue: return "unknown_enum_value";
    case UnresolvedReason::kBufferNotRegistered: return "buffer_not_registered";
    case UnresolvedReason::kBufferNoDeviceResource: return "buffer_no_device_resource";
    case UnresolvedReason::kBufferOutOfRange: return "buffer_out_of_range";
    case UnresolvedReason::kTextureNoName: return "texture_no_name";
    case UnresolvedReason::kTextureRenderTarget: return "texture_render_target";
    case UnresolvedReason::kTextureUnknownFormatSize: return "texture_unknown_format_size";
    case UnresolvedReason::kTextureNoDeviceResource: return "texture_no_device_resource";
    case UnresolvedReason::kProgramNotBound: return "program_not_bound";
    case UnresolvedReason::kNullPointer: return "null_pointer";
    case UnresolvedReason::kStateNotObserved: return "state_not_observed";
  }
  return "unknown_reason";
}

std::vector<uint8_t> SerializeCapture(const Capture& in) {
  Capture& c = const_cast<Capture&>(in);  // Serialize() is shared with the reader
  Writer file;
  file.out.insert(file.out.end(), std::begin(kMagic), std::end(kMagic));
  uint32_t major = kFormatVersionMajor, minor = kFormatVersionMinor;
  file.num(major);
  file.num(minor);

  Writer meta;
  S(meta, c.meta);
  WriteChunk(file, kTagMeta, meta);
  Writer resources;
  SResources(resources, c);
  WriteChunk(file, kTagResources, resources);
  Writer blobs;
  Vec(blobs, c.blobs, [](Writer& io, Blob& b) { S(io, b); });
  WriteChunk(file, kTagBlobs, blobs);
  Writer frames;
  Vec(frames, c.frames, [](Writer& io, Frame& f) {
    io.num(f.index);
    io.num(f.first_swap);
    uint64_t n = f.commands.size();
    io.num(n);
    for (auto& cmd : f.commands) WriteCommand(io, cmd);
  });
  WriteChunk(file, kTagFrames, frames);
  Writer stats;
  S(stats, c.stats);
  WriteChunk(file, kTagStats, stats);
  if (!c.live_blobs.empty() || !c.live_textures.empty()) {
    Writer live;
    Vec(live, c.live_blobs, [](Writer& io, Blob& b) { S(io, b); });
    Vec(live, c.live_textures, [](Writer& io, LiveTextureContent& t) { S(io, t); });
    WriteChunk(file, kTagLive, live);
  }
  if (!c.program_sources.empty()) {
    Writer sources;
    Vec(sources, c.program_sources, [](Writer& io, ProgramSource& p) { S(io, p); });
    WriteChunk(file, kTagProgramSources, sources);
  }
  if (c.reference) {
    Writer ref;
    S(ref, *c.reference);
    WriteChunk(file, kTagReference, ref);
  }
  return std::move(file.out);
}

bool DeserializeCapture(const std::vector<uint8_t>& bytes, Capture& c, std::string& error) {
  c = Capture{};
  if (bytes.size() < 16 || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
    error = "not a TLCAP file";
    return false;
  }
  Reader file(bytes.data() + 8, bytes.data() + bytes.size());
  uint32_t major = 0, minor = 0;
  file.num(major);
  file.num(minor);
  if (major != kFormatVersionMajor) {
    error = "unsupported major version " + std::to_string(major);
    return false;
  }
  while (file.ok() && !file.at_end()) {
    uint32_t tag = 0;
    uint64_t len = 0;
    file.num(tag);
    file.num(len);
    const uint8_t* body = file.position();
    if (!file.take(len)) break;
    Reader r(body, body + len);
    if (tag == kTagMeta) {
      S(r, c.meta);
    } else if (tag == kTagResources) {
      SResources(r, c);
    } else if (tag == kTagBlobs) {
      Vec(r, c.blobs, [](Reader& io, Blob& b) { S(io, b); });
    } else if (tag == kTagFrames) {
      Vec(r, c.frames, [](Reader& io, Frame& f) {
        io.num(f.index);
        io.num(f.first_swap);
        uint64_t n = 0;
        io.num(n);
        if (n > kMaxElements) {
          io.take(~0ull);
          return;
        }
        for (uint64_t i = 0; i < n && io.ok(); ++i) {
          if (!ReadCommand(io, f.commands)) io.take(~0ull);
        }
      });
    } else if (tag == kTagStats) {
      S(r, c.stats);
    } else if (tag == kTagLive) {
      Vec(r, c.live_blobs, [](Reader& io, Blob& b) { S(io, b); });
      Vec(r, c.live_textures, [](Reader& io, LiveTextureContent& t) { S(io, t); });
    } else if (tag == kTagProgramSources) {
      Vec(r, c.program_sources, [](Reader& io, ProgramSource& p) { S(io, p); });
    } else if (tag == kTagReference) {
      c.reference.emplace();
      S(r, *c.reference);
    }  // unknown tags are skipped
    if (!r.ok()) {
      error = "malformed chunk";
      return false;
    }
  }
  if (!file.ok()) {
    error = "truncated file";
    return false;
  }
  return true;
}

namespace {
template <typename IO>
void SSessionFrame(IO& io, SessionFrame& f) {
  io.num(f.swap);
  io.num(f.dropped_before);
  Vec(io, f.vertex_buffers, [](IO& io, VertexBufferDesc& d) { S(io, d); });
  Vec(io, f.index_buffers, [](IO& io, IndexBufferDesc& d) { S(io, d); });
  Vec(io, f.declarations, [](IO& io, VertexDeclarationContent& d) { S(io, d); });
  Vec(io, f.textures, [](IO& io, TextureDesc& d) { S(io, d); });
  Vec(io, f.programs, [](IO& io, ProgramDesc& d) { S(io, d); });
  Vec(io, f.program_sources, [](IO& io, ProgramSource& d) { S(io, d); });
  Vec(io, f.destroyed, [](IO& io, ResourceId& d) { S(io, d); });
  Vec(io, f.contents, [](IO& io, SessionContent& c) {
    S(io, c.id);
    io.num(c.bytes_in_record);
    if (c.bytes_in_record) {
      S(io, c.blob);
    } else {
      io.num(c.blob.hash);
    }
  });
}
}  // namespace

std::vector<uint8_t> SerializeSessionFrame(SessionFrame& frame) {
  Writer w;
  SSessionFrame(w, frame);
  uint64_t count = frame.commands.size();
  w.num(count);
  for (auto& c : frame.commands) WriteCommand(w, c);
  return std::move(w.out);
}

bool DeserializeSessionFrame(const uint8_t* data, size_t size, SessionFrame& frame,
                             std::string& error) {
  Reader r(data, data + size);
  frame = SessionFrame();
  SSessionFrame(r, frame);
  uint64_t count = 0;
  r.num(count);
  if (!r.ok() || count > kMaxElements) {
    error = "malformed session frame";
    return false;
  }
  for (uint64_t i = 0; i < count; ++i) {
    if (!ReadCommand(r, frame.commands)) {
      error = "malformed command in a session frame";
      return false;
    }
  }
  return true;
}

bool WriteCaptureFile(const std::string& path, const Capture& capture, std::string& error) {
  std::vector<uint8_t> bytes = SerializeCapture(capture);
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) {
    error = "cannot open " + path;
    return false;
  }
  f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  if (!f) {
    error = "write failed: " + path;
    return false;
  }
  return true;
}

bool ReadCaptureFile(const std::string& path, Capture& capture, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    error = "cannot open " + path;
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return DeserializeCapture(bytes, capture, error);
}

}  // namespace torchlight::commands
