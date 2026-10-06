#include "live/live_content_source.h"

namespace torchlight::live {

using namespace torchlight::commands;

LiveContentSource::Changes LiveContentSource::Apply(LiveFrame& frame) {
  Changes changes;
  for (const auto& c : frame.contents) {
    if (!c.content) continue;
    Hash key = c.content->hash;
    contents_[key] = c.content;
    auto [it, inserted] = latest_.try_emplace(Of(c.id), key);
    if (inserted) {
      AddCurrent(key);
    } else if (it->second != key) {
      superseded_.push_back(it->second);
      DropCurrent(it->second);
      AddCurrent(key);
      it->second = key;
    }
  }
  for (const auto& d : frame.vertex_buffers) vertex_buffers_[Of(d.id)] = d;
  for (const auto& d : frame.declarations) declarations_[{d.id.guest_address, d.id.generation, d.content}] = d;
  for (auto& t : frame.textures) {
    textures_[Of(t.id)] = t;
    if (t.render_target) render_targets_[t.name] = Of(t.id);
    changes.textures.push_back(t);
  }
  for (const auto& p : frame.programs) program_names_[Of(p.id)] = p.name;
  for (const auto& p : frame.program_sources) programs_[p.name] = rtss::Parse(p.source);
  for (const auto& id : frame.destroyed) {
    Identity who = Of(id);
    auto latest = latest_.find(who);
    if (latest != latest_.end()) {
      superseded_.push_back(latest->second);
      DropCurrent(latest->second);
      latest_.erase(latest);
    }
    vertex_buffers_.erase(who);
    program_names_.erase(who);
    if (id.kind == ResourceKind::kVertexBuffer ||
        id.kind == ResourceKind::kIndexBuffer) {
      changes.destroyed_buffers.push_back(id);
    }
    auto t = textures_.find(who);
    if (t != textures_.end()) {
      if (t->second.render_target) render_targets_.erase(t->second.name);
      textures_.erase(t);
      changes.destroyed_textures.push_back(id);
    }
    // Declarations are keyed by content too; drop every content of the destroyed one.
    for (auto it = declarations_.lower_bound({id.guest_address, id.generation, 0});
         it != declarations_.end() && std::get<0>(it->first) == id.guest_address &&
         std::get<1>(it->first) == id.generation;) {
      it = declarations_.erase(it);
    }
  }
  return changes;
}

std::vector<Hash> LiveContentSource::Finish() {
  std::vector<Hash> freed;
  for (Hash key : superseded_) {
    // A key superseded and then current again (same version, or another resource's identical
    // content) stays.
    if (current_refs_.count(key)) continue;
    if (contents_.erase(key)) freed.push_back(key);
  }
  superseded_.clear();
  return freed;
}

const Blob* LiveContentSource::Blob(Hash key) const {
  auto it = contents_.find(key);
  return it == contents_.end() ? nullptr : it->second.get();
}

const VertexBufferDesc* LiveContentSource::VertexBuffer(const ResourceId& id) const {
  auto it = vertex_buffers_.find(Of(id));
  return it == vertex_buffers_.end() ? nullptr : &it->second;
}

const VertexDeclarationContent* LiveContentSource::Declaration(const ResourceId& id,
                                                               Hash content) const {
  auto it = declarations_.find({id.guest_address, id.generation, content});
  return it == declarations_.end() ? nullptr : &it->second;
}

const TextureDesc* LiveContentSource::Texture(const ResourceId& id) const {
  auto it = textures_.find(Of(id));
  return it == textures_.end() ? nullptr : &it->second;
}

const TextureDesc* LiveContentSource::RenderTargetTexture(const std::string& name) const {
  auto it = render_targets_.find(name);
  if (it == render_targets_.end()) return nullptr;
  auto t = textures_.find(it->second);
  return t == textures_.end() ? nullptr : &t->second;
}

std::string LiveContentSource::ProgramName(const ResourceId& id) const {
  auto it = program_names_.find(Of(id));
  return it == program_names_.end() ? std::string() : it->second;
}

const rtss::Program* LiveContentSource::Program(const std::string& name) const {
  auto it = programs_.find(name);
  return it == programs_.end() || !it->second ? nullptr : &*it->second;
}

std::optional<std::vector<uint8_t>> LiveContentSource::GameFile(const std::string& name) const {
  return pak_.Read(name);
}

size_t LiveContentSource::content_bytes() const {
  size_t n = 0;
  for (const auto& [key, c] : contents_) n += c->bytes.size();
  return n;
}

}  // namespace torchlight::live
