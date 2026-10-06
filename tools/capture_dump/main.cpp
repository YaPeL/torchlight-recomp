// capture_dump: prints a .tlcap capture in readable form.
//
// Usage: capture_dump CAPTURE.tlcap [--draws] [--program_sources DIR]
//   Summary, statistics (unresolved, textures, programs, instance counts, slot histogram) and a
//   check of world/view/projection constants against the captured matrices. --draws adds the
//   state of every draw. --program_sources writes each recorded program source (1.3+) to
//   DIR/<name>.<language>.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "commands/serialize.h"

namespace {

using namespace torchlight::commands;
using Mat = std::array<float, 16>;

template <typename E>
std::string EnumText(const Enum<E>& e, const char* const* names, size_t count) {
  if (e.known() && e.value < count) return names[e.value];
  return "unknown(raw=" + std::to_string(e.raw) + ")";
}
const char* const kPrimitive[] = {"points", "lines", "line_strip", "triangles", "tri_strip",
                                  "tri_fan"};
const char* const kStage[] = {"vertex", "fragment", "geometry"};
const char* const kBlend[] = {"one", "zero", "dst_col", "src_col", "1-dst_col", "1-src_col",
                              "dst_a", "src_a", "1-dst_a", "1-src_a"};
const char* const kCompare[] = {"never", "always", "less", "lequal", "equal", "nequal", "gequal",
                                "greater"};
const char* const kCull[] = {"none", "cw", "ccw"};

Mat Multiply(const Mat& a, const Mat& b) {  // row-major a * b
  Mat r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k) r[i * 4 + j] += a[i * 4 + k] * b[k * 4 + j];
  return r;
}
Mat Transpose(const Mat& a) {
  Mat r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) r[i * 4 + j] = a[j * 4 + i];
  return r;
}

struct Comparison {
  bool compared = false;
  float straight = 0, transposed = 0;  // max relative difference
  float view_z_flip = 0;  // against the variant computed with P * S (see FlipViewZ)
};

// The projection the guest passes to _setProjectionMatrix yields w = +view z. The RTSS uploads
// matrices built with P * S, S = diag(1, 1, -1, 1) (view z negated), so w is positive in front of
// the camera. Observed in the menu capture: VIEWPROJ rows 0, 1 and 3 equal P * V exactly, row 2
// equals (P * S * V) row 2.
Mat FlipViewZ(const Mat& projection) {
  Mat r = projection;
  for (int i = 0; i < 4; ++i) r[i * 4 + 2] = -r[i * 4 + 2];
  return r;
}

// Compares `count` floats of the uploaded constant with an expected matrix.
// `flipped` is the same matrix computed with FlipViewZ(P) (equal to `expected` when no
// projection is involved).
Comparison Compare(const std::vector<float>& uploaded, const Mat& expected, const Mat& flipped) {
  Comparison c;
  if (uploaded.empty()) return c;
  c.compared = true;
  Mat t = Transpose(expected);
  auto diff = [](float a, float b) { return std::fabs(a - b) / std::max(1.0f, std::fabs(b)); };
  for (size_t i = 0; i < uploaded.size() && i < 16; ++i) {
    c.straight = std::max(c.straight, diff(uploaded[i], expected[i]));
    c.transposed = std::max(c.transposed, diff(uploaded[i], t[i]));
    c.view_z_flip = std::max(c.view_z_flip, diff(uploaded[i], flipped[i]));
  }
  return c;
}

// Floats of an auto constant found through its physical index in the uploaded ranges.
std::vector<float> AutoData(const SetConstants& sc, const AutoConstant& a) {
  std::vector<float> out;
  for (const auto& r : sc.floats) {
    if (a.physical_index >= r.physical_index &&
        a.physical_index < r.physical_index + r.element_count) {
      uint32_t offset = a.physical_index - r.physical_index;
      for (uint32_t i = offset; i < r.data.size() && out.size() < a.element_count; ++i) {
        out.push_back(std::bit_cast<float>(r.data[i]));
      }
    }
  }
  return out;
}

struct DrawState {
  std::optional<Mat> world, view, projection;
  std::map<uint32_t, std::optional<ResourceId>> textures;
  std::map<uint8_t, ResourceId> programs;
  std::map<uint8_t, SetConstants> constants;
  std::optional<SetBlend> blend;
  std::optional<SetDepthFunc> depth_func;
  std::optional<SetCull> cull;
  std::optional<SetVertexDeclaration> declaration;
  std::optional<SetRenderTarget> target;
  std::optional<SetViewport> viewport;
  std::optional<SetScissor> scissor;
  bool stencil_check = false;
  std::optional<SetStencil> stencil;
};

const char* kTolerance = "1e-3";
constexpr float kToleranceValue = 1e-3f;

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s CAPTURE.tlcap [--draws] [--program_sources DIR]\n",
                 argv[0]);
    return 2;
  }
  bool draws_detail = false;
  std::string sources_dir;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--draws") {
      draws_detail = true;
    } else if (a == "--program_sources" && i + 1 < argc) {
      sources_dir = argv[++i];
    } else {
      std::fprintf(stderr, "error: unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  Capture c;
  std::string error;
  if (!ReadCaptureFile(argv[1], c, error)) {
    std::fprintf(stderr, "error: %s\n", error.c_str());
    return 1;
  }

  std::map<Hash, const Blob*> blobs;
  size_t blob_bytes = 0;
  for (const auto& b : c.blobs) {
    blobs[b.hash] = &b;
    blob_bytes += b.bytes.size();
  }
  std::map<uint32_t, const TextureDesc*> textures;
  for (const auto& t : c.textures) textures[t.id.guest_address] = &t;
  std::map<uint32_t, const ProgramDesc*> programs;
  for (const auto& p : c.programs) programs[p.id.guest_address] = &p;

  std::printf("== %s\n", argv[1]);
  std::printf("format %u.%u, created %s, first swap %llu, %u frame(s)\n", c.meta.version_major,
              c.meta.version_minor, c.meta.created_utc.c_str(),
              (unsigned long long)c.meta.first_swap, c.meta.frame_count);
  std::printf("resources: %zu vertex buffers, %zu index buffers, %zu declarations, %zu textures, "
              "%zu programs; %zu blobs (%.1f MiB)\n",
              c.vertex_buffers.size(), c.index_buffers.size(), c.vertex_declarations.size(),
              c.textures.size(), c.programs.size(), c.blobs.size(), blob_bytes / 1048576.0);
  if (c.reference) {
    std::printf("reference image: %ux%u, waited %u ms%s\n", c.reference->width,
                c.reference->height, c.reference->wait_ms,
                c.reference->possibly_misaligned ? ", POSSIBLY MISALIGNED" : "");
  } else {
    std::printf("reference image: none\n");
  }

  // ---- walk the command stream ----
  size_t total_draws = 0, draws_unresolved = 0;
  std::map<uint32_t, size_t> unresolved_by_reason;  // reason -> draws
  std::map<uint32_t, size_t> draws_per_program;
  struct WvpRow {
    uint32_t frame, draw;
    std::string what;
    Comparison cmp;
  };
  std::vector<WvpRow> wvp;
  size_t draws_without_vertex_constants = 0;

  for (const auto& frame : c.frames) {
    DrawState st;
    size_t baseline = 0, draws = 0;
    std::map<uint16_t, size_t> per_opcode;
    for (size_t i = 0; i < frame.commands.size(); ++i) {
      const Command& cmd = frame.commands[i];
      per_opcode[OpcodeOf(cmd.payload)]++;
      if (cmd.flags & kFromBaseline) ++baseline;
      std::visit(
          [&](const auto& p) {
            using T = std::decay_t<decltype(p)>;
            if constexpr (std::is_same_v<T, SetMatrix>) {
              if (p.kind.known() && p.kind.get() == MatrixKind::kWorld) st.world = p.m;
              if (p.kind.known() && p.kind.get() == MatrixKind::kView) st.view = p.m;
              if (p.kind.known() && p.kind.get() == MatrixKind::kProjection) st.projection = p.m;
            } else if constexpr (std::is_same_v<T, SetTexture>) {
              st.textures[p.unit + (p.vertex_texture ? 0x100 : 0)] = p.texture;
            } else if constexpr (std::is_same_v<T, BindProgram>) {
              st.programs[p.stage.value] = p.program;
            } else if constexpr (std::is_same_v<T, UnbindProgram>) {
              st.programs.erase(p.stage.value);
            } else if constexpr (std::is_same_v<T, SetConstants>) {
              // Later uploads with a narrower mask update only part of the constants; keep the
              // latest upload of each auto constant by merging ranges.
              auto& sc = st.constants[p.stage.value];
              if (p.mask == 0xFFFF || sc.floats.empty()) {
                sc = p;
              } else {
                for (const auto& r : p.floats) sc.floats.push_back(r);
              }
            } else if constexpr (std::is_same_v<T, SetBlend>) {
              st.blend = p;
            } else if constexpr (std::is_same_v<T, SetDepthFunc>) {
              st.depth_func = p;
            } else if constexpr (std::is_same_v<T, SetCull>) {
              st.cull = p;
            } else if constexpr (std::is_same_v<T, SetVertexDeclaration>) {
              st.declaration = p;
            } else if constexpr (std::is_same_v<T, SetRenderTarget>) {
              st.target = p;
            } else if constexpr (std::is_same_v<T, SetViewport>) {
              st.viewport = p;
            } else if constexpr (std::is_same_v<T, SetScissor>) {
              st.scissor = p;
            } else if constexpr (std::is_same_v<T, SetStencilCheck>) {
              st.stencil_check = p.enabled;
            } else if constexpr (std::is_same_v<T, SetStencil>) {
              st.stencil = p;
            } else if constexpr (std::is_same_v<T, Draw>) {
              ++draws;
              ++total_draws;
              if (p.unresolved_mask) {
                ++draws_unresolved;
                for (uint32_t r = 0; r < 32; ++r)
                  if (p.unresolved_mask & (1u << r)) unresolved_by_reason[r]++;
              }
              for (const auto& [stage, id] : st.programs) draws_per_program[id.guest_address]++;
              // World/view/projection check.
              auto vs = st.constants.find(uint8_t(ProgramStage::kVertex));
              if (vs == st.constants.end()) {
                ++draws_without_vertex_constants;
              } else if (st.world && st.view && st.projection) {
                Mat ps = FlipViewZ(*st.projection);
                Mat wvp_expected = Multiply(Multiply(*st.projection, *st.view), *st.world);
                Mat wvp_flipped = Multiply(Multiply(ps, *st.view), *st.world);
                Mat vp_expected = Multiply(*st.projection, *st.view);
                Mat vp_flipped = Multiply(ps, *st.view);
                for (const auto& a : vs->second.autos) {
                  std::optional<Mat> expected, flipped;
                  if (a.name == "ACT_WORLDVIEWPROJ_MATRIX") expected = wvp_expected, flipped = wvp_flipped;
                  else if (a.name == "ACT_WORLD_MATRIX") expected = flipped = *st.world;
                  else if (a.name == "ACT_VIEW_MATRIX") expected = flipped = *st.view;
                  else if (a.name == "ACT_PROJECTION_MATRIX") expected = *st.projection, flipped = ps;
                  else if (a.name == "ACT_VIEWPROJ_MATRIX") expected = vp_expected, flipped = vp_flipped;
                  if (!expected) continue;
                  wvp.push_back({frame.index, uint32_t(draws - 1), std::string(a.name),
                                 Compare(AutoData(vs->second, a), *expected, *flipped)});
                }
              }
              if (draws_detail) {
                std::printf("  f%u d%zu: %s %s v[%u+%u]", frame.index, draws - 1,
                            EnumText(p.primitive, kPrimitive, 6).c_str(),
                            p.indexed ? "indexed" : "", p.vertex_start, p.vertex_count);
                if (p.indexed) std::printf(" i[%u+%u]x%u", p.index_start, p.index_count, p.index_size);
                std::printf(" inst=%u wind=%d", p.instance_count, p.invert_winding);
                for (const auto& [stage, id] : st.programs) {
                  auto it = programs.find(id.guest_address);
                  std::printf(" %s=%s", stage < 3 ? kStage[stage] : "?",
                              it != programs.end() ? it->second->name.c_str() : "?");
                }
                for (const auto& [unit, id] : st.textures) {
                  if (!id) continue;
                  auto it = textures.find(id->guest_address);
                  std::printf(" t%u=%s", unit,
                              it != textures.end() ? (it->second->name.empty() ? "<unnamed>"
                                                     : it->second->name.c_str())
                                                   : "?");
                }
                if (st.blend)
                  std::printf(" blend=%s/%s", EnumText(st.blend->src, kBlend, 10).c_str(),
                              EnumText(st.blend->dst, kBlend, 10).c_str());
                if (st.depth_func)
                  std::printf(" zfunc=%s->%s", EnumText(st.depth_func->requested, kCompare, 8).c_str(),
                              EnumText(st.depth_func->effective, kCompare, 8).c_str());
                if (st.cull) std::printf(" cull=%s", EnumText(st.cull->mode, kCull, 3).c_str());
                if (st.target) std::printf(" rt='%s'", st.target->name.c_str());
                if (st.viewport)
                  std::printf(" vp=%d,%d,%dx%d@%08x", st.viewport->left, st.viewport->top,
                              st.viewport->width, st.viewport->height, st.viewport->guest_address);
                if (st.scissor && st.scissor->enabled)
                  std::printf(" scissor=%u,%u-%u,%u", st.scissor->left, st.scissor->top,
                              st.scissor->right, st.scissor->bottom);
                if (st.stencil_check && st.stencil)
                  std::printf(" stencil=%s ref=%u mask=%#x ops=%u/%u/%u",
                              EnumText(st.stencil->func, kCompare, 8).c_str(),
                              st.stencil->reference, st.stencil->mask, st.stencil->fail.raw,
                              st.stencil->depth_fail.raw, st.stencil->pass.raw);
                for (const auto& vb : p.vertex_buffers)
                  std::printf(" vb=%#x/g%u(%s)", vb.buffer.guest_address, vb.buffer.generation,
                              vb.blob ? (vb.source == 1 ? "fetch" : "sysmem") : "NONE");
                if (p.index_buffer)
                  std::printf(" ib=%#x(%s)", p.index_buffer->buffer.guest_address,
                              p.index_buffer->blob ? "ok" : "NONE");
                if (p.unresolved_mask) {
                  std::printf(" UNRESOLVED:");
                  for (uint32_t r = 0; r < 32; ++r)
                    if (p.unresolved_mask & (1u << r))
                      std::printf(" %s", ReasonName(UnresolvedReason(r)));
                }
                std::printf("\n");
              }
            }
          },
          cmd.payload);
    }
    std::printf("frame %u (swap %llu): %zu commands (%zu baseline), %zu draws\n", frame.index,
                (unsigned long long)frame.first_swap, frame.commands.size(), baseline, draws);
  }

  // ---- statistics ----
  std::printf("\n== draws: %zu total, %zu with something unresolved\n", total_draws,
              draws_unresolved);
  for (const auto& [reason, n] : unresolved_by_reason)
    std::printf("  %-28s %zu draw(s)\n", ReasonName(UnresolvedReason(reason)), n);
  std::map<std::string, size_t> unresolved_details;
  for (const auto& u : c.stats.unresolved) unresolved_details[ReasonName(u.reason)]++;
  std::printf("unresolved entries: %zu\n", c.stats.unresolved.size());
  for (const auto& [name, n] : unresolved_details) std::printf("  %-28s %zu\n", name.c_str(), n);
  for (size_t i = 0; i < c.stats.unresolved.size() && i < 20; ++i) {
    const auto& u = c.stats.unresolved[i];
    std::printf("    f%u cmd%u %s: %s\n", u.frame, u.command, ReasonName(u.reason),
                u.detail.c_str());
  }

  size_t named = 0, dynamic = 0, dumped = 0, render_targets = 0;
  for (const auto& t : c.textures) {
    if (t.render_target) ++render_targets;
    else if (t.name.empty() || t.manual) ++dynamic;
    else ++named;
    if (t.content) ++dumped;
  }
  std::printf("\n== textures: %zu by name, %zu dynamic/unnamed (%zu with raw content), %zu render "
              "targets\n",
              named, dynamic, dumped, render_targets);
  for (const auto& t : c.textures) {
    if (!t.render_target && !t.name.empty() && !t.manual) continue;
    std::printf("  %#x '%s' %ux%u usage=%#x%s%s\n", t.id.guest_address, t.name.c_str(), t.width,
                t.height, t.usage, t.render_target ? " RT" : "", t.manual ? " manual" : "");
  }

  std::map<std::string, const ProgramSource*> sources;
  for (const auto& p : c.program_sources) sources[p.name] = &p;
  std::printf("\n== programs used: %zu (%zu with source)\n", c.programs.size(),
              c.program_sources.size());
  for (const auto& p : c.programs) {
    auto it = sources.find(p.name);
    std::string src = it == sources.end()
                          ? "no source"
                          : it->second->language + " " + it->second->target + " " +
                                it->second->entry_point + " " +
                                std::to_string(it->second->source.size()) + " bytes";
    std::printf("  %-8s %-24s %4zu draw(s)  %s\n", EnumText(p.stage, kStage, 3).c_str(),
                p.name.c_str(), draws_per_program[p.id.guest_address], src.c_str());
  }
  if (!sources_dir.empty()) {
    std::filesystem::create_directories(sources_dir);
    for (const auto& p : c.program_sources) {
      std::ofstream(sources_dir + "/" + p.name + "." + p.language, std::ios::binary) << p.source;
    }
    std::printf("  wrote %zu source(s) to %s\n", c.program_sources.size(), sources_dir.c_str());
  }

  // Display gamma ramp (1.4+): last one in the first frame.
  const SetGammaRamp* gamma = nullptr;
  if (!c.frames.empty())
    for (const auto& cmd : c.frames[0].commands)
      if (auto* g = std::get_if<SetGammaRamp>(&cmd.payload)) gamma = g;
  if (gamma) {
    std::printf("\n== gamma ramp: %s\n", gamma->pwl ? "PWL (128 x base/delta)" : "table (256)");
    const char* channels = "RGB";
    for (int ch = 0; ch < 3; ++ch) {
      std::printf("  %c:", channels[ch]);
      if (gamma->pwl) {
        for (int e : {0, 8, 16, 32, 64, 96, 127})
          std::printf(" [%d] %u+%u", e, gamma->values[ch * 256 + e * 2] >> 6,
                      gamma->values[ch * 256 + e * 2 + 1] >> 6);
      } else {
        for (int e : {0, 16, 32, 64, 128, 192, 255})
          std::printf(" [%d] %u", e, gamma->values[ch * 256 + e] >> 6);
      }
      std::printf("  (10-bit)\n");
    }
  } else {
    std::printf("\n== gamma ramp: not recorded (capture < 1.4 or never written)\n");
  }

  std::printf("\n== instance counts > 1: %zu\n", c.stats.instance_counts_above_one.size());
  for (const auto& e : c.stats.instance_counts_above_one)
    std::printf("  f%u cmd%u: %u\n", e.frame, e.command, e.instance_count);

  std::printf("\n== world/view/projection constant check (relative tolerance %s)\n", kTolerance);
  // straight, transposed, with P*S, none, no data
  std::map<std::string, std::array<size_t, 5>> wvp_summary;
  for (const auto& r : wvp) {
    auto& s = wvp_summary[r.what];
    if (!r.cmp.compared) s[4]++;
    else if (r.cmp.straight <= kToleranceValue) s[0]++;
    else if (r.cmp.transposed <= kToleranceValue) s[1]++;
    else if (r.cmp.view_z_flip <= kToleranceValue) s[2]++;
    else s[3]++;
  }
  std::printf("  draws without a vertex constant upload in the capture: %zu\n",
              draws_without_vertex_constants);
  for (const auto& [what, s] : wvp_summary)
    std::printf("  %-26s match=%zu transposed=%zu with_P*S=%zu mismatch=%zu no_data=%zu\n",
                what.c_str(), s[0], s[1], s[2], s[3], s[4]);
  size_t shown = 0;
  for (const auto& r : wvp) {
    if (!r.cmp.compared || r.cmp.straight <= kToleranceValue ||
        r.cmp.transposed <= kToleranceValue || r.cmp.view_z_flip <= kToleranceValue)
      continue;
    if (shown++ >= 20) break;
    std::printf("    mismatch f%u d%u %s: max rel diff %.4g (transposed %.4g, with P*S %.4g)\n",
                r.frame, r.draw, r.what.c_str(), r.cmp.straight, r.cmp.transposed, r.cmp.view_z_flip);
  }

  std::printf("\n== slot histogram (captured / since start)\n");
  for (const auto& s : c.stats.slots) {
    if (!s.attributable) {
      std::printf("  %3u %-40s  n/a   (%s)\n", s.slot, s.name.c_str(), s.note.c_str());
    } else if (s.total != 0) {
      std::printf("  %3u %-40s %8llu / %llu%s%s\n", s.slot, s.name.c_str(),
                  (unsigned long long)s.captured, (unsigned long long)s.total,
                  s.note.empty() ? "" : "  ", s.note.c_str());
    }
  }
  size_t never = 0;
  for (const auto& s : c.stats.slots) never += s.attributable && s.total == 0;
  std::printf("  (%zu attributable slots never called)\n", never);
  return 0;
}
