// Readers that turn guest objects into neutral capture data. All offsets come from guest_abi.

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "commands/types.h"

namespace torchlight::capture {

class ConstantMirror;

std::array<float, 16> ReadMatrix(const uint8_t* m, uint32_t matrix);
std::array<float, 4> ReadColour(const uint8_t* m, uint32_t colour);

// RenderTarget / Viewport descriptions (null pointers produce empty commands).
commands::SetRenderTarget ReadRenderTarget(const uint8_t* m, uint32_t target);
commands::SetViewport ReadViewport(const uint8_t* m, uint32_t viewport);

// Description of a texture as the guest holds it (no id, no content).
commands::TextureDesc ReadTextureDesc(const uint8_t* m, uint32_t texture);

// Texture referenced by a TexturePtr-style pRep. Registers the description (and the raw content
// of dynamic textures) in the session and returns the id, or nullopt if never constructed.
std::optional<commands::ResourceId> CaptureTexture(const uint8_t* m, uint32_t texture);

// Vertex declaration content (registered in the session); returns the content hash.
commands::SetVertexDeclaration CaptureVertexDeclaration(const uint8_t* m, uint32_t declaration);

// Current vertex buffer binding (ids only).
commands::SetVertexBuffers ReadVertexBufferBinding(const uint8_t* m, uint32_t binding);

// Draw recorded after the guest D3D9 _render ran (buffers already uploaded).
// `live_keys` receives the live content key of each buffer snapshot (vertex buffers, then the
// index buffer), see Session::DrawEvent.
commands::Draw CaptureDraw(const uint8_t* m, uint32_t render_system, uint32_t operation,
                           std::vector<commands::Hash>& live_keys);

// Constants that D3D9RenderSystem::bindGpuProgramParameters uploads for `mask`. With `mirror`
// (live commands) the values read are stored in it, and with `filter` the ranges it already holds
// are left out (capture/constant_mirror.h); when that leaves nothing to change (no range, the same
// auto constants and transpose flag) `*unchanged` is set and the command is not to be sent, and
// nothing was copied for it.
commands::SetConstants ReadConstants(const uint8_t* m, uint32_t parameters, uint32_t gptype,
                                     uint32_t mask, ConstantMirror* mirror = nullptr,
                                     bool filter = false, bool* unchanged = nullptr);

// Program description (registered in the session).
commands::BindProgram CaptureProgram(const uint8_t* m, uint32_t program);

}  // namespace torchlight::capture
