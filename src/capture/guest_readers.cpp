#include "capture/guest_readers.h"

#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/system/xmemory.h>

#include "capture/constant_mirror.h"
#include "capture/session.h"
#include "capture/translate.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/xbox_d3d.h"

namespace torchlight::capture {

namespace {

namespace abi = guest_abi;
namespace ogre = guest_abi::ogre;
namespace xd3d = guest_abi::xbox_d3d;
using commands::UnresolvedReason;

// Largest buffer or texture snapshot taken from a fetch constant; anything bigger is treated as
// a corrupt descriptor.
constexpr uint32_t kMaxSnapshotBytes = 64u << 20;

uint32_t U32(const uint8_t* m, uint32_t a) { return abi::ReadU32(m, a); }

// Host pointer to the guest's bytes at a guest virtual address, translated as the generated code
// translates its own accesses on this platform (rex::memory::GuestPtr: plus 0x1000 from 0xE0000000
// up on Windows and macOS arm64, nothing elsewhere).
const uint8_t* GuestBytes(const uint8_t* m, uint32_t address) {
  return rex::memory::GuestPtr<const uint8_t*>(const_cast<uint8_t*>(m), address);
}

// In-order walk of an XDK std::map/set. Leaf children point back to the head node.
template <typename F>
void ForEachNode(const uint8_t* m, uint32_t tree, F&& f) {
  uint32_t head = U32(m, tree + ogre::stl_tree::kHead.offset);
  if (head == 0) return;
  uint32_t node = U32(m, head + ogre::stl_tree::kNodeParent.offset);
  std::vector<uint32_t> stack;
  size_t guard = 0;
  while ((node != head && node != 0) || !stack.empty()) {
    while (node != head && node != 0) {
      stack.push_back(node);
      node = U32(m, node + ogre::stl_tree::kNodeLeft.offset);
      if (++guard > (1u << 20)) return;
    }
    node = stack.back();
    stack.pop_back();
    f(node);
    node = U32(m, node + ogre::stl_tree::kNodeRight.offset);
  }
}

// Value of a map<pointer, pointer> entry (0 if absent).
uint32_t FindPointerEntry(const uint8_t* m, uint32_t tree, uint32_t key) {
  uint32_t value = 0;
  ForEachNode(m, tree, [&](uint32_t node) {
    if (value == 0 && U32(m, node + ogre::stl_tree::kNodeKey.offset) == key) {
      value = U32(m, node + ogre::stl_tree::kNodeValue.offset);
    }
  });
  return value;
}

uint32_t ActiveDevice(const uint8_t* m) { return U32(m, xd3d::kActiveDeviceGlobal); }

uint32_t Bit(UnresolvedReason r) { return 1u << uint32_t(r); }

}  // namespace

std::array<float, 16> ReadMatrix(const uint8_t* m, uint32_t matrix) {
  std::array<float, 16> out{};
  if (matrix == 0) return out;
  for (uint32_t i = 0; i < 16; ++i) out[i] = abi::ReadF32(m, matrix + 4 * i);
  return out;
}

std::array<float, 4> ReadColour(const uint8_t* m, uint32_t colour) {
  if (colour == 0) return {};
  return {abi::ReadF32(m, colour + ogre::colour_value::kR.offset),
          abi::ReadF32(m, colour + ogre::colour_value::kG.offset),
          abi::ReadF32(m, colour + ogre::colour_value::kB.offset),
          abi::ReadF32(m, colour + ogre::colour_value::kA.offset)};
}

commands::SetRenderTarget ReadRenderTarget(const uint8_t* m, uint32_t target) {
  commands::SetRenderTarget c;
  c.guest_address = target;
  if (target == 0) return c;
  c.name = ogre::ReadString(m, target + ogre::render_target::kName.offset);
  c.width = abi::ReadU32(m, target, ogre::render_target::kWidth);
  c.height = abi::ReadU32(m, target, ogre::render_target::kHeight);
  c.colour_depth = abi::ReadU32(m, target, ogre::render_target::kColourDepth);
  uint32_t impl = abi::ReadVirtual(m, target, ogre::render_target::kRequiresTextureFlipping);
  if (impl != abi::functions::kReturnFalse.address) {
    Session::Get().AddUnresolved(UnresolvedReason::kStateNotObserved,
                                 fmt::format("render target {:#x}: requiresTextureFlipping is "
                                             "{:#x}, not the known false function",
                                             target, impl));
  }
  c.requires_flipping = false;
  return c;
}

commands::SetViewport ReadViewport(const uint8_t* m, uint32_t viewport) {
  commands::SetViewport c;
  c.guest_address = viewport;
  if (viewport == 0) return c;
  c.target_guest_address = abi::ReadU32(m, viewport, ogre::viewport::kTarget);
  c.left = int32_t(abi::ReadU32(m, viewport, ogre::viewport::kActLeft));
  c.top = int32_t(abi::ReadU32(m, viewport, ogre::viewport::kActTop));
  c.width = int32_t(abi::ReadU32(m, viewport, ogre::viewport::kActWidth));
  c.height = int32_t(abi::ReadU32(m, viewport, ogre::viewport::kActHeight));
  return c;
}

commands::TextureDesc ReadTextureDesc(const uint8_t* m, uint32_t texture) {
  commands::TextureDesc d;
  d.name = ogre::ReadString(m, texture + ogre::resource::kName.offset);
  d.type = ToTextureType(abi::ReadU32(m, texture, ogre::texture::kTextureType));
  d.width = abi::ReadU32(m, texture, ogre::texture::kWidth);
  d.height = abi::ReadU32(m, texture, ogre::texture::kHeight);
  d.depth = abi::ReadU32(m, texture, ogre::texture::kDepth);
  d.num_mipmaps = abi::ReadU32(m, texture, ogre::texture::kNumMipmaps);
  d.raw_pixel_format = abi::ReadU32(m, texture, ogre::texture::kFormat);
  d.usage = abi::ReadU32(m, texture, ogre::texture::kUsage);
  d.manual = abi::ReadBool(m, texture, ogre::resource::kIsManual);
  d.render_target = (d.usage & ogre::texture_usage::kRenderTarget.value) != 0;
  return d;
}

std::optional<commands::ResourceId> CaptureTexture(const uint8_t* m, uint32_t texture) {
  Session& s = Session::Get();
  if (texture == 0) return std::nullopt;
  auto info = s.Lookup(commands::ResourceKind::kTexture, texture);
  if (!info) {
    s.AddUnresolved(UnresolvedReason::kBufferNotRegistered,
                    fmt::format("texture {:#x} was never constructed under the hooks", texture));
    return std::nullopt;
  }
  if (!s.recording()) return info->id;
  // Already in the live stream with a description that cannot change (session.h).
  if (s.LiveTextureDescribed(info->id)) return info->id;

  commands::TextureDesc d = ReadTextureDesc(m, texture);
  d.id = info->id;
  commands::Hash live_content = 0;
  if (d.render_target) {
    d.unresolved = UnresolvedReason::kTextureRenderTarget;
  } else if (d.name.empty() || d.manual) {
    // Dynamic or unnamed: keep the raw Xenos base level (tiled, not decoded).
    uint32_t resources =
        FindPointerEntry(m, texture + ogre::d3d9_texture::kDeviceToTextureResourcesMap.offset,
                         ActiveDevice(m));
    uint32_t base = resources ? abi::ReadU32(m, resources, ogre::d3d9_texture_resources::kBaseTex)
                              : 0;
    if (base == 0) {
      d.unresolved = UnresolvedReason::kTextureNoDeviceResource;
    } else {
      for (uint32_t i = 0; i < xd3d::base_texture::kFetchDwordCount; ++i) {
        d.fetch_constant[i] = U32(m, base + xd3d::base_texture::kFetchDword0.offset + 4 * i);
      }
      uint32_t dword1 = d.fetch_constant[1];
      uint32_t format = dword1 & xd3d::base_texture::kFormatMask;
      uint32_t endian = (dword1 >> xd3d::base_texture::kEndianShift) & 3;
      uint32_t physical = dword1 & xd3d::base_texture::kAddressMask;
      const xd3d::base_texture::FormatBlock* block = nullptr;
      for (const auto& b : xd3d::base_texture::kFormatBlocks) {
        if (b.format == format) block = &b;
      }
      if (block == nullptr) {
        d.unresolved = UnresolvedReason::kTextureUnknownFormatSize;
      } else {
        auto align = [](uint32_t v, uint32_t a) { return (v + a - 1) / a * a; };
        uint32_t bw = align((d.width + block->block_dim - 1) / block->block_dim,
                            xd3d::base_texture::kTileBlocks);
        uint32_t bh = align((d.height + block->block_dim - 1) / block->block_dim,
                            xd3d::base_texture::kTileBlocks);
        uint64_t size = uint64_t(bw) * bh * block->bytes_per_block *
                        (d.depth > 1 ? d.depth : 1);
        if (physical == 0 || size == 0 || size > kMaxSnapshotBytes) {
          d.unresolved = UnresolvedReason::kBufferOutOfRange;
        } else {
          const uint8_t* p = GuestBytes(m, xd3d::PhysicalToVirtual(physical));
          // Content version: that of the base level's pixel buffer (surface 0), whose unlocks
          // and blits write it.
          uint32_t surfaces = texture + ogre::d3d9_texture::kSurfaceList.offset;
          uint32_t first = U32(m, surfaces + ogre::stl_vector::kFirst.offset);
          uint32_t pixel_buffer = first ? U32(m, first + ogre::shared_ptr::kPRep.offset) : 0;
          auto content = s.RecordContent(d.id, pixel_buffer ? pixel_buffer : texture, p,
                                         size_t(size), commands::BlobEndian::kTextureFetch,
                                         uint8_t(endian));
          d.content = content.capture;
          live_content = content.live;
        }
      }
    }
  }
  if (d.unresolved != UnresolvedReason::kNone) {
    s.AddUnresolved(d.unresolved, fmt::format("texture {:#x} '{}'", texture, d.name));
  }
  s.AddTexture(std::move(d), live_content);
  return info->id;
}

commands::SetVertexDeclaration CaptureVertexDeclaration(const uint8_t* m, uint32_t declaration) {
  Session& s = Session::Get();
  commands::SetVertexDeclaration c;
  if (declaration == 0) return c;
  auto info = s.Lookup(commands::ResourceKind::kVertexDeclaration, declaration);
  if (!info) {
    s.AddUnresolved(UnresolvedReason::kBufferNotRegistered,
                    fmt::format("vertex declaration {:#x} never constructed", declaration));
    return c;
  }
  c.declaration = info->id;
  if (!s.recording()) return c;
  uint32_t list = declaration + ogre::vertex_declaration::kElementList.offset;
  uint32_t first = U32(m, list + ogre::stl_vector::kFirst.offset);
  uint32_t last = U32(m, list + ogre::stl_vector::kLast.offset);
  uint32_t stride = ogre::vertex_element::kSize.bytes;
  commands::VertexDeclarationContent content;
  content.id = info->id;
  if (first != 0 && last >= first && (last - first) / stride <= 64) {
    content.content =
        commands::HashBytes(m + first, last - first, commands::BlobEndian::kGuestCpuBigEndian, 0);
    // Already in the live stream with this content: its elements are not read again (session.h).
    if (s.LiveDeclarationDescribed(info->id, content.content)) {
      c.content = content.content;
      return c;
    }
    for (uint32_t e = first; e < last; e += stride) {
      commands::VertexElement el;
      el.source = abi::ReadU16(m, e, ogre::vertex_element::kSource);
      el.offset = abi::ReadU32(m, e, ogre::vertex_element::kOffset);
      el.index = abi::ReadU16(m, e, ogre::vertex_element::kIndex);
      el.type = ToVertexType(abi::ReadU32(m, e, ogre::vertex_element::kType));
      el.semantic = ToVertexSemantic(abi::ReadU32(m, e, ogre::vertex_element::kSemantic));
      if (!el.type.known() || !el.semantic.known()) {
        s.AddUnresolved(UnresolvedReason::kUnknownEnumValue,
                        fmt::format("declaration {:#x} element type {} semantic {}", declaration,
                                    el.type.raw, el.semantic.raw));
      }
      content.elements.push_back(el);
    }
  }
  c.content = content.content;
  s.AddVertexDeclaration(std::move(content));
  return c;
}

commands::SetVertexBuffers ReadVertexBufferBinding(const uint8_t* m, uint32_t binding) {
  commands::SetVertexBuffers c;
  if (binding == 0) return c;
  ForEachNode(m, binding + ogre::vertex_buffer_binding::kBindingMap.offset, [&](uint32_t node) {
    commands::VertexStream stream;
    stream.stream = abi::ReadU16(m, node + ogre::stl_tree::kNodeKey.offset);
    uint32_t buffer =
        U32(m, node + ogre::stl_tree::kNodeValue.offset + ogre::shared_ptr::kPRep.offset);
    if (auto info = Session::Get().Lookup(commands::ResourceKind::kVertexBuffer, buffer)) {
      stream.buffer = info->id;
    }
    c.streams.push_back(stream);
  });
  return c;
}

namespace {

// Pushes exactly one live content key per call (0 when there is no content).
commands::BufferSnapshot SnapshotBuffer(const uint8_t* m, commands::ResourceKind kind,
                                        uint32_t buffer, uint32_t& unresolved,
                                        std::vector<commands::Hash>& live_keys) {
  Session& s = Session::Get();
  commands::BufferSnapshot snap;
  live_keys.push_back(0);
  auto info = s.Lookup(kind, buffer);
  bool vertex = kind == commands::ResourceKind::kVertexBuffer;
  if (!info) {
    unresolved |= Bit(UnresolvedReason::kBufferNotRegistered);
    s.AddUnresolved(UnresolvedReason::kBufferNotRegistered,
                    fmt::format("{} buffer {:#x}", vertex ? "vertex" : "index", buffer));
    return snap;
  }
  snap.buffer = info->id;
  if (vertex) s.AddVertexBuffer(*info);
  else s.AddIndexBuffer(*info);

  uint32_t map = buffer + (vertex ? ogre::d3d9_hardware_vertex_buffer::kDeviceToResourcesMap
                                  : ogre::d3d9_hardware_index_buffer::kDeviceToResourcesMap)
                              .offset;
  uint32_t resources = FindPointerEntry(m, map, ActiveDevice(m));
  uint32_t object = resources ? abi::ReadU32(m, resources, ogre::d3d9_buffer_resources::kBuffer)
                              : 0;
  uint32_t physical = 0, size = 0;
  commands::BlobEndian endian = commands::BlobEndian::kGuestCpuBigEndian;
  uint8_t endian_raw = 0;
  if (object != 0) {
    if (vertex) {
      uint32_t d0 = abi::ReadU32(m, object, xd3d::vertex_buffer::kFetchDword0);
      uint32_t d1 = abi::ReadU32(m, object, xd3d::vertex_buffer::kFetchDword1);
      physical = d0 & xd3d::vertex_buffer::kAddressMask;
      size = d1 & xd3d::vertex_buffer::kSizeMask;
      endian = commands::BlobEndian::kVertexFetch;
      endian_raw = uint8_t(d1 & xd3d::vertex_buffer::kEndianMask);
    } else {
      physical = abi::ReadU32(m, object, xd3d::index_buffer::kAddress);
      size = abi::ReadU32(m, object, xd3d::index_buffer::kSize);
    }
    if (physical == 0 || size == 0 || size > kMaxSnapshotBytes) {
      unresolved |= Bit(UnresolvedReason::kBufferOutOfRange);
      s.AddUnresolved(UnresolvedReason::kBufferOutOfRange,
                      fmt::format("buffer {:#x} fetch address {:#x} size {}", buffer, physical,
                                  size));
      return snap;
    }
    snap.source = 1;
    snap.guest_virtual = xd3d::PhysicalToVirtual(physical);
    snap.size = size;
    auto content = s.RecordContent(info->id, buffer, GuestBytes(m, snap.guest_virtual),
                                   size, endian, endian_raw);
    snap.blob = content.capture;
    live_keys.back() = content.live;
    return snap;
  }
  uint32_t sysmem = abi::ReadU32(
      m, buffer,
      vertex ? ogre::d3d9_hardware_vertex_buffer::kSystemMemoryBuffer
             : ogre::d3d9_hardware_index_buffer::kSystemMemoryBuffer);
  uint32_t bytes = abi::ReadU32(m, buffer, ogre::hardware_buffer::kSizeInBytes);
  if (sysmem != 0 && bytes != 0 && bytes <= kMaxSnapshotBytes) {
    snap.source = 2;
    snap.guest_virtual = sysmem;
    snap.size = bytes;
    auto content = s.RecordContent(info->id, buffer, m + sysmem, bytes,
                                   commands::BlobEndian::kGuestCpuBigEndian, 0);
    snap.blob = content.capture;
    live_keys.back() = content.live;
    return snap;
  }
  unresolved |= Bit(UnresolvedReason::kBufferNoDeviceResource);
  s.AddUnresolved(UnresolvedReason::kBufferNoDeviceResource,
                  fmt::format("buffer {:#x}: no resource for the active device and no system "
                              "memory copy",
                              buffer));
  return snap;
}

}  // namespace

commands::Draw CaptureDraw(const uint8_t* m, uint32_t render_system, uint32_t operation,
                           std::vector<commands::Hash>& live_keys) {
  Session& s = Session::Get();
  commands::Draw d;
  uint32_t vertex_data = abi::ReadU32(m, operation, ogre::render_operation::kVertexData);
  uint32_t index_data = abi::ReadU32(m, operation, ogre::render_operation::kIndexData);
  d.primitive = ToPrimitive(abi::ReadU32(m, operation, ogre::render_operation::kOperationType));
  if (!d.primitive.known()) d.unresolved_mask |= Bit(UnresolvedReason::kUnknownEnumValue);
  d.indexed = abi::ReadBool(m, operation, ogre::render_operation::kUseIndexes);
  d.instance_count = abi::ReadU32(m, operation, ogre::render_operation::kRunicInstanceCount);
  d.invert_winding = abi::ReadBool(m, render_system, ogre::render_system::kInvertVertexWinding);
  d.pass_iteration_count =
      abi::ReadU32(m, render_system, ogre::render_system::kCurrentPassIterationCount);
  uint32_t target = abi::ReadU32(m, render_system, ogre::render_system::kActiveRenderTarget);
  d.target_flipping =
      target != 0 &&
      abi::ReadVirtual(m, target, ogre::render_target::kRequiresTextureFlipping) !=
          abi::functions::kReturnFalse.address;
  if (vertex_data == 0) {
    d.unresolved_mask |= Bit(UnresolvedReason::kNullPointer);
    s.AddUnresolved(UnresolvedReason::kNullPointer, "RenderOperation::vertexData is null");
    return d;
  }
  d.vertex_start = abi::ReadU32(m, vertex_data, ogre::vertex_data::kVertexStart);
  d.vertex_count = abi::ReadU32(m, vertex_data, ogre::vertex_data::kVertexCount);
  uint32_t binding = abi::ReadU32(m, vertex_data, ogre::vertex_data::kVertexBufferBinding);
  if (binding != 0) {
    ForEachNode(m, binding + ogre::vertex_buffer_binding::kBindingMap.offset, [&](uint32_t node) {
      uint32_t buffer =
          U32(m, node + ogre::stl_tree::kNodeValue.offset + ogre::shared_ptr::kPRep.offset);
      d.vertex_buffers.push_back(
          SnapshotBuffer(m, commands::ResourceKind::kVertexBuffer, buffer, d.unresolved_mask,
                         live_keys));
    });
  }
  if (d.indexed) {
    if (index_data == 0) {
      d.unresolved_mask |= Bit(UnresolvedReason::kNullPointer);
      s.AddUnresolved(UnresolvedReason::kNullPointer, "indexed draw with null indexData");
    } else {
      d.index_start = abi::ReadU32(m, index_data, ogre::index_data::kIndexStart);
      d.index_count = abi::ReadU32(m, index_data, ogre::index_data::kIndexCount);
      uint32_t ib = U32(m, index_data + ogre::index_data::kIndexBuffer.offset +
                               ogre::shared_ptr::kPRep.offset);
      if (ib != 0) {
        d.index_size = abi::ReadU32(m, ib, ogre::hardware_index_buffer::kIndexSize);
        d.index_buffer =
            SnapshotBuffer(m, commands::ResourceKind::kIndexBuffer, ib, d.unresolved_mask,
                           live_keys);
      }
    }
  }
  if (d.instance_count > 1) s.AddInstanceCount(d.instance_count);
  return d;
}

commands::SetConstants ReadConstants(const uint8_t* m, uint32_t parameters, uint32_t gptype,
                                     uint32_t mask, ConstantMirror* mirror, bool filter) {
  namespace gpp = ogre::gpu_program_parameters;
  namespace node = ogre::gpu_logical_index_use_node;
  commands::SetConstants c;
  c.stage = ToStage(gptype);
  c.mask = uint16_t(mask);
  c.parameters_guest_address = parameters;
  if (parameters == 0) return c;
  c.transpose_matrices = abi::ReadBool(m, parameters, gpp::kTransposeMatrices);
  auto read_ranges = [&](abi::Field logical, abi::Field values, bool floats,
                         std::vector<commands::ConstantRange>& out) {
    uint32_t data = U32(m, parameters + values.offset + ogre::stl_vector::kFirst.offset);
    uint32_t structure = U32(m, parameters + logical.offset + ogre::shared_ptr::kPRep.offset);
    if (structure == 0 || data == 0) return;
    ForEachNode(m, structure + ogre::gpu_logical_buffer_struct::kMap.offset, [&](uint32_t n) {
      uint16_t variability = abi::ReadU16(m, n, node::kVariability);
      if ((variability & mask) == 0) return;
      commands::ConstantRange r;
      r.logical_index = abi::ReadU32(m, n, node::kLogicalIndex);
      r.register_index = floats ? r.logical_index / 4 : r.logical_index;
      r.element_count = abi::ReadU32(m, n, node::kCurrentSize);
      r.variability = variability;
      uint32_t physical = abi::ReadU32(m, n, node::kPhysicalIndex);
      r.physical_index = physical;
      if (r.element_count > 4096) return;
      // Read into a scratch buffer first: a range the backend already holds is left out without
      // allocating (live commands, `filter`).
      thread_local std::vector<uint32_t> values;
      values.resize(r.element_count);
      for (uint32_t i = 0; i < r.element_count; ++i) values[i] = U32(m, data + 4 * (physical + i));
      const uint8_t stage = c.stage.value;
      if (mirror && filter && mirror->Holds(floats, stage, physical, values.data(), r.element_count))
        return;
      if (mirror) mirror->Store(floats, stage, physical, values.data(), r.element_count);
      r.data.assign(values.begin(), values.end());
      out.push_back(std::move(r));
    });
  };
  read_ranges(gpp::kFloatLogicalToPhysical, gpp::kFloatConstants, true, c.floats);
  read_ranges(gpp::kIntLogicalToPhysical, gpp::kIntConstants, false, c.ints);

  uint32_t first = U32(m, parameters + gpp::kAutoConstants.offset + ogre::stl_vector::kFirst.offset);
  uint32_t last = U32(m, parameters + gpp::kAutoConstants.offset + ogre::stl_vector::kLast.offset);
  uint32_t stride = ogre::auto_constant_entry::kSize.bytes;
  if (first != 0 && last >= first && (last - first) / stride <= 512) {
    c.autos.reserve((last - first) / stride);
    for (uint32_t e = first; e < last; e += stride) {
      commands::AutoConstant a;
      a.raw_type = abi::ReadU32(m, e, ogre::auto_constant_entry::kParamType);
      a.physical_index = abi::ReadU32(m, e, ogre::auto_constant_entry::kPhysicalIndex);
      a.element_count = abi::ReadU32(m, e, ogre::auto_constant_entry::kElementCount);
      a.data = abi::ReadU32(m, e, ogre::auto_constant_entry::kData);
      a.variability = abi::ReadU16(m, e, ogre::auto_constant_entry::kVariability);
      if (a.raw_type < ogre::kAutoConstantTypeCount) {
        a.name = ogre::kAutoConstantTypes[a.raw_type].name;
      } else {
        Session::Get().AddUnresolved(UnresolvedReason::kUnknownEnumValue,
                                     fmt::format("auto constant type {}", a.raw_type));
      }
      c.autos.push_back(std::move(a));
    }
  }
  return c;
}

commands::BindProgram CaptureProgram(const uint8_t* m, uint32_t program) {
  commands::BindProgram c;
  // Identity from the createGpuProgram hook (generation per program object at the address).
  auto info = Session::Get().Lookup(commands::ResourceKind::kProgram, program);
  c.program = info ? info->id : commands::ResourceId{commands::ResourceKind::kProgram, program, 0};
  if (program == 0) return c;
  c.stage = ToStage(abi::ReadU32(m, program, ogre::gpu_program::kType));
  if (Session::Get().recording()) {
    commands::ProgramDesc d;
    d.id = c.program;
    d.name = ogre::ReadString(m, program + ogre::resource::kName.offset);
    d.stage = c.stage;
    Session::Get().AddProgram(std::move(d));
  }
  return c;
}

}  // namespace torchlight::capture
