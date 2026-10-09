// Frontend: turns the neutral command stream into host backend calls.
//
// The one path to the backend. The replay feeds it frames from a capture file; the live mode feeds
// it frames cut at the guest's swap. It keeps the guest render state, the guest constant register
// files and the bound programs, reads what each RTSS program does (rtss_program.h) and builds the
// backend draw. What it cannot reproduce exactly is counted (stats), never special-cased.

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <unordered_set>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/backend_api.h"
#include "commands/types.h"
#include "frontend/content_source.h"

namespace torchlight::frontend {

struct Counter {
  std::map<std::string, size_t> counts;
  void Add(const std::string& reason) { counts[reason]++; }
  size_t total() const;
};

struct Stats {
  size_t draws_total = 0, draws_drawn = 0;
  Counter skipped, degraded, texture_problems;
  // Draws that bound a texture which was not ready (drawn with the fallback texture).
  size_t textures_not_ready = 0;
  size_t approx_combination = 0, unrecorded_combination = 0;
  size_t named_loaded = 0, dynamic_loaded = 0, render_targets = 0;
  double draw_ms = 0;  // time inside tl_backend_draw, accumulated
  std::string gamma_ramp = "not recorded (linear)";
};

// Backend texture id for a resource identity.
uint64_t PackId(const commands::ResourceId& id);

class Frontend {
 public:
  Frontend(tl_backend* backend, const ContentSource& content);

  // Creates the host resource of a texture (named file, dynamic content or render target). Done
  // outside the draws that use it: a texture that is not ready when drawn is replaced by a flat
  // fallback colour and counted.
  // A texture whose content key changed (a new version of dynamic content) is created again.
  void PrepareTexture(const commands::TextureDesc& desc);
  bool TextureReady(const commands::ResourceId& id) const;
  // Live mode: frees the host copy of superseded content and of destroyed textures.
  void ReleaseContent(commands::Hash key);
  // The guest destroyed this vertex or index buffer: frees the host buffers kept for it.
  void ReleaseBufferOwner(const commands::ResourceId& id);
  void ReleaseTexture(const commands::ResourceId& id);

  void BeginFrame();
  void Execute(const commands::Command& command);
  void EndFrame();

  // Diagnostics: draws for which the filter returns false are not issued (counted as excluded);
  // the observer is called after each issued draw with its index.
  void set_draw_filter(std::function<bool(size_t index)> filter) { filter_ = std::move(filter); }
  void set_draw_observer(std::function<void(size_t index)> observer) {
    observer_ = std::move(observer);
  }
  // Programs and textures of the current draw, for diagnostics.
  std::string DescribeCurrentDraw() const;

  const Stats& stats() const { return stats_; }
  // The guest's frame size, from its window target (0 x 0 until the first one is bound).
  uint32_t guest_width() const { return guest_width_; }
  uint32_t guest_height() const { return guest_height_; }

 private:
  struct Unit {
    std::optional<commands::ResourceId> texture;
    tl_sampler sampler{};
    std::optional<commands::SetTextureBlend> blend;
  };
  enum class LayoutSource { kFlag, kInferred, kUnknown };
  using Mat = std::array<float, 16>;
  struct StageConstants {
    std::unordered_map<uint32_t, float> physical;  // guest float constants by physical index
    std::vector<commands::AutoConstant> autos;     // from the latest upload
    bool transposed = false;  // GpuProgramParameters::mTransposeMatrices (format 1.2+)
    LayoutSource layout = LayoutSource::kUnknown;
    std::optional<Mat> Raw(const commands::AutoConstant& a, uint32_t count) const;
    std::optional<Mat> Auto(const std::string& name) const;
    std::optional<std::array<float, 4>> Register(uint32_t reg) const;
    Mat Matrix(uint32_t first, uint32_t registers) const;
    void InferLayout();
  };

  void Draw(const commands::Draw& d);
  bool EnsureBuffer(const commands::BufferSnapshot& s, bool vertex, uint32_t index_size);

  tl_backend* backend_;
  const ContentSource& content_;
  Stats stats_;
  std::function<bool(size_t)> filter_;
  std::function<void(size_t)> observer_;
  std::unordered_set<uint64_t> ready_textures_, uploaded_buffers_;
  std::unordered_map<uint64_t, commands::Hash> texture_content_;  // PackId -> content key

  tl_state state_{};
  uint8_t blend_op_ = 0;
  std::map<uint32_t, Unit> units_;
  std::map<uint8_t, StageConstants> stages_;
  std::optional<commands::SetVertexDeclaration> declaration_;
  bool vertex_program_bound_ = false;
  std::map<uint8_t, std::string> bound_programs_;  // stage -> program name
  std::optional<Mat> world_, view_, projection_;
  std::optional<commands::SetVertexBuffers> streams_;
  std::optional<commands::SetViewport> viewport_;
  uint32_t guest_width_ = 0, guest_height_ = 0;
  uint32_t scene_clip_viewport_ = 0;  // SetSceneClip
  void ApplySceneClip();
  std::vector<float> bones_;  // bone matrices of the current skinned draw (3x4 each)
  // Programs of a vertex + fragment pair, parsed, with their registers resolved for the placement
  // of their auto constants and analysed; by program names and placements (Draw reads only the
  // constant values per draw).
  struct AnalysedStage {
    bool bound = false, ok = false;
    std::string problem;  // degraded reason when bound but not usable
    rtss::Program program;
  };
  struct AnalysedPrograms {
    AnalysedStage vs, fs;
    rtss::VertexFeatures vf;
    rtss::FragmentFeatures ff;
  };
  std::unordered_map<std::string, AnalysedPrograms> analysed_;
};

}  // namespace torchlight::frontend
