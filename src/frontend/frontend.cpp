#include "frontend/frontend.h"

#include <algorithm>
#include <chrono>
#include <bit>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <variant>

#include "frontend/detile.h"

namespace torchlight::frontend {

using namespace torchlight::commands;

namespace {

using Mat = std::array<float, 16>;

// Backend texture id of the flat fallback used for textures that are not ready (PackId never
// produces it: its top byte is the resource kind + 1).
constexpr uint64_t kFallbackTexture = 1;

Mat Multiply(const Mat& a, const Mat& b) {
  Mat r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
  return r;
}

}  // namespace

uint64_t PackId(const ResourceId& id) {
  return (uint64_t(uint8_t(id.kind)) + 1) << 56 | (uint64_t(id.generation) & 0xFFFFFF) << 32 |
         id.guest_address;
}

size_t Counter::total() const {
  size_t t = 0;
  for (const auto& [k, v] : counts) t += v;
  return t;
}

// ---- guest constant register files ------------------------------------------------------------

std::optional<Frontend::Mat> Frontend::StageConstants::Raw(const AutoConstant& a, uint32_t count) const {
  Mat m{};
  for (uint32_t i = 0; i < count && i < 16; ++i) {
    auto it = physical.find(a.physical_index + i);
    if (it == physical.end()) return std::nullopt;
    m[i] = it->second;
  }
  return m;
}
// Matrix in OGRE's row-major, column-vector layout.
std::optional<Frontend::Mat> Frontend::StageConstants::Auto(const std::string& name) const {
  for (const auto& a : autos) {
    if (a.name != name) continue;
    auto m = Raw(a, 16);
    if (!m || !transposed) return m;
    Mat t{};
    for (int i = 0; i < 4; ++i)
      for (int j = 0; j < 4; ++j) t[i * 4 + j] = (*m)[j * 4 + i];
    return t;
  }
  return std::nullopt;
}
// Constant register `reg` (4 floats) as last written to the guest register file.
std::optional<std::array<float, 4>> Frontend::StageConstants::Register(uint32_t reg) const {
  std::array<float, 4> v{};
  for (uint32_t i = 0; i < 4; ++i) {
    auto it = physical.find(reg * 4 + i);
    if (it == physical.end()) return std::nullopt;
    v[i] = it->second;
  }
  return v;
}
// Matrix global of `registers` rows starting at `first` (rows the program never reads are
// dropped by the compiler and come back as identity rows). With mTransposeMatrices the guest
// wrote the transpose, so the registers hold columns.
Frontend::Mat Frontend::StageConstants::Matrix(uint32_t first, uint32_t registers) const {
  Mat m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  for (uint32_t r = 0; r < registers && r < 4; ++r) {
    auto v = Register(first + r);
    if (!v) continue;
    for (uint32_t c = 0; c < 4; ++c) {
      if (transposed) m[c * 4 + r] = (*v)[c];
      else m[r * 4 + c] = (*v)[c];
    }
  }
  return m;
}
// Captures older than 1.2 lack the transpose flag: an affine 4x4 auto constant of the same
// upload shows the layout (bottom row 0 0 0 1 -> as is; right column 0 0 0 1 with the
// translation in the bottom row -> transposed).
void Frontend::StageConstants::InferLayout() {
  static const char* kAffine[] = {"ACT_WORLD_MATRIX", "ACT_VIEW_MATRIX",
                                  "ACT_INVERSE_WORLD_MATRIX", "ACT_INVERSE_VIEW_MATRIX"};
  for (const auto& a : autos) {
    bool affine = false;
    for (const char* n : kAffine) affine |= a.name == n && a.element_count >= 16;
    if (!affine) continue;
    auto m = Raw(a, 16);
    if (!m) continue;
    auto near = [](float x, float y) { return std::fabs(x - y) < 1e-5f; };
    bool bottom_identity = near((*m)[12], 0) && near((*m)[13], 0) && near((*m)[14], 0) &&
                           near((*m)[15], 1);
    bool right_identity = near((*m)[3], 0) && near((*m)[7], 0) && near((*m)[11], 0) &&
                          near((*m)[15], 1);
    if (bottom_identity && !right_identity) {
      transposed = false;
      layout = LayoutSource::kInferred;
      return;
    }
    if (right_identity && !bottom_identity) {
      transposed = true;
      layout = LayoutSource::kInferred;
      return;
    }
  }
  layout = LayoutSource::kUnknown;
}

// ---- frontend ---------------------------------------------------------------------------------

Frontend::Frontend(tl_backend* backend, const ContentSource& content)
    : backend_(backend), content_(content) {
  state_.blend_src = uint8_t(BlendFactor::kOne);
  state_.blend_dst = uint8_t(BlendFactor::kZero);
  state_.depth_check = state_.depth_write = 1;
  state_.depth_func_requested = state_.depth_func_effective = uint8_t(CompareFunc::kLessEqual);
  state_.alpha_func = uint8_t(CompareFunc::kAlways);
  state_.colour_write = 0xF;
  state_.polygon_mode = uint8_t(PolygonMode::kSolid);
  // Fallback for textures that are not ready: flat dark magenta, visible on purpose.
  const uint8_t kFallbackBgra[4] = {0x60, 0x00, 0x60, 0xFF};
  tl_backend_texture_linear(backend_, kFallbackTexture, 1, 1, TL_LINEAR_BGRA8, kFallbackBgra,
                            sizeof(kFallbackBgra));
}

bool Frontend::TextureReady(const ResourceId& id) const {
  return ready_textures_.count(PackId(id)) != 0;
}

void Frontend::ReleaseContent(Hash key) {
  if (uploaded_buffers_.erase(key)) tl_backend_release_buffer(backend_, key);
}

void Frontend::ReleaseBufferOwner(const ResourceId& id) {
  tl_backend_release_buffer_owner(backend_, PackId(id));
}

void Frontend::ReleaseTexture(const ResourceId& id) {
  uint64_t key = PackId(id);
  if (ready_textures_.erase(key)) tl_backend_release_texture(backend_, key);
  texture_content_.erase(key);
}

void Frontend::PrepareTexture(const TextureDesc& t) {
  uint64_t key = PackId(t.id);
  const bool dynamic = !t.render_target && (t.name.empty() || t.manual) && t.content != 0;
  if (ready_textures_.count(key)) {
    auto content = texture_content_.find(key);
    if (content == texture_content_.end() || content->second == t.content) return;
    // New content version: a dynamic texture is uploaded again under the same id (the backend
    // rewrites it in place); anything else is created again.
    if (dynamic) {
      ready_textures_.erase(key);
    } else {
      ReleaseTexture(t.id);
    }
  }
  texture_content_[key] = t.content;
  char err[512] = {};
  bool ok = false;
  if (t.render_target) {
    ok = tl_backend_render_target(backend_, key, t.width, t.height) == 0;
    stats_.render_targets += ok;
    if (!ok) stats_.texture_problems.Add("render target creation failed");
  } else if (!t.name.empty() && !t.manual) {
    auto data = content_.GameFile(t.name);
    if (!data) {
      stats_.texture_problems.Add("not found in pak.zip");
    } else {
      std::string ext = std::filesystem::path(t.name).extension().string();
      if (!ext.empty()) ext = ext.substr(1);
      for (auto& ch : ext) ch = char(std::tolower(uint8_t(ch)));
      uint32_t type = t.type.value == uint8_t(TextureType::kCubeMap) ? TL_TEXTURE_CUBE
                                                                      : TL_TEXTURE_2D;
      ok = tl_backend_texture_file(backend_, key, t.name.c_str(), data->data(),
                                   uint32_t(data->size()), ext.c_str(), type, err,
                                   sizeof(err)) == 0;
      if (ok) ++stats_.named_loaded;
      else stats_.texture_problems.Add(std::string("image load failed: ") + err);
    }
  } else if (t.content != 0) {
    const commands::Blob* blob = content_.Blob(t.content);
    LinearTexture linear;
    std::string reason;
    if (!blob) {
      stats_.texture_problems.Add("dynamic texture without its content");
    } else if (!DetileTexture(t, *blob, linear, reason)) {
      stats_.texture_problems.Add("dynamic texture: " + reason);
    } else {
      ok = tl_backend_texture_linear(backend_, key, linear.width, linear.height, linear.format,
                                     linear.data.data(), uint32_t(linear.data.size())) == 0;
      if (ok) ++stats_.dynamic_loaded;
      else stats_.texture_problems.Add("dynamic texture upload failed");
    }
    // A failed new version must not leave the previous one in the backend under this id.
    if (!ok) tl_backend_release_texture(backend_, key);
  } else {
    stats_.texture_problems.Add(std::string("no content: ") + ReasonName(t.unresolved));
  }
  if (ok) ready_textures_.insert(key);
}

bool Frontend::EnsureBuffer(const BufferSnapshot& s, bool vertex, uint32_t index_size) {
  if (s.blob == 0) return false;
  if (uploaded_buffers_.count(s.blob)) return true;
  const commands::Blob* blob = content_.Blob(s.blob);
  if (!blob) return false;
  int r = vertex ? tl_backend_vertex_buffer(backend_, s.blob, blob->bytes.data(),
                                            uint32_t(blob->bytes.size()),
                                            blob->endian == BlobEndian::kVertexFetch ? blob->endian_raw : 0)
                 : tl_backend_index_buffer(backend_, s.blob, blob->bytes.data(),
                                           uint32_t(blob->bytes.size()), index_size);
  if (r != 0) return false;
  uploaded_buffers_.insert(s.blob);
  return true;
}

void Frontend::BeginFrame() { tl_backend_begin(backend_); }

void Frontend::EndFrame() { tl_backend_end(backend_); }

// The scene clip follows the viewport the draws are in (SetSceneClip names the scene's).
void Frontend::ApplySceneClip() {
  tl_backend_set_scene_clip(backend_, scene_clip_viewport_ != 0 && viewport_ &&
                                          viewport_->guest_address == scene_clip_viewport_);
}

void Frontend::Execute(const Command& command) {
  std::visit(
      [&](const auto& p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, SetRenderTarget>) {
          uint64_t target = 0;
          if (p.name.rfind("rtt/", 0) == 0) {
            // Guest D3D9 names render textures "rtt/<pixel buffer>/<texture name>"
            // (OgreD3D9HardwarePixelBuffer.cpp:969; "rtt/" at guest 0x820F71F4).
            std::string texture_name = p.name.substr(p.name.rfind('/') + 1);
            const TextureDesc* t = content_.RenderTargetTexture(texture_name);
            if (t && TextureReady(t->id)) target = PackId(t->id);
          } else if (p.width && p.height &&
                     (p.width != guest_width_ || p.height != guest_height_)) {
            // The guest's window target: the frame is drawn at its size.
            if (tl_backend_set_guest_size(backend_, p.width, p.height) == 0) {
              guest_width_ = p.width;
              guest_height_ = p.height;
            }
          }
          tl_backend_set_target(backend_, target);
          // A viewport belongs to its target. The guest D3D9 _setViewport binds the
          // viewport's target from inside (OgreD3D9RenderSystem.cpp:2778), so that
          // SetRenderTarget is recorded after the SetViewport that caused it; binding the
          // viewport's own target keeps the viewport rectangle.
          if (viewport_ && viewport_->target_guest_address == p.guest_address) {
            tl_backend_set_viewport(backend_, viewport_->left, viewport_->top, viewport_->width,
                                    viewport_->height);
          }
        } else if constexpr (std::is_same_v<T, SetGammaRamp>) {
          tl_backend_set_gamma_ramp(backend_, p.pwl, p.values.data());
          stats_.gamma_ramp = p.pwl ? "PWL" : "table";
        } else if constexpr (std::is_same_v<T, SetSceneClip>) {
          scene_clip_viewport_ = p.viewport;
          ApplySceneClip();
        } else if constexpr (std::is_same_v<T, SetViewport>) {
          viewport_ = p;
          tl_backend_set_viewport(backend_, p.left, p.top, p.width, p.height);
          ApplySceneClip();
        } else if constexpr (std::is_same_v<T, Clear>) {
          tl_backend_clear(backend_, p.buffers, p.colour.data(), p.depth, p.stencil);
        } else if constexpr (std::is_same_v<T, SetBlend>) {
          state_.blend_src = p.src.value;
          state_.blend_dst = p.dst.value;
          blend_op_ = p.op.value;
        } else if constexpr (std::is_same_v<T, SetDepthCheck>) {
          state_.depth_check = p.enabled;
        } else if constexpr (std::is_same_v<T, SetDepthWrite>) {
          state_.depth_write = p.enabled;
        } else if constexpr (std::is_same_v<T, SetDepthFunc>) {
          state_.depth_func_requested = p.requested.value;
          state_.depth_func_effective = p.effective.value;
        } else if constexpr (std::is_same_v<T, SetDepthBias>) {
          state_.depth_bias_constant = p.constant;
          state_.depth_bias_slope = p.slope_scale;
        } else if constexpr (std::is_same_v<T, SetCull>) {
          state_.cull_mode = p.mode.value;
        } else if constexpr (std::is_same_v<T, SetInvertWinding>) {
          state_.invert_winding = p.invert;
        } else if constexpr (std::is_same_v<T, SetAlphaReject>) {
          state_.alpha_func = p.func.value;
          state_.alpha_ref = uint8_t(p.reference);
        } else if constexpr (std::is_same_v<T, SetColourWrite>) {
          state_.colour_write = uint8_t(p.r | p.g << 1 | p.b << 2 | p.a << 3);
        } else if constexpr (std::is_same_v<T, SetPolygonMode>) {
          state_.polygon_mode = p.mode.value;
        } else if constexpr (std::is_same_v<T, SetTexture>) {
          if (!p.vertex_texture) units_[p.unit].texture = p.enabled ? p.texture : std::nullopt;
        } else if constexpr (std::is_same_v<T, DisableTextureUnit>) {
          units_[p.unit].texture.reset();
        } else if constexpr (std::is_same_v<T, SetSamplerFilter>) {
          auto& s = units_[p.unit].sampler;
          if (p.stage.value == uint8_t(FilterStage::kMin)) s.min_filter = p.filter.value;
          if (p.stage.value == uint8_t(FilterStage::kMag)) s.mag_filter = p.filter.value;
          if (p.stage.value == uint8_t(FilterStage::kMip)) s.mip_filter = p.filter.value;
        } else if constexpr (std::is_same_v<T, SetSamplerAddress>) {
          auto& s = units_[p.unit].sampler;
          s.address_u = p.uvw[0].value;
          s.address_v = p.uvw[1].value;
          s.address_w = p.uvw[2].value;
        } else if constexpr (std::is_same_v<T, SetSamplerAnisotropy>) {
          units_[p.unit].sampler.max_anisotropy = p.max_anisotropy;
        } else if constexpr (std::is_same_v<T, SetTexCoordSet>) {
          units_[p.unit].sampler.coord_set = p.index;
        } else if constexpr (std::is_same_v<T, SetTextureBlend>) {
          if (p.raw_blend_type == 0) units_[p.unit].blend = p;
        } else if constexpr (std::is_same_v<T, BindProgram>) {
          if (p.stage.value == uint8_t(ProgramStage::kVertex)) vertex_program_bound_ = true;
          bound_programs_[p.stage.value] = content_.ProgramName(p.program);
        } else if constexpr (std::is_same_v<T, UnbindProgram>) {
          if (p.stage.value == uint8_t(ProgramStage::kVertex)) vertex_program_bound_ = false;
          bound_programs_.erase(p.stage.value);
        } else if constexpr (std::is_same_v<T, SetMatrix>) {
          if (p.kind.value == uint8_t(MatrixKind::kWorld)) world_ = p.m;
          if (p.kind.value == uint8_t(MatrixKind::kView)) view_ = p.m;
          if (p.kind.value == uint8_t(MatrixKind::kProjection)) projection_ = p.m;
        } else if constexpr (std::is_same_v<T, SetVertexDeclaration>) {
          declaration_ = p;
        } else if constexpr (std::is_same_v<T, SetVertexBuffers>) {
          streams_ = p;
        } else if constexpr (std::is_same_v<T, SetConstants>) {
          auto& st = stages_[p.stage.value];
          for (const auto& r : p.floats)
            for (uint32_t i = 0; i < r.element_count && i < r.data.size(); ++i)
              st.physical[r.physical_index + i] = std::bit_cast<float>(r.data[i]);
          st.autos = p.autos;
          if (p.transpose_matrices) {
            st.transposed = *p.transpose_matrices;
            st.layout = LayoutSource::kFlag;
          } else {
            st.InferLayout();
          }
        } else if constexpr (std::is_same_v<T, commands::Draw>) {
          Draw(p);
        }
      },
      command.payload);
}

std::string Frontend::DescribeCurrentDraw() const {
  std::ostringstream s;
  for (const auto& [stage, name] : bound_programs_)
    s << (stage == uint8_t(ProgramStage::kVertex) ? " vs=" : " fs=") << name;
  for (const auto& [unit, u] : units_) {
    if (!u.texture) continue;
    const TextureDesc* t = content_.Texture(*u.texture);
    s << " t" << unit << "=" << (t ? t->name : std::string("?"));
  }
  return s.str();
}

void Frontend::Draw(const commands::Draw& p) {
  ++stats_.draws_total;
  size_t index = stats_.draws_total - 1;
  if (filter_ && !filter_(index)) {
    stats_.skipped.Add("excluded by --draws");
    return;
  }
  // Matrix: the uploaded world-view-projection with a vertex program, the setter
  // matrices without one (see xbox_to_gl_conventions).
  const StageConstants& vs = stages_[uint8_t(ProgramStage::kVertex)];
  std::optional<Mat> wvp;
  if (vertex_program_bound_) wvp = vs.Auto("ACT_WORLDVIEWPROJ_MATRIX");
  if (!vertex_program_bound_ && !(world_ && view_ && projection_)) {
    stats_.skipped.Add("fixed-function draw without world/view/projection setters");
    return;
  }
  // Skinned programs clip with the view-projection (set below from the program).
  bool wvp_from_skinning = false;
  if (vertex_program_bound_ && !wvp) {
    wvp = vs.Auto("ACT_VIEWPROJ_MATRIX");
    wvp_from_skinning = wvp.has_value();
  }
  if (vertex_program_bound_ && !wvp) {
    stats_.skipped.Add("no world-view-projection upload for the vertex stage");
    return;
  }
  if (vertex_program_bound_ && vs.layout == LayoutSource::kInferred)
    stats_.degraded.Add("matrix layout inferred from an affine constant (capture < 1.2)");
  if (vertex_program_bound_ && vs.layout == LayoutSource::kUnknown)
    stats_.degraded.Add("matrix layout unknown (capture < 1.2), assumed not transposed");
  if (!declaration_ || !declaration_->declaration) {
    stats_.skipped.Add("no vertex declaration");
    return;
  }
  const VertexDeclarationContent* decl =
      content_.Declaration(*declaration_->declaration, declaration_->content);
  if (!decl) {
    stats_.skipped.Add("vertex declaration content missing");
    return;
  }
  std::vector<tl_vertex_element> elements;
  for (const auto& e : decl->elements) {
    elements.push_back({e.source, e.offset, e.index, e.type.value, e.semantic.value});
  }
  // Streams: snapshot i belongs to binding entry i (same in-order map walk).
  uint32_t stream_count = 0;
  std::vector<uint64_t> stream_ids;
  std::vector<uint64_t> owners;
  std::vector<uint32_t> strides;
  if (!streams_ || streams_->streams.size() != p.vertex_buffers.size()) {
    stats_.skipped.Add("vertex buffer binding does not match the draw snapshots");
    return;
  }
  for (size_t i = 0; i < p.vertex_buffers.size(); ++i) {
    uint32_t s = streams_->streams[i].stream;
    stream_count = std::max(stream_count, s + 1);
  }
  stream_ids.assign(stream_count, 0);
  owners.assign(stream_count, 0);
  strides.assign(stream_count, 0);
  for (size_t i = 0; i < p.vertex_buffers.size(); ++i) {
    const auto& snap = p.vertex_buffers[i];
    if (!EnsureBuffer(snap, true, 0)) {
      stats_.skipped.Add("vertex buffer content not captured");
      return;
    }
    const VertexBufferDesc* d = content_.VertexBuffer(snap.buffer);
    if (!d) {
      stats_.skipped.Add("vertex buffer description missing");
      return;
    }
    stream_ids[streams_->streams[i].stream] = snap.blob;
    owners[streams_->streams[i].stream] = PackId(snap.buffer);
    strides[streams_->streams[i].stream] = d->vertex_size;
  }
  tl_draw draw{};
  draw.primitive = p.primitive.value;
  draw.elements = elements.data();
  draw.element_count = uint32_t(elements.size());
  draw.stream_buffers = stream_ids.data();
  draw.stream_strides = strides.data();
  draw.stream_owners = owners.data();
  draw.stream_count = stream_count;
  draw.vertex_start = p.vertex_start;
  draw.vertex_count = p.vertex_count;
  if (p.indexed) {
    if (!p.index_buffer || !EnsureBuffer(*p.index_buffer, false, p.index_size)) {
      stats_.skipped.Add("index buffer content not captured");
      return;
    }
    draw.index_buffer = p.index_buffer->blob;
    draw.index_owner = PackId(p.index_buffer->buffer);
    draw.index_start = p.index_start;
    draw.index_count = p.index_count;
  }
  draw.fixed_function = !vertex_program_bound_;
  if (wvp) std::copy(wvp->begin(), wvp->end(), draw.guest_wvp);
  if (draw.fixed_function) {
    std::copy(world_->begin(), world_->end(), draw.world);
    std::copy(view_->begin(), view_->end(), draw.view);
    std::copy(projection_->begin(), projection_->end(), draw.projection);
  }
  // Colour, lighting and fog, read from the bound programs (see rtss_program.h).
  draw.colour_source = TL_COLOUR_VERTEX;
  if (vertex_program_bound_) {
    if (projection_) {
      draw.has_guest_projection = 1;
      std::copy(projection_->begin(), projection_->end(), draw.guest_projection);
    }
    // Analysed programs, cached by program names and auto constant placements.
    auto& vs_constants = stages_[uint8_t(ProgramStage::kVertex)];
    auto& fs_constants = stages_[uint8_t(ProgramStage::kFragment)];
    std::string key;
    for (auto [stage, constants] : {std::pair{ProgramStage::kVertex, &vs_constants},
                                    std::pair{ProgramStage::kFragment, &fs_constants}}) {
      auto name = bound_programs_.find(uint8_t(stage));
      key += name == bound_programs_.end() ? std::string("-") : name->second;
      for (const auto& a : constants->autos)
        key += ':' + std::to_string(a.physical_index) + ',' + std::to_string(a.element_count);
      key += '|';
    }
    auto found = analysed_.find(key);
    if (found == analysed_.end()) {
      AnalysedPrograms entry;
      auto analyse = [&](ProgramStage stage, const StageConstants& constants, AnalysedStage& out) {
        auto name = bound_programs_.find(uint8_t(stage));
        if (name == bound_programs_.end()) return;
        out.bound = true;
        const rtss::Program* parsed = content_.Program(name->second);
        if (!parsed) {
          out.problem = "program source missing or not parsed (colour from the vertex)";
          return;
        }
        out.program = *parsed;
        std::vector<rtss::AutoPlacement> autos;
        for (const auto& a : constants.autos) autos.push_back({a.physical_index, a.element_count});
        std::string reason;
        if (!rtss::ResolveRegisters(out.program, autos, &reason)) {
          out.problem = "program constant registers not resolved: " + reason;
          return;
        }
        out.ok = true;
      };
      analyse(ProgramStage::kVertex, vs_constants, entry.vs);
      analyse(ProgramStage::kFragment, fs_constants, entry.fs);
      if (entry.vs.ok && entry.fs.ok) {
        entry.vf = rtss::AnalyseVertex(entry.vs.program);
        entry.ff = rtss::AnalyseFragment(entry.fs.program);
      }
      found = analysed_.emplace(key, std::move(entry)).first;
    }
    const AnalysedPrograms& analysed = found->second;
    for (const AnalysedStage* st : {&analysed.vs, &analysed.fs})
      if (st->bound && !st->ok) stats_.degraded.Add(st->problem);
    const rtss::Program* vs_program = analysed.vs.ok ? &analysed.vs.program : nullptr;
    const rtss::Program* fs_program = analysed.fs.ok ? &analysed.fs.program : nullptr;
    auto value = [&](const rtss::Program& program, const StageConstants& constants,
                     const std::string& name) -> std::optional<std::array<float, 4>> {
      const rtss::Global* g = rtss::FindGlobal(program, name);
      if (!g) return std::nullopt;
      return constants.Register(g->first_register);
    };
    if (wvp_from_skinning && !vs_program) {
      stats_.degraded.Add("view-projection used without the program's source (unskinned)");
    }
    if (vs_program && fs_program) {
      const rtss::VertexFeatures& vf = analysed.vf;
      const rtss::FragmentFeatures& ff = analysed.ff;
      for (const auto& u : vf.unsupported) stats_.degraded.Add("vertex program call not translated: " + u);
      for (const auto& u : ff.unsupported) stats_.degraded.Add("fragment program call not translated: " + u);
      if (!ff.uses_colour || vf.colour == rtss::VertexColour::kNone) {
        draw.colour_source = TL_COLOUR_WHITE;
      } else if (vf.colour == rtss::VertexColour::kSceneColour) {
        auto scene = value(*vs_program, vs, vf.scene_colour);
        if (!scene) {
          stats_.degraded.Add("scene colour not uploaded (colour from the vertex)");
        } else {
          draw.colour_source = TL_COLOUR_LIGHTING;
          std::copy(scene->begin(), scene->end(), draw.lighting.scene_colour);
          if (vf.lights.size() > 1) stats_.degraded.Add("more than one light (first one used)");
          if (!vf.lights.empty()) {
            auto direction = value(*vs_program, vs, vf.lights[0].direction);
            auto diffuse = value(*vs_program, vs, vf.lights[0].diffuse);
            if (direction && diffuse) {
              draw.lighting.light_count = 1;
              std::copy_n(direction->begin(), 3, draw.lighting.towards_light_view);
              std::copy_n(diffuse->begin(), 3, draw.lighting.light_diffuse);
              draw.lighting.diffuse_from_vertex = vf.lights[0].diffuse_from_vertex;
            } else {
              stats_.degraded.Add("light constants not uploaded (light dropped)");
            }
          }
        }
      }
      if (ff.fog_linear && vf.fog_depth) {
        auto params = value(*fs_program, fs_constants, ff.fog_params);
        auto colour = value(*fs_program, fs_constants, ff.fog_colour);
        if (params && colour) {
          draw.fog.enabled = 1;
          draw.fog.end = (*params)[2];
          draw.fog.inverse_range = (*params)[3];
          std::copy(colour->begin(), colour->end(), draw.fog.colour);
        } else {
          stats_.degraded.Add("fog constants not uploaded (fog dropped)");
        }
      } else if (ff.fog_linear) {
        stats_.degraded.Add("fragment fog without the vertex fog distance (fog dropped)");
      }
      // Texture stages: sampler N is unit N; its coordinate comes from the vertex
      // output with the same semantic.
      auto matrix_of = [&](const std::string& name) -> std::optional<Mat> {
        const rtss::Global* g = rtss::FindGlobal(*vs_program, name);
        if (!g) return std::nullopt;
        return vs.Matrix(g->first_register, g->registers);
      };
      // Runic skinning on the CPU: world-space positions clipped with the program's
      // position matrix; guest matrices applied to the object-space position get
      // the inverse world folded in (see rtss::Skinning).
      std::optional<Mat> skin_inverse_world;
      if (vf.skinning) {
        const rtss::Global* bones_global = rtss::FindGlobal(*vs_program, vf.skinning->bones);
        auto clip = matrix_of(vf.skinning->position_matrix);
        if (bones_global && clip) {
          bones_.assign(size_t(bones_global->array) * 12, 0.0f);
          uint32_t rows = bones_global->registers / std::max(1u, bones_global->array);
          for (uint32_t bone = 0; bone < bones_global->array; ++bone)
            for (uint32_t r = 0; r < 3 && r < rows; ++r)
              if (auto v = vs.Register(bones_global->first_register + bone * rows + r))
                std::copy(v->begin(), v->end(), &bones_[bone * 12 + r * 4]);
          draw.skinning.influence_count = uint32_t(std::min<size_t>(4, vf.skinning->influences.size()));
          for (uint32_t k = 0; k < draw.skinning.influence_count; ++k) {
            draw.skinning.index_lane[k] = uint8_t(vf.skinning->influences[k].index_lane);
            draw.skinning.weight_lane[k] = uint8_t(vf.skinning->influences[k].weight_lane);
          }
          draw.skinning.bones = bones_.data();
          draw.skinning.bone_count = bones_global->array;
          std::copy(clip->begin(), clip->end(), draw.guest_wvp);
          if (!vf.skinning->inverse_world.empty())
            skin_inverse_world = matrix_of(vf.skinning->inverse_world);
        } else {
          stats_.degraded.Add("skinning constants missing (drawn unskinned)");
        }
      } else if (wvp_from_skinning) {
        stats_.degraded.Add("no world-view-projection upload (view-projection used)");
      }
      draw.program_stages = 1;
      for (const rtss::TextureStage& st : ff.stages) {
        if (st.sampler >= TL_MAX_STAGES) {
          stats_.degraded.Add("texture stage beyond the backend's stages (dropped)");
          continue;
        }
        tl_stage& out = draw.stages[st.sampler];
        out.sampled = 1;
        auto combine = [](const rtss::Combine& c, tl_combine& o) {
          static const uint8_t kOps[] = {TL_OP_SOURCE1, TL_OP_SOURCE2, TL_OP_MODULATE,
                                         TL_OP_MODULATE_X2, TL_OP_MODULATE_X4,
                                         TL_OP_ADD, TL_OP_ADD_SIGNED,
                                         TL_OP_ADD_SMOOTH, TL_OP_SUBTRACT};
          static const uint8_t kSources[] = {TL_SRC_TEXTURE, TL_SRC_CURRENT,
                                             TL_SRC_DIFFUSE, TL_SRC_CONSTANT};
          o.op = kOps[size_t(c.operation)];
          o.source1 = kSources[size_t(c.source1)];
          o.source2 = kSources[size_t(c.source2)];
          std::copy(c.constant1.begin(), c.constant1.end(), o.constant1);
          std::copy(c.constant2.begin(), c.constant2.end(), o.constant2);
        };
        combine(st.colour, out.colour);
        combine(st.alpha, out.alpha);
        auto tc = std::find_if(vf.texcoords.begin(), vf.texcoords.end(),
                               [&](const rtss::TexCoordOutput& t) {
                                 return t.semantic == st.coord_semantic;
                               });
        if (tc == vf.texcoords.end()) {
          stats_.degraded.Add("texture stage coordinate not written by the vertex program");
          continue;
        }
        if (!tc->matrix.empty()) {
          if (auto m = matrix_of(tc->matrix)) {
            out.has_matrix = 1;
            std::copy(m->begin(), m->end(), out.matrix);
          }
        }
        switch (tc->source) {
          case rtss::TexCoordSource::kInput:
          case rtss::TexCoordSource::kTransformed:
            out.texgen = TL_TEXGEN_NONE;
            out.coord_set = tc->input_set;
            break;
          case rtss::TexCoordSource::kProjective: {
            auto world_m = matrix_of(tc->world);
            auto projector = matrix_of(tc->projector);
            if (!world_m || !projector || !st.projective) {
              stats_.degraded.Add("projective coordinate without its matrices (unit dropped)");
              out.sampled = 0;
              break;
            }
            out.texgen = TL_TEXGEN_PROJECTIVE;
            if (draw.skinning.influence_count && skin_inverse_world)
              world_m = Multiply(*world_m, *skin_inverse_world);
            std::copy(world_m->begin(), world_m->end(), out.world);
            std::copy(projector->begin(), projector->end(), out.projector);
            break;
          }
          case rtss::TexCoordSource::kEnvMapNormal:
            out.texgen = TL_TEXGEN_VIEW_NORMAL;
            break;
          case rtss::TexCoordSource::kEnvMapSphere:
            out.texgen = TL_TEXGEN_SPHERE;
            break;
          case rtss::TexCoordSource::kEnvMapReflect: {
            auto world_m = matrix_of(tc->world);
            if (!world_m) {
              stats_.degraded.Add("reflection coordinate without its world matrix (unit dropped)");
              out.sampled = 0;
              break;
            }
            out.texgen = TL_TEXGEN_REFLECT;
            if (draw.skinning.influence_count && skin_inverse_world)
              world_m = Multiply(*world_m, *skin_inverse_world);
            std::copy(world_m->begin(), world_m->end(), out.world);
            break;
          }
        }
      }
    }
  }
  // State and textures.
  state_.invert_winding = p.invert_winding;
  tl_backend_set_state(backend_, &state_);
  if (blend_op_ != uint8_t(BlendOp::kAdd)) stats_.degraded.Add("blend operation is not add");
  // With program stages every unit the program samples is bound; otherwise only
  // unit 0 (fixed-function draws: further units are not combined yet).
  bool unrecorded = false, approximate = false, extra_units = false, not_ready = false;
  for (auto& [unit, u] : units_) {
    uint64_t key = 0;
    bool bind = unit == 0 || (draw.program_stages && unit < TL_MAX_STAGES &&
                              draw.stages[unit].sampled);
    if (bind && u.texture) {
      if (TextureReady(*u.texture)) {
        key = PackId(*u.texture);
      } else {
        key = kFallbackTexture;
        not_ready = true;
      }
    }
    if (!bind && u.texture && !draw.program_stages) extra_units = true;
    tl_backend_set_texture(backend_, unit, key, &u.sampler);
    if (unit == 0 && u.texture) {
      if (!u.blend) unrecorded = true;
      else if (!u.blend->is_modulate) approximate = true;
    }
  }
  if (extra_units) stats_.degraded.Add("texture units >= 1 ignored (fixed-function draw)");
  if (not_ready) ++stats_.textures_not_ready;
  if (approximate) ++stats_.approx_combination;
  else if (unrecorded) ++stats_.unrecorded_combination;
  auto draw_start = std::chrono::steady_clock::now();
  int r = tl_backend_draw(backend_, &draw);
  stats_.draw_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                              draw_start)
                        .count();
  if (r == TL_DRAWN && observer_) observer_(index);
  if (r == TL_DRAWN) {
    ++stats_.draws_drawn;
    uint32_t notes = tl_backend_draw_notes(backend_);
    if (notes & TL_NOTE_NO_VIEW_SPACE)
      stats_.degraded.Add("no view space (guest projection does not split the WVP): lights and fog dropped");
    if (notes & TL_NOTE_STAGE_APPROXIMATED)
      stats_.degraded.Add("texture stage combination approximated (host fixed function)");
    if (notes & TL_NOTE_SKINNING_LAYOUT)
      stats_.degraded.Add("skinning: vertex layout not supported (drawn unskinned)");
    if (notes & TL_NOTE_STAGE_DROPPED)
      stats_.degraded.Add("texture stage beyond the host's fixed-function units (dropped)");
  } else {
    static const char* kReasons[] = {"", "vertex type not supported",
                                     "buffer missing in the backend",
                                     "primitive type not supported",
                                     "index/vertex range outside the buffer",
                                     "OGRE exception",
                                     "no program generated (RTSS)",
                                     "skinning palette too large for the vertex program"};
    stats_.skipped.Add(std::string("backend: ") + (r < 8 ? kReasons[r] : "unknown"));
  }
}

}  // namespace torchlight::frontend
