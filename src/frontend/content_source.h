// Where the frontend gets resource descriptions and content from.
//
// The frontend turns a stream of commands into backend calls; everything a command refers to by
// identity (buffer content, vertex declarations, textures, programs, game files) comes through
// this interface. A capture file provides it all up front (CaptureContentSource); the live mode
// provides it as the guest creates and updates resources.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "commands/types.h"
#include "frontend/zip_archive.h"
#include "rtss/rtss_program.h"

namespace torchlight::frontend {

class ContentSource {
 public:
  virtual ~ContentSource() = default;

  // Buffer or dynamic texture content by key (BufferSnapshot::blob, TextureDesc::content).
  virtual const commands::Blob* Blob(commands::Hash key) const = 0;
  virtual const commands::VertexBufferDesc* VertexBuffer(const commands::ResourceId& id) const = 0;
  virtual const commands::VertexDeclarationContent* Declaration(const commands::ResourceId& id,
                                                                commands::Hash content) const = 0;
  virtual const commands::TextureDesc* Texture(const commands::ResourceId& id) const = 0;
  // Render target texture by its texture name (render targets are named "rtt/<buffer>/<name>").
  virtual const commands::TextureDesc* RenderTargetTexture(const std::string& name) const = 0;
  virtual std::string ProgramName(const commands::ResourceId& id) const = 0;
  // Parsed source of a program, by name (null when not recorded or not parsed).
  virtual const rtss::Program* Program(const std::string& name) const = 0;
  // A file of the game data (pak.zip), e.g. a named texture.
  virtual std::optional<std::vector<uint8_t>> GameFile(const std::string& name) const = 0;
};

// Everything from a capture file and the game's pak.zip.
class CaptureContentSource : public ContentSource {
 public:
  CaptureContentSource(const commands::Capture& capture, const ZipArchive& pak);

  const commands::Blob* Blob(commands::Hash key) const override;
  const commands::VertexBufferDesc* VertexBuffer(const commands::ResourceId& id) const override;
  const commands::VertexDeclarationContent* Declaration(const commands::ResourceId& id,
                                                        commands::Hash content) const override;
  const commands::TextureDesc* Texture(const commands::ResourceId& id) const override;
  const commands::TextureDesc* RenderTargetTexture(const std::string& name) const override;
  std::string ProgramName(const commands::ResourceId& id) const override;
  const rtss::Program* Program(const std::string& name) const override;
  std::optional<std::vector<uint8_t>> GameFile(const std::string& name) const override;

 private:
  const ZipArchive& pak_;
  std::unordered_map<commands::Hash, const commands::Blob*> blobs_;
  std::map<std::pair<uint32_t, uint32_t>, const commands::VertexBufferDesc*> vertex_buffers_;
  std::map<std::tuple<uint32_t, uint32_t, commands::Hash>,
           const commands::VertexDeclarationContent*>
      declarations_;
  std::map<std::pair<uint32_t, uint32_t>, const commands::TextureDesc*> textures_;
  std::map<std::string, const commands::TextureDesc*> render_targets_;
  std::map<std::pair<uint32_t, uint32_t>, std::string> program_names_;  // (address, generation)
  std::map<std::string, std::optional<rtss::Program>> programs_;
};

}  // namespace torchlight::frontend
