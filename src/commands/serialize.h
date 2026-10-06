// Binary serialization of a capture: "TLCAP" file of tagged chunks, little-endian.
//
// File: magic "TLCAP\0\0\0", u32 version_major, u32 version_minor, then chunks
// {u32 tag, u64 length, payload}. A reader rejects a different major version and skips unknown
// chunk tags. Commands are {u16 opcode, u16 flags, u32 payload length, payload}.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "commands/types.h"

namespace torchlight::commands {

std::vector<uint8_t> SerializeCapture(const Capture& capture);

// Returns false and sets `error` on malformed input or an unsupported major version.
bool DeserializeCapture(const std::vector<uint8_t>& bytes, Capture& capture, std::string& error);

bool WriteCaptureFile(const std::string& path, const Capture& capture, std::string& error);
bool ReadCaptureFile(const std::string& path, Capture& capture, std::string& error);

// ---- live session recording ------------------------------------------------------------------
// One frame of a live session recording (live/session_file.h): what the live mode's backend
// consumed, in order, with the resources announced in it. Content is written once per key: a
// later frame that holds the same content carries only its key.
struct SessionContent {
  ResourceId id;
  Blob blob;                    // blob.bytes empty when !bytes_in_record
  bool bytes_in_record = true;  // false: written with an earlier frame (same blob.hash)
};
struct SessionFrame {
  uint64_t swap = 0;
  uint32_t dropped_before = 0;
  std::vector<Command> commands;
  std::vector<VertexBufferDesc> vertex_buffers;
  std::vector<IndexBufferDesc> index_buffers;
  std::vector<VertexDeclarationContent> declarations;
  std::vector<TextureDesc> textures;
  std::vector<ProgramDesc> programs;
  std::vector<ProgramSource> program_sources;
  std::vector<ResourceId> destroyed;
  std::vector<SessionContent> contents;
};
std::vector<uint8_t> SerializeSessionFrame(SessionFrame& frame);
bool DeserializeSessionFrame(const uint8_t* data, size_t size, SessionFrame& frame,
                             std::string& error);

}  // namespace torchlight::commands
