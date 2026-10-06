#include "frontend/content_source.h"

namespace torchlight::frontend {

using namespace torchlight::commands;

CaptureContentSource::CaptureContentSource(const Capture& c, const ZipArchive& pak) : pak_(pak) {
  for (const auto& b : c.blobs) blobs_[b.hash] = &b;
  for (const auto& d : c.vertex_buffers) vertex_buffers_[{d.id.guest_address, d.id.generation}] = &d;
  for (const auto& d : c.vertex_declarations)
    declarations_[{d.id.guest_address, d.id.generation, d.content}] = &d;
  for (const auto& t : c.textures) {
    textures_[{t.id.guest_address, t.id.generation}] = &t;
    if (t.render_target) render_targets_[t.name] = &t;
  }
  for (const auto& p : c.programs) program_names_[{p.id.guest_address, p.id.generation}] = p.name;
  for (const auto& p : c.program_sources) programs_[p.name] = rtss::Parse(p.source);
}

const Blob* CaptureContentSource::Blob(Hash key) const {
  auto it = blobs_.find(key);
  return it == blobs_.end() ? nullptr : it->second;
}

const VertexBufferDesc* CaptureContentSource::VertexBuffer(const ResourceId& id) const {
  auto it = vertex_buffers_.find({id.guest_address, id.generation});
  return it == vertex_buffers_.end() ? nullptr : it->second;
}

const VertexDeclarationContent* CaptureContentSource::Declaration(const ResourceId& id,
                                                                  Hash content) const {
  auto it = declarations_.find({id.guest_address, id.generation, content});
  return it == declarations_.end() ? nullptr : it->second;
}

const TextureDesc* CaptureContentSource::Texture(const ResourceId& id) const {
  auto it = textures_.find({id.guest_address, id.generation});
  return it == textures_.end() ? nullptr : it->second;
}

const TextureDesc* CaptureContentSource::RenderTargetTexture(const std::string& name) const {
  auto it = render_targets_.find(name);
  return it == render_targets_.end() ? nullptr : it->second;
}

std::string CaptureContentSource::ProgramName(const ResourceId& id) const {
  auto it = program_names_.find({id.guest_address, id.generation});
  return it == program_names_.end() ? std::string() : it->second;
}

const rtss::Program* CaptureContentSource::Program(const std::string& name) const {
  auto it = programs_.find(name);
  return it == programs_.end() || !it->second ? nullptr : &*it->second;
}

std::optional<std::vector<uint8_t>> CaptureContentSource::GameFile(const std::string& name) const {
  return pak_.Read(name);
}

}  // namespace torchlight::frontend
