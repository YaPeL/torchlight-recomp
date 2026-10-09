// The frontend's ContentSource in the live mode: resources as the live frames announce them.
//
// Backend thread only. Each frame's resources are applied before its commands run; content that a
// newer version superseded during the frame stays available until the frame has been rendered
// (Finish), since earlier draws of the same frame may still use it.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "frontend/content_source.h"
#include "live/frame_queue.h"

namespace torchlight::live {

class LiveContentSource : public frontend::ContentSource {
 public:
  explicit LiveContentSource(const frontend::ZipArchive& pak) : pak_(pak) {}

  struct Changes {
    std::vector<commands::TextureDesc> textures;           // to create (new or new content)
    std::vector<commands::ResourceId> destroyed_textures;  // to free
    std::vector<commands::ResourceId> destroyed_buffers;   // vertex and index buffers, to free
  };
  // Applies a frame's resource announcements.
  Changes Apply(LiveFrame& frame);
  // After the frame: drops superseded and destroyed content; returns the keys to free on the
  // host.
  std::vector<commands::Hash> Finish();

  const commands::Blob* Blob(commands::Hash key) const override;
  const commands::VertexBufferDesc* VertexBuffer(const commands::ResourceId& id) const override;
  const commands::VertexDeclarationContent* Declaration(const commands::ResourceId& id,
                                                        commands::Hash content) const override;
  const commands::TextureDesc* Texture(const commands::ResourceId& id) const override;
  const commands::TextureDesc* RenderTargetTexture(const std::string& name) const override;
  std::string ProgramName(const commands::ResourceId& id) const override;
  const rtss::Program* Program(const std::string& name) const override;
  std::optional<std::vector<uint8_t>> GameFile(const std::string& name) const override;

  size_t content_bytes() const;

 private:
  using Identity = std::pair<uint32_t, uint32_t>;  // guest address, generation
  static Identity Of(const commands::ResourceId& id) { return {id.guest_address, id.generation}; }
  struct IdentityHash {
    size_t operator()(const Identity& i) const {
      return std::hash<uint64_t>()(uint64_t(i.first) << 32 | i.second);
    }
  };
  // Looked up for every draw: hashed, not ordered (none is walked in order).
  template <typename T>
  using ByIdentity = std::unordered_map<Identity, T, IdentityHash>;

  const frontend::ZipArchive& pak_;
  std::unordered_map<commands::Hash, SnapshotStore::Content> contents_;
  ByIdentity<commands::Hash> latest_;  // latest content key per resource
  // How many resources have each key as their latest (identical content can be shared): Finish
  // keeps a superseded key that is still someone's latest without scanning latest_.
  std::unordered_map<commands::Hash, uint32_t> current_refs_;
  std::vector<commands::Hash> superseded_;     // freed by Finish
  void AddCurrent(commands::Hash key) { ++current_refs_[key]; }
  void DropCurrent(commands::Hash key) {
    auto it = current_refs_.find(key);
    if (it != current_refs_.end() && --it->second == 0) current_refs_.erase(it);
  }
  ByIdentity<commands::VertexBufferDesc> vertex_buffers_;
  std::map<std::tuple<uint32_t, uint32_t, commands::Hash>, commands::VertexDeclarationContent>
      declarations_;
  ByIdentity<commands::TextureDesc> textures_;
  std::map<std::string, Identity> render_targets_;
  ByIdentity<std::string> program_names_;
  std::map<std::string, std::optional<rtss::Program>> programs_;
};

}  // namespace torchlight::live
