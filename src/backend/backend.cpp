// Host render backend: OGRE 14 with the GL3+ render system (core profile, no fixed function),
// offscreen.
//
// C API in backend/backend_api.h. All drawing goes through OGRE's RenderSystem API (no direct GL
// calls); guest conventions are converted only in backend/xbox_to_gl_conventions. Notes on how
// OGRE 14's API is used:
// - guest draws run programs the RTSS generates from a Pass that describes the draw's structure
//   (lighting, vertex colour, fog, texture stages), one per distinct structure, cached; their
//   auto constants come from the backend's own AutoParamDataSource (ApplyProgram). There is no
//   scene: a SceneManager exists only to own the camera and the light the data source reads.
//   The data source also negates clip y for render textures (GL textures have their first row at
//   the bottom); the front face follows on the render system's side (flipFrontFace);
// - the full-screen passes (gamma, present) use two small programs of the backend's own;
// - texture filtering, addressing and anisotropy go through Sampler objects (cached per state);
// - blending and the colour write mask through setColourBlendState;
// - the projective texgen projector is a Frustum with custom matrices;
// - a child window is resized by OGRE itself (RenderWindow::resize).
// Platform-specific code (the parent window parameter, key input) lives in the platform module
// (platform/platform.h), not here.

#include "backend/backend_api.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Ogre.h>
#include <OgreAutoParamDataSource.h>
#include <OgreRTShaderSystem.h>
#include <OgreShaderExHardwareSkinning.h>
#include <OgreShaderFFPRenderState.h>
#include <OgreShaderFunction.h>
#include <OgreShaderProgram.h>
#include <OgreShaderProgramSet.h>

#include "backend/xbox_to_gl_conventions.h"
#include "platform/platform.h"

namespace {

namespace conv = torchlight::backend;

// ---- neutral -> OGRE 14 enums (explicit; numbering is never assumed equal) -------------------

bool ToOgre(uint8_t v, Ogre::SceneBlendFactor& out) {
  switch (v) {
    case 0: out = Ogre::SBF_ONE; return true;
    case 1: out = Ogre::SBF_ZERO; return true;
    case 2: out = Ogre::SBF_DEST_COLOUR; return true;
    case 3: out = Ogre::SBF_SOURCE_COLOUR; return true;
    case 4: out = Ogre::SBF_ONE_MINUS_DEST_COLOUR; return true;
    case 5: out = Ogre::SBF_ONE_MINUS_SOURCE_COLOUR; return true;
    case 6: out = Ogre::SBF_DEST_ALPHA; return true;
    case 7: out = Ogre::SBF_SOURCE_ALPHA; return true;
    case 8: out = Ogre::SBF_ONE_MINUS_DEST_ALPHA; return true;
    case 9: out = Ogre::SBF_ONE_MINUS_SOURCE_ALPHA; return true;
  }
  return false;
}

bool ToOgre(uint8_t v, Ogre::CompareFunction& out) {
  switch (v) {
    case 0: out = Ogre::CMPF_ALWAYS_FAIL; return true;
    case 1: out = Ogre::CMPF_ALWAYS_PASS; return true;
    case 2: out = Ogre::CMPF_LESS; return true;
    case 3: out = Ogre::CMPF_LESS_EQUAL; return true;
    case 4: out = Ogre::CMPF_EQUAL; return true;
    case 5: out = Ogre::CMPF_NOT_EQUAL; return true;
    case 6: out = Ogre::CMPF_GREATER_EQUAL; return true;
    case 7: out = Ogre::CMPF_GREATER; return true;
  }
  return false;
}

Ogre::CullingMode ToOgreCull(uint8_t v) {
  switch (v) {
    case 1: return Ogre::CULL_CLOCKWISE;
    case 2: return Ogre::CULL_ANTICLOCKWISE;
    default: return Ogre::CULL_NONE;
  }
}

Ogre::PolygonMode ToOgrePolygon(uint8_t v) {
  switch (v) {
    case 0: return Ogre::PM_POINTS;
    case 1: return Ogre::PM_WIREFRAME;
    default: return Ogre::PM_SOLID;
  }
}

Ogre::FilterOptions ToOgreFilter(uint8_t v) {
  switch (v) {
    case 0: return Ogre::FO_NONE;
    case 1: return Ogre::FO_POINT;
    case 3: return Ogre::FO_ANISOTROPIC;
    default: return Ogre::FO_LINEAR;
  }
}

Ogre::TextureAddressingMode ToOgreAddress(uint8_t v) {
  switch (v) {
    case 1: return Ogre::TAM_MIRROR;
    case 2: return Ogre::TAM_CLAMP;
    case 3: return Ogre::TAM_BORDER;
    default: return Ogre::TAM_WRAP;
  }
}

bool ToOgre(uint8_t v, Ogre::RenderOperation::OperationType& out) {
  switch (v) {
    case 0: out = Ogre::RenderOperation::OT_POINT_LIST; return true;
    case 1: out = Ogre::RenderOperation::OT_LINE_LIST; return true;
    case 2: out = Ogre::RenderOperation::OT_LINE_STRIP; return true;
    case 3: out = Ogre::RenderOperation::OT_TRIANGLE_LIST; return true;
    case 4: out = Ogre::RenderOperation::OT_TRIANGLE_STRIP; return true;
    case 5: out = Ogre::RenderOperation::OT_TRIANGLE_FAN; return true;
  }
  return false;
}

// Host vertex element type; guest colours are converted to bytes R, G, B, A (see conventions).
bool ToOgre(uint8_t v, Ogre::VertexElementType& out, bool& is_colour) {
  is_colour = false;
  switch (v) {
    case 0: out = Ogre::VET_FLOAT1; return true;
    case 1: out = Ogre::VET_FLOAT2; return true;
    case 2: out = Ogre::VET_FLOAT3; return true;
    case 3: out = Ogre::VET_FLOAT4; return true;
    case 4:   // colour
    case 10:  // colour ARGB
    case 11:  // colour ABGR
      out = Ogre::VET_UBYTE4_NORM;
      is_colour = true;
      return v != 11;  // ABGR guest colours are not produced by the guest D3D9 path; refuse
    case 5: out = Ogre::VET_SHORT1; return true;
    case 6: out = Ogre::VET_SHORT2; return true;
    case 7: out = Ogre::VET_SHORT3; return true;
    case 8: out = Ogre::VET_SHORT4; return true;
    case 9: out = Ogre::VET_UBYTE4; return true;
  }
  return false;
}

bool ToOgre(uint8_t v, Ogre::VertexElementSemantic& out) {
  switch (v) {
    case 0: out = Ogre::VES_POSITION; return true;
    case 1: out = Ogre::VES_BLEND_WEIGHTS; return true;
    case 2: out = Ogre::VES_BLEND_INDICES; return true;
    case 3: out = Ogre::VES_NORMAL; return true;
    case 4: out = Ogre::VES_DIFFUSE; return true;
    case 5: out = Ogre::VES_SPECULAR; return true;
    case 6: out = Ogre::VES_TEXTURE_COORDINATES; return true;
    case 7: out = Ogre::VES_BINORMAL; return true;
    case 8: out = Ogre::VES_TANGENT; return true;
  }
  return false;
}

bool ToOgre(uint8_t v, Ogre::LayerBlendOperationEx& out) {
  switch (v) {
    case TL_OP_SOURCE1: out = Ogre::LBX_SOURCE1; return true;
    case TL_OP_SOURCE2: out = Ogre::LBX_SOURCE2; return true;
    case TL_OP_MODULATE: out = Ogre::LBX_MODULATE; return true;
    case TL_OP_MODULATE_X2: out = Ogre::LBX_MODULATE_X2; return true;
    case TL_OP_MODULATE_X4: out = Ogre::LBX_MODULATE_X4; return true;
    case TL_OP_ADD: out = Ogre::LBX_ADD; return true;
    case TL_OP_ADD_SIGNED: out = Ogre::LBX_ADD_SIGNED; return true;
    case TL_OP_ADD_SMOOTH: out = Ogre::LBX_ADD_SMOOTH; return true;
    case TL_OP_SUBTRACT: out = Ogre::LBX_SUBTRACT; return true;
  }
  return false;
}

bool ToOgre(uint8_t v, Ogre::LayerBlendSource& out) {
  switch (v) {
    case TL_SRC_TEXTURE: out = Ogre::LBS_TEXTURE; return true;
    case TL_SRC_CURRENT: out = Ogre::LBS_CURRENT; return true;
    case TL_SRC_DIFFUSE: out = Ogre::LBS_DIFFUSE; return true;
    case TL_SRC_CONSTANT: out = Ogre::LBS_MANUAL; return true;
  }
  return false;
}

Ogre::Matrix4 ToMatrix(const float* m) {
  return Ogre::Matrix4(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11],
                       m[12], m[13], m[14], m[15]);
}

// Transform, lighting, fog and skinning of a draw: the values behind its RTSS program
// (ApplyProgram).
struct ProgramInputs {
  Ogre::Matrix4 world = Ogre::Matrix4::IDENTITY;
  Ogre::Matrix4 projection = Ogre::Matrix4::IDENTITY;  // GL clip; the data source flips it for RTs
  // The guest's own clip matrix (world-view-projection), in GL conventions: the position
  // transform. projection * world equals it, but when world is the guest world-view split from it
  // (GuestWorldViewFromWvp) the product carries float error, enough to push geometry the guest
  // places exactly on the near plane (z = 0 in D3D, -w in GL) outside the clip volume.
  Ogre::Matrix4 clip = Ogre::Matrix4::IDENTITY;
  bool lighting = false;
  Ogre::TrackVertexColourType tracking = Ogre::TVC_NONE;
  Ogre::ColourValue diffuse = Ogre::ColourValue::White, emissive = Ogre::ColourValue::Black;
  bool light = false;
  float towards_light[3] = {0, 0, 1};  // view space
  Ogre::ColourValue light_diffuse = Ogre::ColourValue::Black;
  bool fog = false;
  float fog_start = 0, fog_end = 0;
  Ogre::ColourValue fog_colour = Ogre::ColourValue::Black;
  // Skinning (RTSS HardwareSkinning): palette size, influences and the guest's bones (3x4
  // row-major, world space).
  uint32_t skin_bones = 0, skin_weights = 0;
  const float* skin_palette = nullptr;
  // Alpha test (render state; GuestAlphaTest).
  Ogre::CompareFunction alpha_function = Ogre::CMPF_ALWAYS_PASS;
  uint8_t alpha_reference = 0;
};

}  // namespace

// Internal render scale, a host feature (not a guest convention): the main colour target and the
// output are created at Host(guest size), so the frame is drawn at more (or fewer) pixels than the
// guest asked for. The guest's own render targets keep its sizes: it samples them as textures
// (shadow and light maps, projected with clamp addressing) and their texels are what it designed
// for; drawn larger, a caster that only grazes the edge at 512 covers the edge texel at 768 and
// clamping stretches it across the ground. Everything the guest gives in pixels stays in its own
// units and is converted here or relative to the guest size:
//  - viewports go to OGRE as fractions of the target's guest size (tl_backend_set_viewport);
//  - D3D pixel centres move by half a guest pixel (the same image at any scale, DrawImpl);
//  - the probe rectangle is scaled (tl_backend_draw);
//  - read-backs return guest sizes, filtered down from the host size (ReadTarget).
struct RenderScale {
  float factor = 1;
  uint32_t Host(uint32_t guest) const {
    return std::max<uint32_t>(1, uint32_t(std::lround(guest * factor)));
  }
};

struct tl_backend {
  Ogre::LogManager* log_manager = nullptr;  // ours when no other backend made one first
  Ogre::Root* root = nullptr;
  Ogre::RenderSystem* rs = nullptr;
  Ogre::RenderWindow* window = nullptr;
  // The native window OGRE draws in where it cannot make its own (platform.h; macOS); outlives
  // `window`.
  std::unique_ptr<torchlight::platform::OgreTopLevelWindow> top_level;
  bool visible_window = false;
  bool child_window = false;  // inside another application's window (tl_backend_create_child)
  Ogre::Viewport* window_viewport = nullptr;
  std::unique_ptr<torchlight::platform::KeyReader> keyboard;  // top-level window only
  uint32_t keys = 0;  // tl_key bits pressed since tl_backend_take_keys
  uint32_t width = 0, height = 0;  // the guest's frame size (main target, output)
  RenderScale scale;
  std::string renderer;

  struct Target {
    Ogre::TexturePtr texture;
    Ogre::RenderTexture* rt = nullptr;
    Ogre::Viewport* viewport = nullptr;
    uint32_t guest_width = 0, guest_height = 0;  // as the guest created it
    bool scaled = false;  // drawn at the render scale (RenderScale): the main target and output
    int32_t viewport_width = 0, viewport_height = 0;  // the current viewport, guest pixels
  };
  Target main;
  Target output;  // main after the display gamma (DisplayGammaPass)
  std::map<uint64_t, Target> targets;
  Ogre::TexturePtr gamma_lut;  // 256 x 1, one texel per 8-bit value
  Ogre::HighLevelGpuProgramPtr quad_vertex_program, gamma_program, copy_program;
  Ogre::GpuProgramParametersSharedPtr gamma_params, copy_params;
  Ogre::HardwareVertexBufferSharedPtr quad;
  Ogre::HardwareVertexBufferSharedPtr window_quad;  // for tl_backend_present
  // Host UI (tl_backend_set_ui_frame), drawn by DrawUiPass.
  struct UiFrame {
    float space_width = 0, space_height = 0;
    std::vector<tl_ui_vertex> vertices;
    std::vector<uint16_t> indices;
    std::vector<tl_ui_cmd> cmds;
  };
  struct UiTexture {
    Ogre::TexturePtr texture;
    bool linear = true, repeat = false;
  };
  UiFrame ui;
  std::map<uint64_t, UiTexture> ui_textures;
  Ogre::TexturePtr ui_white;  // colour-only commands
  Ogre::HighLevelGpuProgramPtr ui_vertex_program, ui_fragment_program;
  Ogre::GpuProgramParametersSharedPtr ui_vertex_params, ui_fragment_params;
  // Clears (tl_backend_clear) take the viewport's area only, as the guest's D3D clears do. Some
  // render systems clear the whole target whatever the viewport (Direct3D 11's
  // ClearRenderTargetView; GL3+ scissors to the viewport): measured once at creation
  // (MeasureClearArea); on those a clear of part of the target draws a quad (ClearViewportByQuad).
  bool clears_whole_target = false;
  // conv::DepthBiasConstantForHost: what the render system multiplies the constant bias by.
  float depth_bias_factor = 1.0f;
  Ogre::HighLevelGpuProgramPtr clear_vertex_program, clear_fragment_program;
  Ogre::GpuProgramParametersSharedPtr clear_vertex_params, clear_fragment_params;
  Ogre::HardwareVertexBufferSharedPtr ui_vertex_buffer;
  Ogre::HardwareIndexBufferSharedPtr ui_index_buffer;
  Target* current = nullptr;
  Ogre::RenderTarget* active = nullptr;  // the target last set on the render system

  struct Buffer {
    std::vector<uint8_t> bytes;  // fetch swap applied (vertex) / host order (index)
    uint32_t index_size = 0;
  };
  std::map<uint64_t, Buffer> vertex_buffers, index_buffers;
  // GPU buffers per guest buffer (tl_draw stream_owners / index_owner), holding one content
  // version at a time: a new version is written over the old one (whole buffer, discarding it, so
  // draws still queued keep the data they were issued with). Vertex buffers also per stride and
  // layout (colour conversion and skinning lanes rewrite the bytes).
  struct HostVertexKey {
    uint64_t owner = 0;
    uint32_t stride = 0;
    uint64_t layout = 0;
    auto operator<=>(const HostVertexKey&) const = default;
  };
  struct HostVertex {
    Ogre::HardwareVertexBufferSharedPtr buffer;
    uint64_t content = 0;
    uint64_t serial = 0;  // unique per GPU buffer created (VertexLayout::bound)
  };
  struct HostIndex {
    Ogre::HardwareIndexBufferSharedPtr buffer;
    uint64_t content = 0;
  };
  std::map<HostVertexKey, HostVertex> host_vertex;
  // Vertex data per declaration layout (FNV-1a of the elements), reused by every draw with that
  // layout: only the buffer bindings change per draw. A VertexData of its own per draw meant a
  // vertex declaration, and in GL3+ a vertex array object, created and destroyed for each one.
  // GL3+ keeps a vertex array object per declaration and re-specifies it only when the bound
  // buffers' addresses change; a freed buffer whose address a new one reuses would go unnoticed,
  // so each layout remembers the serials it had bound and forces the update when they change.
  struct VertexLayout {
    std::unique_ptr<Ogre::VertexData> data;
    std::vector<uint64_t> bound;  // HostVertex serial per stream, 0 = none
  };
  std::unordered_map<uint64_t, VertexLayout> vertex_layouts;
  uint64_t host_buffer_serial = 0;
  std::map<uint64_t, HostIndex> host_index;
  std::map<uint64_t, Ogre::TexturePtr> textures;
  std::unordered_map<uint64_t, Ogre::SamplerPtr> samplers;  // by packed state (GetSampler)
  uint32_t texture_serial = 0;
  uint32_t enabled_units = 0;
  std::unique_ptr<Ogre::Frustum> projectors[TL_MAX_STAGES];
  // RTSS programs by structural key (ProgramKey), and what feeds their auto constants.
  struct Program {
    Ogre::MaterialPtr material;
    Ogre::Pass* pass = nullptr;  // the generated pass: programs, colours, texture transforms
    Ogre::GpuProgramParametersSharedPtr vertex_params, fragment_params;
    std::string alpha_function;  // the generated name of kAlphaFunctionUniform
    bool failed = false;
  };
  std::unordered_map<std::string, Program> programs;
  uint32_t program_serial = 0;
  std::vector<Ogre::Affine3> world_matrices;  // ApplyProgram's world + bone palette
  std::unique_ptr<Ogre::RTShader::SubRenderStateFactory> guest_texgen_factory, guest_pixel_fog,
      guest_alpha_test, guest_lighting;
  const Program* bound_program = nullptr;
  std::unique_ptr<Ogre::AutoParamDataSource> params_source;
  Ogre::SceneManager* scene = nullptr;  // never rendered
  Ogre::Camera* camera = nullptr;
  Ogre::Light* light = nullptr;
  Ogre::SceneNode* light_node = nullptr;
  Ogre::TexturePtr placeholder_2d, placeholder_cube;  // texture types for program generation
  bool unit_cube[TL_MAX_STAGES] = {};  // texture bound per unit is a cube map
  uint32_t unit_coord_set[TL_MAX_STAGES] = {};
  uint32_t notes = 0;  // tl_draw_note bits of the last draw
  tl_backend_counters counters = {};  // since tl_backend_take_counters
  bool scene_clip = false;  // tl_backend_set_scene_clip
  tl_state state{};    // last state set (restored after a probe pass)
  bool probe = false;
  uint32_t probe_rect[4] = {0, 0, 0, 0};
  uint32_t probe_samples = 0;
  Ogre::HardwareOcclusionQuery* query = nullptr;
};

namespace {

void SetError(char* error, uint32_t size, const std::string& message) {
  if (error && size) {
    std::strncpy(error, message.c_str(), size - 1);
    error[size - 1] = 0;
  }
}

void SetViewport(tl_backend* b, Ogre::Viewport* viewport) {
  b->rs->_setViewport(viewport);
  b->active = viewport->getTarget();
}

// SceneClip16x9: the front end's 3D scene on a frame wider than 16:9 (tl_backend_set_scene_clip).
// The centred 16:9 strip of the main target, in host pixels; nothing when the clip is off, the
// target is not the main one or the frame is not wider than 16:9.
std::optional<Ogre::Rect> SceneClip16x9(const tl_backend* b) {
  if (!b->scene_clip || b->current != &b->main || !b->main.rt) return std::nullopt;
  const long width = long(b->main.rt->getWidth()), height = long(b->main.rt->getHeight());
  const long strip = std::lround(height * 16.0 / 9.0);
  if (strip >= width) return std::nullopt;
  const long left = (width - strip) / 2;
  return Ogre::Rect(left, 0, left + strip, height);
}

void SetColourBlend(tl_backend* b, Ogre::SceneBlendFactor src, Ogre::SceneBlendFactor dst,
                    uint8_t write_mask) {
  Ogre::ColourBlendState state;
  state.sourceFactor = state.sourceFactorAlpha = src;
  state.destFactor = state.destFactorAlpha = dst;
  state.writeR = (write_mask & 1) != 0;
  state.writeG = (write_mask & 2) != 0;
  state.writeB = (write_mask & 4) != 0;
  state.writeA = (write_mask & 8) != 0;
  b->rs->setColourBlendState(state);
}

// Sampler for a filtering / addressing / anisotropy combination, created once.
Ogre::SamplerPtr GetSampler(tl_backend* b, Ogre::FilterOptions min, Ogre::FilterOptions mag,
                            Ogre::FilterOptions mip, Ogre::TextureAddressingMode u,
                            Ogre::TextureAddressingMode v, Ogre::TextureAddressingMode w,
                            unsigned int anisotropy) {
  uint64_t key = uint64_t(min) | uint64_t(mag) << 8 | uint64_t(mip) << 16 | uint64_t(u) << 24 |
                 uint64_t(v) << 32 | uint64_t(w) << 40 | uint64_t(anisotropy & 0xFFFF) << 48;
  Ogre::SamplerPtr& s = b->samplers[key];
  if (!s) {
    s = Ogre::TextureManager::getSingleton().createSampler();
    s->setFiltering(min, mag, mip);
    s->setAddressingMode(u, v, w);
    s->setAnisotropy(anisotropy);
  }
  return s;
}

// A render target of the guest's size w x h; `scaled` ones are created at the render scale's host
// size.
bool MakeTarget(tl_backend* b, const std::string& name, uint32_t w, uint32_t h, bool scaled,
                tl_backend::Target& t) {
  t.scaled = scaled;
  t.guest_width = w;
  t.guest_height = h;
  t.viewport_width = int32_t(w);
  t.viewport_height = int32_t(h);
  t.texture = Ogre::TextureManager::getSingleton().createManual(
      name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_2D,
      scaled ? b->scale.Host(w) : w, scaled ? b->scale.Host(h) : h, 0, Ogre::PF_A8R8G8B8,
      Ogre::TU_RENDERTARGET);
  t.rt = t.texture->getBuffer()->getRenderTarget();
  t.rt->setAutoUpdated(false);
  t.viewport = t.rt->addViewport(nullptr);
  t.viewport->setClearEveryFrame(false);
  t.viewport->setOverlaysEnabled(false);
  return t.rt != nullptr;
}

// A scaled target again at the current render scale (its content is lost: it is drawn every
// frame).
void RemakeTarget(tl_backend* b, tl_backend::Target& t) {
  const std::string name = t.texture->getName();
  const uint32_t w = t.guest_width, h = t.guest_height;
  t.texture.reset();
  Ogre::TextureManager::getSingleton().remove(
      name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
  if (!MakeTarget(b, name, w, h, t.scaled, t)) OGRE_EXCEPT(Ogre::Exception::ERR_RENDERINGAPI_ERROR,
                                                 "cannot recreate " + name, "RemakeTarget");
}

// Reads a target as RGBA8 at its guest size, top row first (filtered down when the render scale
// made it larger, up when smaller).
void ReadTarget(const tl_backend::Target& t, void* rgba, uint32_t stride) {
  const uint32_t host_w = t.rt->getWidth(), host_h = t.rt->getHeight();
  Ogre::PixelBox out(t.guest_width, t.guest_height, 1, Ogre::PF_BYTE_RGBA, rgba);
  out.rowPitch = stride / 4;
  out.slicePitch = out.rowPitch * t.guest_height;
  if (host_w == t.guest_width && host_h == t.guest_height) {
    t.rt->copyContentsToMemory(Ogre::Box(0, 0, host_w, host_h), out, Ogre::RenderTarget::FB_AUTO);
    return;
  }
  Ogre::Image host(Ogre::PF_BYTE_RGBA, host_w, host_h);
  t.rt->copyContentsToMemory(Ogre::Box(0, 0, host_w, host_h), host.getPixelBox(),
                             Ogre::RenderTarget::FB_AUTO);
  host.resize(t.guest_width, t.guest_height, Ogre::Image::FILTER_BILINEAR);
  Ogre::PixelUtil::bulkPixelConversion(host.getPixelBox(), out);
}

bool ToBlendMode(const tl_combine& c, Ogre::LayerBlendType type, Ogre::LayerBlendModeEx& out) {
  out.blendType = type;
  if (!ToOgre(c.op, out.operation) || !ToOgre(c.source1, out.source1) ||
      !ToOgre(c.source2, out.source2)) {
    return false;
  }
  out.colourArg1 = Ogre::ColourValue(c.constant1[0], c.constant1[1], c.constant1[2], c.constant1[3]);
  out.colourArg2 = Ogre::ColourValue(c.constant2[0], c.constant2[1], c.constant2[2], c.constant2[3]);
  out.alphaArg1 = c.constant1[3];
  out.alphaArg2 = c.constant2[3];
  return true;
}

// What a draw's texture unit does, for the program generation (ProgramKey) and its constants.
struct UnitDesc {
  bool present = false;  // sampled with a texture bound
  Ogre::LayerBlendModeEx colour, alpha;
  Ogre::TexCoordCalcMethod texgen = Ogre::TEXCALC_NONE;
  uint32_t coord_set = 0;
  bool has_matrix = false;
  Ogre::Matrix4 matrix = Ogre::Matrix4::IDENTITY;
  // Coordinates the program computes itself (GuestTexgen); coord_set is then the reserved output
  // set.
  enum class Guest : uint8_t { kNone, kViewNormal2d, kReflect } guest_texgen = Guest::kNone;
};

void Combine(Ogre::LayerBlendModeEx& m, Ogre::LayerBlendType type, Ogre::LayerBlendOperationEx op,
             Ogre::LayerBlendSource source1, Ogre::LayerBlendSource source2) {
  m.blendType = type;
  m.operation = op;
  m.source1 = source1;
  m.source2 = source2;
}

// Texture units of a draw. With guest program stages: what the guest program does per stage.
// Without: every bound unit modulates its texture with the previous result, and unit 0 combines
// the texture with the colour (or takes the texture alone when the colour is constant white).
// Sets the projective projectors (they depend on the host world matrix, `world_view`). Returns the
// number of units the program covers (the last present one + 1).
uint32_t DescribeUnits(tl_backend* b, const tl_draw* d, const float* world_view,
                       UnitDesc units[TL_MAX_STAGES]) {
  size_t host_units = b->rs->getCapabilities()->getNumTextureUnits();
  uint32_t count = 0;
  for (uint32_t u = 0; u < TL_MAX_STAGES; ++u) {
    UnitDesc& unit = units[u];
    unit = UnitDesc();
    bool bound = (b->enabled_units & (1u << u)) != 0;
    if (!d->program_stages) {
      if (!bound) continue;
      unit.present = true;
      unit.coord_set = b->unit_coord_set[u];
      Ogre::LayerBlendSource previous = u == 0 ? Ogre::LBS_DIFFUSE : Ogre::LBS_CURRENT;
      Ogre::LayerBlendOperationEx op = Ogre::LBX_MODULATE;
      if (u == 0) {
        bool has_diffuse = false;
        for (uint32_t i = 0; i < d->element_count; ++i) has_diffuse |= d->elements[i].semantic == 4;
        bool modulate = d->colour_source == TL_COLOUR_LIGHTING ||
                        (d->colour_source == TL_COLOUR_VERTEX && has_diffuse);
        op = modulate ? Ogre::LBX_MODULATE : Ogre::LBX_SOURCE1;
      }
      Combine(unit.colour, Ogre::LBT_COLOUR, op, Ogre::LBS_TEXTURE, previous);
      Combine(unit.alpha, Ogre::LBT_ALPHA, op, Ogre::LBS_TEXTURE, previous);
      count = u + 1;
      continue;
    }
    const tl_stage& st = d->stages[u];
    if (!st.sampled || !bound) continue;
    if (u >= host_units) {
      b->notes |= TL_NOTE_STAGE_DROPPED;
      continue;
    }
    if (!ToBlendMode(st.colour, Ogre::LBT_COLOUR, unit.colour) ||
        !ToBlendMode(st.alpha, Ogre::LBT_ALPHA, unit.alpha)) {
      b->notes |= TL_NOTE_STAGE_APPROXIMATED;
      Combine(unit.colour, Ogre::LBT_COLOUR, Ogre::LBX_MODULATE, Ogre::LBS_TEXTURE,
              Ogre::LBS_CURRENT);
      Combine(unit.alpha, Ogre::LBT_ALPHA, Ogre::LBX_MODULATE, Ogre::LBS_TEXTURE,
              Ogre::LBS_CURRENT);
    }
    unit.present = true;
    unit.coord_set = st.coord_set;
    switch (st.texgen) {
      case TL_TEXGEN_PROJECTIVE: {
        // Texture world-view-projection = CLIPSPACE2DTOIMAGESPACE * frustum projection * frustum
        // view * host world, on object-space positions. The guest wants projector * world.
        Ogre::Frustum* f = b->projectors[u].get();
        f->setCustomProjectionMatrix(
            true, Ogre::Matrix4::CLIPSPACE2DTOIMAGESPACE.inverse() * ToMatrix(st.projector));
        Ogre::Matrix4 host_world = world_view ? ToMatrix(world_view) : Ogre::Matrix4::IDENTITY;
        f->setCustomViewMatrix(true, Ogre::Affine3(ToMatrix(st.world) * host_world.inverse()));
        unit.texgen = Ogre::TEXCALC_PROJECTIVE_TEXTURE;
        break;
      }
      case TL_TEXGEN_VIEW_NORMAL:
        unit.texgen = Ogre::TEXCALC_ENVIRONMENT_MAP_NORMAL;
        unit.has_matrix = st.has_matrix != 0;
        if (unit.has_matrix) unit.matrix = ToMatrix(st.matrix);
        break;
      case TL_TEXGEN_SPHERE: {
        float m[16];
        conv::SphereMapMatrixForHost(st.has_matrix ? st.matrix : nullptr, m);
        unit.texgen = Ogre::TEXCALC_ENVIRONMENT_MAP_NORMAL;
        unit.has_matrix = true;
        unit.matrix = ToMatrix(m);
        break;
      }
      case TL_TEXGEN_REFLECT: {
        if (!b->unit_cube[u]) {
          b->notes |= TL_NOTE_STAGE_APPROXIMATED;  // a 3D coordinate on a 2D texture
          break;
        }
        // Without the guest's view space the host world is the guest world (R = identity).
        if (!world_view) b->notes |= TL_NOTE_NO_VIEW_SPACE;
        float m[16];
        if (!conv::ReflectionMatrixForHost(world_view ? world_view : st.world, st.world,
                                           st.has_matrix ? st.matrix : nullptr, m)) {
          b->notes |= TL_NOTE_STAGE_APPROXIMATED;
          break;
        }
        unit.guest_texgen = UnitDesc::Guest::kReflect;
        unit.has_matrix = true;
        unit.matrix = ToMatrix(m);
        break;
      }
      default:
        unit.has_matrix = st.has_matrix != 0;
        if (unit.has_matrix) unit.matrix = ToMatrix(st.matrix);
        break;
    }
    count = u + 1;
  }
  // The RTSS's normal texgen samples cube maps only (on a 2D texture GuestTexgen computes it), and
  // its reflection differs from the guest's (GuestTexgen computes the guest's). Their coordinates
  // go into sets no other unit reads.
  for (uint32_t u = 0; u < count; ++u) {
    UnitDesc& unit = units[u];
    if (unit.present && unit.texgen == Ogre::TEXCALC_ENVIRONMENT_MAP_NORMAL && !b->unit_cube[u]) {
      unit.guest_texgen = UnitDesc::Guest::kViewNormal2d;
      unit.texgen = Ogre::TEXCALC_NONE;
      unit.has_matrix = true;  // identity when the guest has none
    }
  }
  uint32_t used_sets = 0;
  for (uint32_t u = 0; u < count; ++u) {
    if (units[u].present && units[u].guest_texgen == UnitDesc::Guest::kNone &&
        units[u].coord_set < 8)
      used_sets |= 1u << units[u].coord_set;
  }
  for (uint32_t u = 0; u < count; ++u) {
    UnitDesc& unit = units[u];
    if (!unit.present || unit.guest_texgen == UnitDesc::Guest::kNone) continue;
    int set = 7;
    while (set >= 0 && (used_sets & (1u << set))) --set;
    if (set < 0) {
      b->notes |= TL_NOTE_STAGE_APPROXIMATED;
      unit.present = false;
      continue;
    }
    used_sets |= 1u << set;
    unit.coord_set = uint32_t(set);
  }
  return count;
}

// RTSS extension: texture coordinates the backend computes as the guest does, written into the
// unit's output coordinate (content SPC_TEXTURE_COORDINATE0 + its reserved set) before FFPTexturing,
// which then takes it as already resolved. Per unit:
// - view normal on a 2D texture, as GL's normal-map texgen followed by the texture matrix:
//   uv = (texture matrix * (normalize(normal matrix * n), 1)).xy;
// - reflection (FFP_GenerateTexCoord_EnvMap_Reflect): r = reflect(normalize(view position), normal
//   matrix * n), the normal not normalised as in the guest, then texture matrix * (r, 1), the
//   matrix from ReflectionMatrixForHost.
class GuestTexgen : public Ogre::RTShader::SubRenderState {
 public:
  static const Ogre::String kType;
  struct Unit {
    uint32_t sampler, set;
    UnitDesc::Guest kind;
  };
  std::vector<Unit> units;

  const Ogre::String& getType() const override { return kType; }
  int getExecutionOrder() const override { return Ogre::RTShader::FFP_TEXTURING - 1; }
  void copyFrom(const SubRenderState& rhs) override {
    units = static_cast<const GuestTexgen&>(rhs).units;
  }
  bool resolveDependencies(Ogre::RTShader::ProgramSet* set) override {
    auto* vs = set->getCpuProgram(Ogre::GPT_VERTEX_PROGRAM);
    vs->addDependency("FFPLib_Transform");
    vs->addDependency("FFPLib_Texturing");
    return true;
  }
  bool addFunctionInvocations(Ogre::RTShader::ProgramSet* set) override {
    using namespace Ogre::RTShader;
    using Ogre::GpuProgramParameters;
    Program* vs = set->getCpuProgram(Ogre::GPT_VERTEX_PROGRAM);
    Function* main = vs->getMain();
    UniformParameterPtr normal_matrix = vs->resolveParameter(GpuProgramParameters::ACT_NORMAL_MATRIX);
    ParameterPtr normal = main->resolveInputParameter(Parameter::SPC_NORMAL_OBJECT_SPACE);
    FunctionStageRef stage = main->getStage(FFP_VS_TEXTURING);
    ParameterPtr view_normal, reflection;
    for (const Unit& u : units) {
      ParameterPtr coordinate;
      if (u.kind == UnitDesc::Guest::kViewNormal2d) {
        if (!view_normal) {
          view_normal = main->resolveLocalParameter(Ogre::GCT_FLOAT4, "tl_viewNormal");
          stage.assign(Ogre::Vector4(0, 0, 0, 1), view_normal);
          stage.callFunction("FFP_GenerateTexCoord_EnvMap_Normal", In(normal_matrix), In(normal),
                             Out(view_normal).xyz());
        }
        coordinate = view_normal;
      } else {
        if (!reflection) {
          UniformParameterPtr world_view =
              vs->resolveParameter(GpuProgramParameters::ACT_WORLDVIEW_MATRIX);
          ParameterPtr position = main->resolveInputParameter(Parameter::SPC_POSITION_OBJECT_SPACE);
          ParameterPtr eye = main->resolveLocalParameter(Ogre::GCT_FLOAT3, "tl_eye");
          ParameterPtr n = main->resolveLocalParameter(Ogre::GCT_FLOAT3, "tl_reflectNormal");
          reflection = main->resolveLocalParameter(Ogre::GCT_FLOAT3, "tl_reflect");
          stage.callFunction("FFP_Transform", In(world_view), In(position), Out(eye));
          stage.callBuiltin("normalize", In(eye), Out(eye));
          stage.callBuiltin("mul", In(normal_matrix), In(normal), Out(n));
          stage.callBuiltin("reflect", In(eye), In(n), Out(reflection));
        }
        coordinate = reflection;
      }
      bool cube = u.kind == UnitDesc::Guest::kReflect;
      UniformParameterPtr matrix =
          vs->resolveParameter(GpuProgramParameters::ACT_TEXTURE_MATRIX, u.sampler);
      ParameterPtr out = main->resolveOutputParameter(
          Parameter::Content(Parameter::SPC_TEXTURE_COORDINATE0 + u.set),
          cube ? Ogre::GCT_FLOAT3 : Ogre::GCT_FLOAT2);
      stage.callFunction("FFP_TransformTexCoord", In(matrix), In(coordinate), Out(out));
    }
    return true;
  }
};
const Ogre::String GuestTexgen::kType = "tl_guest_texgen";

// RTSS replacement of FFPFog with the guest's fog (Runic FFPLib FFP_PixelFog_Linear): per pixel
// from the clip-space w, colour = lerp(fog colour, colour, factor) and the alpha kept. The RTSS's
// own fog lerps the alpha towards the fog colour's too, which changes alpha-blended draws.
class GuestPixelFog : public Ogre::RTShader::SubRenderState {
 public:
  static const Ogre::String kType;

  const Ogre::String& getType() const override { return kType; }
  int getExecutionOrder() const override { return Ogre::RTShader::FFP_FOG; }
  void copyFrom(const SubRenderState&) override {}
  bool addFunctionInvocations(Ogre::RTShader::ProgramSet* set) override {
    using namespace Ogre::RTShader;
    Program* vs = set->getCpuProgram(Ogre::GPT_VERTEX_PROGRAM);
    Program* ps = set->getCpuProgram(Ogre::GPT_FRAGMENT_PROGRAM);
    Function* vs_main = vs->getMain();
    Function* ps_main = ps->getMain();
    ParameterPtr position = vs_main->resolveOutputParameter(Parameter::SPC_POSITION_PROJECTIVE_SPACE);
    ParameterPtr depth_out = vs_main->resolveOutputParameter(Parameter::SPC_DEPTH_VIEW_SPACE);
    vs_main->getStage(FFP_VS_FOG).assign(In(position).w(), depth_out);
    ParameterPtr depth = ps_main->resolveInputParameter(depth_out);
    UniformParameterPtr params = ps->resolveParameter(Ogre::GpuProgramParameters::ACT_FOG_PARAMS);
    UniformParameterPtr colour = ps->resolveParameter(Ogre::GpuProgramParameters::ACT_FOG_COLOUR);
    ParameterPtr out = ps_main->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
    ParameterPtr factor = ps_main->resolveLocalParameter(Ogre::GCT_FLOAT1, "tl_fogFactor");
    FunctionStageRef stage = ps_main->getStage(FFP_PS_FOG);
    // factor = saturate((end - |w|) * 1 / (end - start))
    stage.callBuiltin("abs", In(depth), Out(factor));
    stage.sub(In(params).z(), In(factor), Out(factor));
    stage.mul(In(factor), In(params).w(), Out(factor));
    stage.callBuiltin("saturate", In(factor), Out(factor));
    stage.callBuiltin("mix", In(colour).xyz(), In(out).xyz(), In(factor), Out(out).xyz());
    return true;
  }
};
const Ogre::String GuestPixelFog::kType = "tl_guest_pixel_fog";

class GuestPixelFogFactory : public Ogre::RTShader::SubRenderStateFactory {
 public:
  const Ogre::String& getType() const override { return GuestPixelFog::kType; }

 protected:
  Ogre::RTShader::SubRenderState* createInstanceImpl() override { return OGRE_NEW GuestPixelFog(); }
};

// RTSS alpha test (FFPLib_AlphaTest FFP_Alpha_Test): the comparison is a uniform with the values
// of Ogre::CompareFunction (kAlphaFunctionUniform, written per draw) and the reference comes from
// the pass (surface alpha rejection value), so one program serves every alpha test. The guest's
// alpha test is render state (D3D); GL3+ has no fixed-function alpha test, and the RTSS's own reads
// its function from a per-renderable update the backend does not run. Last in the fragment
// program, after fog.
constexpr const char* kAlphaFunctionUniform = "tl_alpha_function";
class GuestAlphaTest : public Ogre::RTShader::SubRenderState {
 public:
  static const Ogre::String kType;

  const Ogre::String& getType() const override { return kType; }
  int getExecutionOrder() const override { return Ogre::RTShader::FFP_ALPHA_TEST; }
  void copyFrom(const SubRenderState&) override {}
  bool resolveDependencies(Ogre::RTShader::ProgramSet* set) override {
    set->getCpuProgram(Ogre::GPT_FRAGMENT_PROGRAM)->addDependency("FFPLib_AlphaTest");
    return true;
  }
  bool addFunctionInvocations(Ogre::RTShader::ProgramSet* set) override {
    using namespace Ogre::RTShader;
    Program* ps = set->getCpuProgram(Ogre::GPT_FRAGMENT_PROGRAM);
    Function* main = ps->getMain();
    UniformParameterPtr reference =
        ps->resolveParameter(Ogre::GpuProgramParameters::ACT_SURFACE_ALPHA_REJECTION_VALUE);
    UniformParameterPtr function = ps->resolveParameter(Ogre::GCT_FLOAT1, kAlphaFunctionUniform);
    ParameterPtr out = main->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
    main->getStage(FFP_PS_POST_PROCESS + 1)
        .callFunction("FFP_Alpha_Test", {In(function), In(reference), In(out)});
    return true;
  }
};
const Ogre::String GuestAlphaTest::kType = "tl_guest_alpha_test";

class GuestAlphaTestFactory : public Ogre::RTShader::SubRenderStateFactory {
 public:
  const Ogre::String& getType() const override { return GuestAlphaTest::kType; }

 protected:
  Ogre::RTShader::SubRenderState* createInstanceImpl() override { return OGRE_NEW GuestAlphaTest(); }
};

// RTSS replacement of FFPLighting with the guest's (Runic FFPLib FFP_Light_Directional_Diffuse):
// colour = derived scene colour + light diffuse [* vertex colour when the diffuse tracks it] *
// saturate(dot(normalize(normal matrix * n), direction towards the light)), not clamped as a whole
// (the RTSS saturates the lit colour; the guest's programs multiply it into the texture stages
// unclamped). At most one directional light (tl_lighting).
class GuestLighting : public Ogre::RTShader::SubRenderState {
 public:
  static const Ogre::String kType;
  bool light = false, track_vertex_colour = false;

  const Ogre::String& getType() const override { return kType; }
  int getExecutionOrder() const override { return Ogre::RTShader::FFP_LIGHTING; }
  void copyFrom(const SubRenderState& rhs) override {
    const auto& r = static_cast<const GuestLighting&>(rhs);
    light = r.light;
    track_vertex_colour = r.track_vertex_colour;
  }
  bool addFunctionInvocations(Ogre::RTShader::ProgramSet* set) override {
    using namespace Ogre::RTShader;
    using Ogre::GpuProgramParameters;
    Program* vs = set->getCpuProgram(Ogre::GPT_VERTEX_PROGRAM);
    Function* main = vs->getMain();
    UniformParameterPtr scene = vs->resolveParameter(GpuProgramParameters::ACT_DERIVED_SCENE_COLOUR);
    ParameterPtr out = main->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
    FunctionStageRef stage = main->getStage(FFP_VS_LIGHTING);
    stage.assign(In(scene), Out(out));
    // The fragment program reads this colour (the RTSS links it only for its own FFPLighting,
    // fixupFFPLighting): the colour stage starts from it instead of FFPColour's constant white.
    Function* ps_main = set->getCpuProgram(Ogre::GPT_FRAGMENT_PROGRAM)->getMain();
    ParameterPtr ps_in = ps_main->resolveInputParameter(out);
    ParameterPtr ps_out = ps_main->resolveOutputParameter(Parameter::SPC_COLOR_DIFFUSE);
    ps_main->getStage(FFP_PS_COLOUR_BEGIN + 1).assign(In(ps_in), Out(ps_out));
    if (!light) return true;
    UniformParameterPtr normal_matrix = vs->resolveParameter(GpuProgramParameters::ACT_NORMAL_MATRIX);
    UniformParameterPtr towards =
        vs->resolveParameter(GpuProgramParameters::ACT_LIGHT_POSITION_VIEW_SPACE, 0);
    UniformParameterPtr diffuse =
        vs->resolveParameter(GpuProgramParameters::ACT_LIGHT_DIFFUSE_COLOUR, 0);
    ParameterPtr normal = main->resolveInputParameter(Parameter::SPC_NORMAL_OBJECT_SPACE);
    ParameterPtr n = main->resolveLocalParameter(Ogre::GCT_FLOAT3, "tl_lightNormal");
    ParameterPtr ndotl = main->resolveLocalParameter(Ogre::GCT_FLOAT1, "tl_nDotL");
    ParameterPtr colour = main->resolveLocalParameter(Ogre::GCT_FLOAT3, "tl_lightColour");
    stage.callBuiltin("mul", In(normal_matrix), In(normal), Out(n));
    stage.callBuiltin("normalize", In(n), Out(n));
    stage.callBuiltin("dot", In(n), In(towards).xyz(), Out(ndotl));
    stage.callBuiltin("saturate", In(ndotl), Out(ndotl));
    stage.assign(In(diffuse).xyz(), Out(colour));
    if (track_vertex_colour) {
      ParameterPtr vertex_colour = main->resolveInputParameter(Parameter::SPC_COLOR_DIFFUSE);
      stage.mul(In(colour), In(vertex_colour).xyz(), Out(colour));
    }
    stage.mul(In(colour), In(ndotl), Out(colour));
    stage.add(In(out).xyz(), In(colour), Out(out).xyz());
    return true;
  }
};
const Ogre::String GuestLighting::kType = "tl_guest_lighting";

class GuestLightingFactory : public Ogre::RTShader::SubRenderStateFactory {
 public:
  const Ogre::String& getType() const override { return GuestLighting::kType; }

 protected:
  Ogre::RTShader::SubRenderState* createInstanceImpl() override { return OGRE_NEW GuestLighting(); }
};


class GuestTexgenFactory : public Ogre::RTShader::SubRenderStateFactory {
 public:
  const Ogre::String& getType() const override { return GuestTexgen::kType; }

 protected:
  Ogre::RTShader::SubRenderState* createInstanceImpl() override { return OGRE_NEW GuestTexgen(); }
};

// ---- RTSS programs ----------------------------------------------------------------------------
//
// The RTSS generates a vertex and a fragment program from a Pass. Everything that changes the
// generated code is in the key; everything else (matrices, colours, fog range, texture matrices)
// is an auto constant, written per draw. Stage constants (LBS_MANUAL) are compiled into the
// fragment program by the RTSS, so they are part of the key.

constexpr const char* kProgramScheme = "tl_rtss";
// Vertex program constant registers kept for everything but a skinning palette (matrices, light,
// fog, texture matrices).
constexpr size_t kProgramRegisterReserve = 64;

#pragma pack(push, 1)
struct UnitKey {
  uint8_t present, cube, texgen, coord_set, has_matrix, guest_texgen;
  uint8_t colour_op, colour_source1, colour_source2, alpha_op, alpha_source1, alpha_source2;
  float colour_arg1[4], colour_arg2[4], alpha_arg1, alpha_arg2;
};
struct ProgramKey {
  uint8_t lighting, tracking, light_count, fog, unit_count, skin_weights;
  uint16_t skin_bones;
  UnitKey units[TL_MAX_STAGES];
};
#pragma pack(pop)

std::string MakeProgramKey(const tl_backend* b, const ProgramInputs& f, const UnitDesc* units,
                           uint32_t unit_count) {
  ProgramKey k;
  std::memset(&k, 0, sizeof(k));
  k.lighting = f.lighting;
  k.tracking = uint8_t(f.tracking);
  k.light_count = f.lighting && f.light ? 1 : 0;
  k.fog = f.fog;
  k.unit_count = uint8_t(unit_count);
  k.skin_bones = uint16_t(f.skin_bones);
  k.skin_weights = uint8_t(f.skin_weights);
  for (uint32_t u = 0; u < unit_count; ++u) {
    const UnitDesc& d = units[u];
    UnitKey& uk = k.units[u];
    uk.present = d.present;
    if (!d.present) continue;
    uk.cube = b->unit_cube[u];
    uk.texgen = uint8_t(d.texgen);
    uk.coord_set = uint8_t(d.coord_set);
    uk.has_matrix = d.has_matrix;
    uk.guest_texgen = uint8_t(d.guest_texgen);
    uk.colour_op = uint8_t(d.colour.operation);
    uk.colour_source1 = uint8_t(d.colour.source1);
    uk.colour_source2 = uint8_t(d.colour.source2);
    uk.alpha_op = uint8_t(d.alpha.operation);
    uk.alpha_source1 = uint8_t(d.alpha.source1);
    uk.alpha_source2 = uint8_t(d.alpha.source2);
    if (d.colour.source1 == Ogre::LBS_MANUAL || d.colour.source2 == Ogre::LBS_MANUAL) {
      const Ogre::ColourValue* args[2] = {&d.colour.colourArg1, &d.colour.colourArg2};
      for (int i = 0; i < 4; ++i) {
        uk.colour_arg1[i] = args[0]->ptr()[i];
        uk.colour_arg2[i] = args[1]->ptr()[i];
      }
    }
    if (d.alpha.source1 == Ogre::LBS_MANUAL || d.alpha.source2 == Ogre::LBS_MANUAL) {
      uk.alpha_arg1 = d.alpha.alphaArg1;
      uk.alpha_arg2 = d.alpha.alphaArg2;
    }
  }
  return std::string(reinterpret_cast<const char*>(&k), sizeof(k));
}

// Builds the source Pass for a key and lets the RTSS generate (and compile) its programs.
void GenerateProgram(tl_backend* b, const ProgramInputs& f, const UnitDesc* units,
                     uint32_t unit_count, tl_backend::Program& p) {
  auto& sg = Ogre::RTShader::ShaderGenerator::getSingleton();
  Ogre::MaterialPtr mat = Ogre::MaterialManager::getSingleton().create(
      "tl_rtss#" + std::to_string(++b->program_serial),
      Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
  Ogre::Pass* src = mat->getTechnique(0)->getPass(0);
  src->setLightingEnabled(f.lighting);
  src->setVertexColourTracking(f.tracking);
  src->setFog(true, f.fog ? Ogre::FOG_LINEAR : Ogre::FOG_NONE);
  for (uint32_t u = 0; u < unit_count; ++u) {
    const UnitDesc& d = units[u];
    Ogre::TextureUnitState* tus = src->createTextureUnitState();
    tus->setTexture(d.present && b->unit_cube[u] ? b->placeholder_cube : b->placeholder_2d);
    if (!d.present) {
      // A gap below a used unit: the program samples it but passes the previous result through.
      tus->setColourOperationEx(Ogre::LBX_SOURCE2, Ogre::LBS_TEXTURE, Ogre::LBS_CURRENT);
      tus->setAlphaOperation(Ogre::LBX_SOURCE2, Ogre::LBS_TEXTURE, Ogre::LBS_CURRENT);
      continue;
    }
    tus->setTextureCoordSet(uint8_t(d.coord_set));
    tus->setColourOperationEx(d.colour.operation, d.colour.source1, d.colour.source2,
                              d.colour.colourArg1, d.colour.colourArg2, d.colour.factor);
    tus->setAlphaOperation(d.alpha.operation, d.alpha.source1, d.alpha.source2, d.alpha.alphaArg1,
                           d.alpha.alphaArg2, d.alpha.factor);
    if (d.texgen == Ogre::TEXCALC_ENVIRONMENT_MAP_NORMAL) {
      tus->setEnvironmentMap(true, Ogre::TextureUnitState::ENV_NORMAL);
    } else if (d.texgen == Ogre::TEXCALC_PROJECTIVE_TEXTURE) {
      tus->setProjectiveTexturing(true, b->projectors[u].get());
    }
    // The RTSS adds a texture matrix only for a non-identity transform; the real one is written
    // per draw (ApplyProgram). GuestTexgen applies its own.
    if (d.has_matrix && d.guest_texgen == UnitDesc::Guest::kNone)
      tus->setTextureTransform(Ogre::Matrix4(Ogre::Affine3::getScale(2, 2, 2)));
  }
  p.material = mat;
  if (!sg.createShaderBasedTechnique(mat->getTechnique(0), kProgramScheme)) {
    p.failed = true;
    return;
  }
  Ogre::RTShader::RenderState* state = sg.getRenderState(kProgramScheme, *mat, 0);
  state->setLightCount(f.lighting && f.light ? 1 : 0);
  state->setLightCountAutoUpdate(false);
  if (f.lighting) {
    auto* lighting = static_cast<GuestLighting*>(sg.createSubRenderState(GuestLighting::kType));
    lighting->light = f.light;
    lighting->track_vertex_colour = f.tracking == Ogre::TVC_DIFFUSE;
    state->addTemplateSubRenderState(lighting);
  }
  if (f.fog) state->addTemplateSubRenderState(sg.createSubRenderState(GuestPixelFog::kType));
  state->addTemplateSubRenderState(sg.createSubRenderState(GuestAlphaTest::kType));
  if (f.skin_bones) {
    // Linear blend of world-space bones given as object-space ones (setBonesUseObjectSpace): the
    // blended position is the guest's world-space one and the rest of the program takes it as the
    // object-space position under the host world, as Runic writes it back into its position input.
    Ogre::RTShader::SubRenderState* skin =
        sg.createSubRenderState(Ogre::RTShader::SRS_HARDWARE_SKINNING);
    skin->setParameter("type", "linear");
    skin->setParameter("max_bone_count", std::to_string(f.skin_bones));
    skin->setParameter("weight_count", std::to_string(f.skin_weights));
    state->addTemplateSubRenderState(skin);
  }
  std::vector<GuestTexgen::Unit> guest_units;
  for (uint32_t u = 0; u < unit_count; ++u) {
    if (units[u].present && units[u].guest_texgen != UnitDesc::Guest::kNone)
      guest_units.push_back({u, units[u].coord_set, units[u].guest_texgen});
  }
  if (!guest_units.empty()) {
    auto* texgen = static_cast<GuestTexgen*>(sg.createSubRenderState(GuestTexgen::kType));
    texgen->units = guest_units;
    state->addTemplateSubRenderState(texgen);
  }
  sg.validateMaterial(kProgramScheme, *mat);
  mat->load();
  for (Ogre::Technique* t : mat->getTechniques()) {
    if (t->getSchemeName() != kProgramScheme || t->getNumPasses() == 0) continue;
    Ogre::Pass* pass = t->getPass(0);
    if (!pass->hasVertexProgram() || !pass->hasFragmentProgram()) break;
    p.pass = pass;
    p.vertex_params = pass->getVertexProgramParameters();
    p.fragment_params = pass->getFragmentProgramParameters();
    // The RTSS appends an index to a resolved uniform's name.
    for (const auto& [name, def] : p.fragment_params->getConstantDefinitions().map) {
      if (name.rfind(kAlphaFunctionUniform, 0) == 0) p.alpha_function = name;
    }
    if (p.alpha_function.empty()) break;
    return;
  }
  p.failed = true;
}

// Binds the draw's program and writes its auto constants. Returns false when the RTSS could not
// generate it.
bool ApplyProgram(tl_backend* b, const ProgramInputs& f, const UnitDesc* units,
                  uint32_t unit_count) {
  tl_backend::Program& p = b->programs[MakeProgramKey(b, f, units, unit_count)];
  if (!p.pass && !p.failed) {
    ++b->counters.programs_generated;
    const auto start = std::chrono::steady_clock::now();
    try {
      GenerateProgram(b, f, units, unit_count, p);
    } catch (Ogre::Exception& e) {
      Ogre::LogManager::getSingleton().logError("RTSS program generation: " + e.getFullDescription());
      p.failed = true;
    }
    b->counters.program_generate_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  if (p.failed) return false;
  Ogre::RenderSystem* rs = b->rs;
  if (b->bound_program != &p) {
    rs->bindGpuProgram(p.pass->getVertexProgram()->_getBindingDelegate());
    rs->bindGpuProgram(p.pass->getFragmentProgram()->_getBindingDelegate());
    b->bound_program = &p;
  }
  // Values the data source reads from the pass and the scene objects.
  p.pass->setDiffuse(f.diffuse);
  p.pass->setAlphaRejectValue(f.alpha_reference);
  p.fragment_params->setNamedConstant(p.alpha_function, float(f.alpha_function));
  p.pass->setSelfIllumination(f.emissive);
  for (uint32_t u = 0; u < unit_count && u < p.pass->getNumTextureUnitStates(); ++u) {
    if (units[u].has_matrix) p.pass->getTextureUnitState(u)->setTextureTransform(units[u].matrix);
  }
  Ogre::AutoParamDataSource& source = *b->params_source;
  Ogre::LightList lights;
  if (f.lighting && f.light) {
    // Directional, in view space (the host view is the identity).
    float direction[3];
    conv::DirectionalLightForHost(f.towards_light, direction);
    b->light_node->setOrientation(Ogre::Vector3::NEGATIVE_UNIT_Z.getRotationTo(
        Ogre::Vector3(direction[0], direction[1], direction[2]).normalisedCopy()));
    b->light->setDiffuseColour(f.light_diffuse);
    lights.push_back(b->light);
  }
  b->camera->setCustomProjectionMatrix(true, f.projection);
  // The host world, then (skinned) the guest's bones (object-space bones: array[0] is the world).
  std::vector<Ogre::Affine3>& world = b->world_matrices;
  world.assign(1, Ogre::Affine3(f.world));
  for (uint32_t i = 0; i < f.skin_bones && f.skin_palette; ++i) {
    const float* m = f.skin_palette + 12 * i;
    world.push_back(Ogre::Affine3(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9],
                                  m[10], m[11]));
  }
  source.setCurrentRenderable(nullptr);
  source.setWorldMatrices(world.data(), world.size());
  source.setCurrentCamera(b->camera, false);
  source.setCurrentRenderTarget(b->active);
  source.setCurrentViewport(b->current->viewport);
  source.setCurrentLightList(&lights);
  source.setAmbientLightColour(Ogre::ColourValue::Black);
  source.setFog(f.fog ? Ogre::FOG_LINEAR : Ogre::FOG_NONE, f.fog_colour, 0, f.fog_start, f.fog_end);
  source.setCurrentPass(p.pass);
  for (uint32_t u = 0; u < unit_count; ++u) {
    source.setTextureProjector(
        units[u].texgen == Ogre::TEXCALC_PROJECTIVE_TEXTURE ? b->projectors[u].get() : nullptr, u);
  }
  p.vertex_params->_updateAutoParams(&source, Ogre::GPV_ALL);
  // Every RTSS vertex program transforms the position with the world-view-projection constant
  // (gl_Position = worldviewproj_matrix * vertex, skinned ones after blending in object space):
  // it gets the guest's matrix itself, with the render system's conversion and render-target
  // flip the data source applies to the projection (AutoParamDataSource::getProjectionMatrix).
  Ogre::Matrix4 clip;
  rs->_convertProjectionMatrix(f.clip, clip, true);
  if (b->active && b->active->requiresTextureFlipping()) {
    for (int c = 0; c < 4; ++c) clip[1][c] = -clip[1][c];
  }
  for (const auto& entry : p.vertex_params->getAutoConstantList()) {
    if (entry.paramType == Ogre::GpuProgramParameters::ACT_WORLDVIEWPROJ_MATRIX) {
      p.vertex_params->_writeRawConstant(entry.physicalIndex, clip, entry.elementCount);
    }
  }
  p.fragment_params->_updateAutoParams(&source, Ogre::GPV_ALL);
  rs->bindGpuProgramParameters(Ogre::GPT_VERTEX_PROGRAM, p.vertex_params, Ogre::GPV_ALL);
  rs->bindGpuProgramParameters(Ogre::GPT_FRAGMENT_PROGRAM, p.fragment_params, Ogre::GPV_ALL);
  return true;
}

void UnbindProgram(tl_backend* b) {
  if (!b->bound_program) return;
  b->rs->unbindGpuProgram(Ogre::GPT_FRAGMENT_PROGRAM);
  b->rs->unbindGpuProgram(Ogre::GPT_VERTEX_PROGRAM);
  b->bound_program = nullptr;
}

// ---- full-screen passes (display gamma, present) ---------------------------------------------
//
// Two small programs of the backend's own, created through OGRE's HighLevelGpuProgramManager:
// the display gamma (the Xbox ramp is a per-channel table lookup on the final colour,
// xbox_to_gl_conventions GammaRampLut) in one full-screen pass from the main target to the output
// target, and the copy of the output to the window. Both share a pass-through vertex program.
//
// The backend's programs are written once with OGRE's unified shader macros (OgreUnifiedShader.h,
// in OGRE's Main media) and compiled as GLSL or HLSL, whichever the render system takes
// (MakeProgram). Samplers are bound by register (SAMPLER2D's second argument).

const char* kQuadVertexSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
MAIN_PARAMETERS
IN(vec4 vertex, POSITION)
IN(vec4 uv0, TEXCOORD0)
OUT(vec2 uv, TEXCOORD0)
MAIN_DECLARATION
{
  gl_Position = vec4(vertex.xy, 0.0, 1.0);
  uv = uv0.xy;
}
)";

const char* kGammaFragmentSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
SAMPLER2D(scene, 0);
SAMPLER2D(lut, 1);
MAIN_PARAMETERS
IN(vec2 uv, TEXCOORD0)
MAIN_DECLARATION
{
  vec3 c = texture2D(scene, uv).rgb;
  vec3 u = (floor(c * 255.0 + 0.5) + 0.5) / 256.0;
  gl_FragColor = vec4(texture2D(lut, vec2(u.r, 0.5)).r, texture2D(lut, vec2(u.g, 0.5)).g,
                      texture2D(lut, vec2(u.b, 0.5)).b, 1.0);
}
)";

const char* kCopyFragmentSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
SAMPLER2D(image, 0);
MAIN_PARAMETERS
IN(vec2 uv, TEXCOORD0)
MAIN_DECLARATION
{
  gl_FragColor = texture2D(image, uv);
}
)";

// A unified-macro program in the language the render system takes: GLSL (GL3+) or HLSL
// (Direct3D 11, shader model 4).
Ogre::HighLevelGpuProgramPtr MakeProgram(const char* name, Ogre::GpuProgramType type,
                                          const char* source) {
  auto& programs = Ogre::HighLevelGpuProgramManager::getSingleton();
  const bool glsl = programs.isLanguageSupported("glsl");
  auto program =
      programs.createProgram(name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
                             glsl ? "glsl" : "hlsl", type);
  if (!glsl) {
    program->setParameter("entry_point", "main");
    program->setParameter("target", type == Ogre::GPT_VERTEX_PROGRAM ? "vs_4_0" : "ps_4_0");
  }
  program->setSource(source);
  program->load();
  return program;
}

// Parameters of a backend program: a sampler bound by register has no named constant in HLSL, so
// the names the GLSL side has are set where they exist.
Ogre::GpuProgramParametersSharedPtr MakeParameters(const Ogre::HighLevelGpuProgramPtr& program) {
  Ogre::GpuProgramParametersSharedPtr params = program->createParameters();
  params->setIgnoreMissingParams(true);
  return params;
}

void UploadGammaLut(tl_backend* b, const uint8_t lut[256 * 3]) {
  std::vector<uint8_t> rgba(256 * 4);
  for (int v = 0; v < 256; ++v) {
    rgba[v * 4 + 0] = lut[v * 3 + 0];
    rgba[v * 4 + 1] = lut[v * 3 + 1];
    rgba[v * 4 + 2] = lut[v * 3 + 2];
    rgba[v * 4 + 3] = 255;
  }
  Ogre::PixelBox box(256, 1, 1, Ogre::PF_BYTE_RGBA, rgba.data());
  b->gamma_lut->getBuffer()->blitFromMemory(box);
}

void CreateDisplayGamma(tl_backend* b) {
  b->gamma_lut = Ogre::TextureManager::getSingleton().createManual(
      "tl_gamma_lut", Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_2D,
      256, 1, 0, Ogre::PF_A8R8G8B8, Ogre::TU_DEFAULT);
  uint8_t lut[256 * 3];
  for (int v = 0; v < 256; ++v) lut[v * 3] = lut[v * 3 + 1] = lut[v * 3 + 2] = uint8_t(v);
  UploadGammaLut(b, lut);
  b->quad_vertex_program = MakeProgram("tl_quad_vs", Ogre::GPT_VERTEX_PROGRAM, kQuadVertexSource);
  b->gamma_program = MakeProgram("tl_display_gamma", Ogre::GPT_FRAGMENT_PROGRAM,
                                 kGammaFragmentSource);
  b->gamma_params = MakeParameters(b->gamma_program);
  b->gamma_params->setNamedConstant("scene", 0);
  b->gamma_params->setNamedConstant("lut", 1);
  b->copy_program = MakeProgram("tl_copy", Ogre::GPT_FRAGMENT_PROGRAM, kCopyFragmentSource);
  b->copy_params = MakeParameters(b->copy_program);
  b->copy_params->setNamedConstant("image", 0);
  auto& buffers = Ogre::HardwareBufferManager::getSingleton();
  // Full-screen quad between render textures: position xy (clip space), uv, keeping memory order
  // on the render system's render textures (conventions RenderTextureCopyQuad).
  float quad[16];
  conv::RenderTextureCopyQuad(b->output.rt->requiresTextureFlipping(), quad);
  b->quad = buffers.createVertexBuffer(4 * sizeof(float), 4, Ogre::HBU_GPU_ONLY);
  b->quad->writeData(0, sizeof(quad), quad, true);
  // Window quad: clip y = +1 (top) samples the output's first row.
  const float window_quad[] = {-1, 1, 0, 0, 1, 1, 1, 0, -1, -1, 0, 1, 1, -1, 1, 1};
  b->window_quad = buffers.createVertexBuffer(4 * sizeof(float), 4, Ogre::HBU_GPU_ONLY);
  b->window_quad->writeData(0, sizeof(window_quad), window_quad, true);
}

// Common state of the full-screen passes (gamma, present): `fragment` with the quad vertex
// program, no blending, depth or culling.
void FullScreenPassState(tl_backend* b, const Ogre::HighLevelGpuProgramPtr& fragment,
                         const Ogre::GpuProgramParametersSharedPtr& params) {
  Ogre::RenderSystem* rs = b->rs;
  UnbindProgram(b);
  SetColourBlend(b, Ogre::SBF_ONE, Ogre::SBF_ZERO, 0xF);
  rs->_setDepthBufferParams(false, false);
  rs->_setCullingMode(Ogre::CULL_NONE);
  rs->_setPolygonMode(Ogre::PM_SOLID);
  rs->bindGpuProgram(b->quad_vertex_program->_getBindingDelegate());
  rs->bindGpuProgram(fragment->_getBindingDelegate());
  rs->bindGpuProgramParameters(Ogre::GPT_FRAGMENT_PROGRAM, params, Ogre::GPV_ALL);
}

void EndFullScreenPass(tl_backend* b) {
  b->rs->unbindGpuProgram(Ogre::GPT_FRAGMENT_PROGRAM);
  b->rs->unbindGpuProgram(Ogre::GPT_VERTEX_PROGRAM);
  b->rs->_disableTextureUnitsFrom(0);
  b->enabled_units = 0;  // the next frame binds its own units
}

void DrawQuad(tl_backend* b, const Ogre::HardwareVertexBufferSharedPtr& quad) {
  Ogre::VertexData vertex_data;
  vertex_data.vertexDeclaration->addElement(0, 0, Ogre::VET_FLOAT2, Ogre::VES_POSITION);
  vertex_data.vertexDeclaration->addElement(0, 2 * sizeof(float), Ogre::VET_FLOAT2,
                                            Ogre::VES_TEXTURE_COORDINATES, 0);
  vertex_data.vertexBufferBinding->setBinding(0, quad);
  vertex_data.vertexStart = 0;
  vertex_data.vertexCount = 4;
  Ogre::RenderOperation op;
  op.operationType = Ogre::RenderOperation::OT_TRIANGLE_STRIP;
  op.vertexData = &vertex_data;
  op.useIndexes = false;
  b->rs->_render(op);
}

// ---- clears ------------------------------------------------------------------------------------
//
// The guest's clears (D3D) take the current viewport's area. GL3+ does the same (it scissors to
// the viewport); Direct3D 11 clears the whole target. MeasureClearArea finds out which once, by
// clearing half of a render texture and reading the other half; where the whole target is
// cleared, a clear of part of it draws a quad of the viewport's size instead, writing the clear
// colour and depth. The stencil is not cleared there: the backend never tests it.

const char* kClearVertexSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
OGRE_UNIFORMS(
uniform vec4 depth; // x: the clip z that lands on the clear depth
)
MAIN_PARAMETERS
IN(vec4 vertex, POSITION)
MAIN_DECLARATION
{
  gl_Position = vec4(vertex.xy, depth.x, 1.0);
}
)";

const char* kClearFragmentSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
OGRE_UNIFORMS(
uniform vec4 colour;
)
MAIN_PARAMETERS
MAIN_DECLARATION
{
  gl_FragColor = colour;
}
)";

// Whether the render system's clearFrameBuffer clears the whole target rather than the viewport:
// the output target cleared black, then its left half white, and a pixel of its right half read.
bool MeasureClearArea(tl_backend* b) {
  Ogre::Viewport* viewport = b->output.viewport;
  viewport->setDimensions(0, 0, 1, 1);
  SetViewport(b, viewport);
  b->rs->clearFrameBuffer(Ogre::FBT_COLOUR, Ogre::ColourValue::Black);
  viewport->setDimensions(0, 0, 0.5f, 1);
  SetViewport(b, viewport);
  b->rs->clearFrameBuffer(Ogre::FBT_COLOUR, Ogre::ColourValue::White);
  viewport->setDimensions(0, 0, 1, 1);
  SetViewport(b, viewport);
  const uint32_t x = b->output.rt->getWidth() * 3 / 4, y = b->output.rt->getHeight() / 2;
  uint8_t pixel[4] = {};
  Ogre::PixelBox box(1, 1, 1, Ogre::PF_BYTE_RGBA, pixel);
  b->output.rt->copyContentsToMemory(Ogre::Box(x, y, x + 1, y + 1), box,
                                     Ogre::RenderTarget::FB_AUTO);
  return pixel[0] > 127;
}

void CreateClear(tl_backend* b) {
  b->clear_vertex_program =
      MakeProgram("tl_clear_vs", Ogre::GPT_VERTEX_PROGRAM, kClearVertexSource);
  b->clear_fragment_program =
      MakeProgram("tl_clear_fs", Ogre::GPT_FRAGMENT_PROGRAM, kClearFragmentSource);
  b->clear_vertex_params = MakeParameters(b->clear_vertex_program);
  b->clear_fragment_params = MakeParameters(b->clear_fragment_program);
  b->clears_whole_target = MeasureClearArea(b);
}

// A clear of the current viewport's area drawn as a quad (no blending, depth always passes, cull
// off).
void ClearViewportByQuad(tl_backend* b, unsigned int buffers, const Ogre::ColourValue& colour,
                         float depth) {
  Ogre::RenderSystem* rs = b->rs;
  UnbindProgram(b);
  SetColourBlend(b, Ogre::SBF_ONE, Ogre::SBF_ZERO, (buffers & Ogre::FBT_COLOUR) ? 0xF : 0x0);
  rs->_setDepthBufferParams(true, (buffers & Ogre::FBT_DEPTH) != 0, Ogre::CMPF_ALWAYS_PASS);
  rs->_setDepthBias(0, 0);
  rs->_setCullingMode(Ogre::CULL_NONE);
  rs->_setPolygonMode(Ogre::PM_SOLID);
  // The clip z that lands on `depth`: z = 2 * depth - 1 in GL's form, through the render
  // system's conversion as every clip matrix is (conventions, host render systems).
  Ogre::Matrix4 clip_z = Ogre::Matrix4::IDENTITY;
  clip_z[2][2] = 0;
  clip_z[2][3] = 2 * depth - 1;
  Ogre::Matrix4 host_z;
  rs->_convertProjectionMatrix(clip_z, host_z, true);
  b->clear_vertex_params->setNamedConstant("depth", Ogre::Vector4(host_z[2][3], 0, 0, 0));
  b->clear_fragment_params->setNamedConstant("colour", colour);
  rs->bindGpuProgram(b->clear_vertex_program->_getBindingDelegate());
  rs->bindGpuProgram(b->clear_fragment_program->_getBindingDelegate());
  rs->bindGpuProgramParameters(Ogre::GPT_VERTEX_PROGRAM, b->clear_vertex_params, Ogre::GPV_ALL);
  rs->bindGpuProgramParameters(Ogre::GPT_FRAGMENT_PROGRAM, b->clear_fragment_params,
                               Ogre::GPV_ALL);
  rs->_disableTextureUnitsFrom(0);
  DrawQuad(b, b->quad);
  EndFullScreenPass(b);
}

// Clears the current viewport's area of the current target.
void ClearViewport(tl_backend* b, unsigned int buffers, const Ogre::ColourValue& colour,
                   float depth, uint16_t stencil) {
  Ogre::Viewport* viewport = b->current->viewport;
  const bool whole = viewport->getActualLeft() == 0 && viewport->getActualTop() == 0 &&
                     viewport->getActualWidth() == int(viewport->getTarget()->getWidth()) &&
                     viewport->getActualHeight() == int(viewport->getTarget()->getHeight());
  if (whole || !b->clears_whole_target) {
    b->rs->clearFrameBuffer(buffers, colour, depth, stencil);
  } else {
    ClearViewportByQuad(b, buffers, colour, depth);
  }
}

void DisplayGammaPass(tl_backend* b) {
  Ogre::RenderSystem* rs = b->rs;
  b->output.viewport->setDimensions(0, 0, 1, 1);
  SetViewport(b, b->output.viewport);
  FullScreenPassState(b, b->gamma_program, b->gamma_params);
  Ogre::TexturePtr inputs[2] = {b->main.texture, b->gamma_lut};
  Ogre::SamplerPtr point = GetSampler(b, Ogre::FO_POINT, Ogre::FO_POINT, Ogre::FO_NONE,
                                      Ogre::TAM_CLAMP, Ogre::TAM_CLAMP, Ogre::TAM_CLAMP, 1);
  for (size_t u = 0; u < 2; ++u) {
    rs->_setTexture(u, true, inputs[u]);
    rs->_setSampler(u, *point);
  }
  rs->_disableTextureUnitsFrom(2);
  DrawQuad(b, b->quad);
  EndFullScreenPass(b);
}

// ---- host UI pass ------------------------------------------------------------------------------
//
// The runtime's ImGui dialogs (tl_backend_set_ui_frame), drawn last over the whole target in their
// own coordinate space: x right and y down from the top left, scaled to the target's pixels. No
// guest conventions apply; OGRE's own do: a render texture that requires texture flipping (the
// output, offscreen) has its top row at clip y = -1, so y is negated there as OGRE does with
// projections (AutoParamDataSource::getProjectionMatrix), and setScissorTest takes the rectangle
// from the top on every target.

const char* kUiVertexSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
OGRE_UNIFORMS(
uniform vec4 transform; // clip = position * transform.xy + transform.zw
)
MAIN_PARAMETERS
IN(vec4 vertex, POSITION)
IN(vec4 uv0, TEXCOORD0)
IN(vec4 colour, COLOR)
OUT(vec2 uv, TEXCOORD0)
OUT(vec4 tint, COLOR)
MAIN_DECLARATION
{
  gl_Position = vec4(vertex.xy * transform.xy + transform.zw, 0.0, 1.0);
  uv = uv0.xy;
  tint = colour;
}
)";

const char* kUiFragmentSource = R"(OGRE_NATIVE_GLSL_VERSION_DIRECTIVE
#include <OgreUnifiedShader.h>
SAMPLER2D(image, 0);
MAIN_PARAMETERS
IN(vec2 uv, TEXCOORD0)
IN(vec4 tint, COLOR)
MAIN_DECLARATION
{
  gl_FragColor = tint * texture2D(image, uv);
}
)";

Ogre::TexturePtr MakeUiTexture(const std::string& name, uint32_t width, uint32_t height,
                               const uint8_t* rgba) {
  Ogre::TexturePtr texture = Ogre::TextureManager::getSingleton().createManual(
      name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, Ogre::TEX_TYPE_2D, width,
      height, 0, Ogre::PF_BYTE_RGBA, Ogre::TU_DEFAULT);
  Ogre::PixelBox box(width, height, 1, Ogre::PF_BYTE_RGBA, const_cast<uint8_t*>(rgba));
  texture->getBuffer()->blitFromMemory(box);
  return texture;
}

void CreateUiResources(tl_backend* b) {
  if (b->ui_vertex_program) return;
  b->ui_vertex_program = MakeProgram("tl_ui_vs", Ogre::GPT_VERTEX_PROGRAM, kUiVertexSource);
  b->ui_fragment_program = MakeProgram("tl_ui_fs", Ogre::GPT_FRAGMENT_PROGRAM, kUiFragmentSource);
  b->ui_vertex_params = MakeParameters(b->ui_vertex_program);
  b->ui_fragment_params = MakeParameters(b->ui_fragment_program);
  b->ui_fragment_params->setNamedConstant("image", 0);
  const uint8_t white[4] = {255, 255, 255, 255};
  b->ui_white = MakeUiTexture("tl_ui_white", 1, 1, white);
}

// The frame's UI over `viewport`'s whole target.
void DrawUiPass(tl_backend* b, Ogre::Viewport* viewport) {
  const tl_backend::UiFrame& ui = b->ui;
  if (ui.cmds.empty() || ui.vertices.empty() || ui.space_width <= 0 || ui.space_height <= 0) {
    return;
  }
  CreateUiResources(b);
  Ogre::RenderSystem* rs = b->rs;
  auto& buffers = Ogre::HardwareBufferManager::getSingleton();
  if (!b->ui_vertex_buffer || b->ui_vertex_buffer->getNumVertices() < ui.vertices.size()) {
    b->ui_vertex_buffer = buffers.createVertexBuffer(sizeof(tl_ui_vertex), ui.vertices.size() * 2,
                                                     Ogre::HBU_CPU_TO_GPU);
  }
  b->ui_vertex_buffer->writeData(0, ui.vertices.size() * sizeof(tl_ui_vertex), ui.vertices.data(),
                                 true);
  const bool indexed = !ui.indices.empty();
  if (indexed) {
    if (!b->ui_index_buffer || b->ui_index_buffer->getNumIndexes() < ui.indices.size()) {
      b->ui_index_buffer = buffers.createIndexBuffer(Ogre::HardwareIndexBuffer::IT_16BIT,
                                                     ui.indices.size() * 2, Ogre::HBU_CPU_TO_GPU);
    }
    b->ui_index_buffer->writeData(0, ui.indices.size() * sizeof(uint16_t), ui.indices.data(),
                                  true);
  }

  viewport->setDimensions(0, 0, 1, 1);
  SetViewport(b, viewport);
  const float target_width = float(viewport->getActualWidth());
  const float target_height = float(viewport->getActualHeight());
  UnbindProgram(b);
  SetColourBlend(b, Ogre::SBF_SOURCE_ALPHA, Ogre::SBF_ONE_MINUS_SOURCE_ALPHA, 0xF);
  rs->_setDepthBufferParams(false, false);
  rs->_setCullingMode(Ogre::CULL_NONE);
  rs->_setPolygonMode(Ogre::PM_SOLID);
  const float y_sign = b->active->requiresTextureFlipping() ? 1.0f : -1.0f;
  b->ui_vertex_params->setNamedConstant(
      "transform",
      Ogre::Vector4(2 / ui.space_width, y_sign * 2 / ui.space_height, -1, -y_sign));
  rs->bindGpuProgram(b->ui_vertex_program->_getBindingDelegate());
  rs->bindGpuProgram(b->ui_fragment_program->_getBindingDelegate());
  rs->bindGpuProgramParameters(Ogre::GPT_VERTEX_PROGRAM, b->ui_vertex_params, Ogre::GPV_ALL);
  rs->bindGpuProgramParameters(Ogre::GPT_FRAGMENT_PROGRAM, b->ui_fragment_params, Ogre::GPV_ALL);
  rs->_disableTextureUnitsFrom(1);

  Ogre::VertexData vertex_data;
  vertex_data.vertexDeclaration->addElement(0, offsetof(tl_ui_vertex, x), Ogre::VET_FLOAT2,
                                            Ogre::VES_POSITION);
  vertex_data.vertexDeclaration->addElement(0, offsetof(tl_ui_vertex, u), Ogre::VET_FLOAT2,
                                            Ogre::VES_TEXTURE_COORDINATES, 0);
  vertex_data.vertexDeclaration->addElement(0, offsetof(tl_ui_vertex, colour),
                                            Ogre::VET_UBYTE4_NORM, Ogre::VES_DIFFUSE);
  vertex_data.vertexBufferBinding->setBinding(0, b->ui_vertex_buffer);
  Ogre::IndexData index_data;
  index_data.indexBuffer = b->ui_index_buffer;
  const float scale_x = target_width / ui.space_width, scale_y = target_height / ui.space_height;
  for (const tl_ui_cmd& cmd : ui.cmds) {
    if (!cmd.count) continue;
    if (cmd.scissor) {
      long left = std::max(0L, long(std::floor(cmd.scissor_left * scale_x)));
      long top = std::max(0L, long(std::floor(cmd.scissor_top * scale_y)));
      long right = std::min(long(target_width), long(std::ceil(cmd.scissor_right * scale_x)));
      long bottom = std::min(long(target_height), long(std::ceil(cmd.scissor_bottom * scale_y)));
      if (right <= left || bottom <= top) continue;
      rs->setScissorTest(true, Ogre::Rect(left, top, right, bottom));
    } else {
      rs->setScissorTest(false);
    }
    const tl_backend::UiTexture* texture = nullptr;
    if (cmd.texture) {
      auto it = b->ui_textures.find(cmd.texture);
      if (it != b->ui_textures.end()) texture = &it->second;
    }
    rs->_setTexture(0, true, texture ? texture->texture : b->ui_white);
    Ogre::FilterOptions filter = texture && texture->linear ? Ogre::FO_LINEAR : Ogre::FO_POINT;
    Ogre::TextureAddressingMode address =
        texture && texture->repeat ? Ogre::TAM_WRAP : Ogre::TAM_CLAMP;
    rs->_setSampler(0, *GetSampler(b, filter, filter, Ogre::FO_NONE, address, address, address, 1));
    Ogre::RenderOperation op;
    op.operationType =
        cmd.lines ? Ogre::RenderOperation::OT_LINE_LIST : Ogre::RenderOperation::OT_TRIANGLE_LIST;
    op.vertexData = &vertex_data;
    if (indexed) {
      if (cmd.index_start + cmd.count > ui.indices.size()) continue;
      vertex_data.vertexStart = size_t(std::max(0, cmd.base_vertex));
      vertex_data.vertexCount = ui.vertices.size() - vertex_data.vertexStart;
      index_data.indexStart = cmd.index_start;
      index_data.indexCount = cmd.count;
      op.useIndexes = true;
      op.indexData = &index_data;
    } else {
      if (size_t(cmd.base_vertex) + cmd.index_start + cmd.count > ui.vertices.size()) continue;
      vertex_data.vertexStart = size_t(std::max(0, cmd.base_vertex)) + cmd.index_start;
      vertex_data.vertexCount = cmd.count;
      op.useIndexes = false;
    }
    rs->_render(op);
  }
  rs->setScissorTest(false);
  EndFullScreenPass(b);
}

// The RTSS (its shader library from OGRE's media, generated sources cached in the platform's
// cache directory) and what ApplyProgram feeds it: the data source, a scene manager that is never
// rendered (it owns the camera and the light), and placeholder textures that give the generated
// samplers their type.
// OGRE's media the backend uses: Main (OgreUnifiedShader.h, which the backend's own programs
// include too) and the RTSS's library. Before any program is created.
void RegisterMedia() {
  auto& resources = Ogre::ResourceGroupManager::getSingleton();
  const std::string group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME;
  const std::string media = torchlight::platform::OgreMediaDir();
  resources.addResourceLocation(media + "Main", "FileSystem", group);
  resources.addResourceLocation(media + "RTShaderLib", "FileSystem", group);
  resources.initialiseResourceGroup(group);
}

void CreateProgramGenerator(tl_backend* b) {
  const std::string group = Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME;
  if (!Ogre::RTShader::ShaderGenerator::initialize()) {
    OGRE_EXCEPT(Ogre::Exception::ERR_INTERNAL_ERROR, "cannot initialise the RTSS",
                "CreateProgramGenerator");
  }
  Ogre::RTShader::ShaderGenerator::getSingleton().setShaderCachePath(
      torchlight::platform::ShaderCacheDir());
  b->guest_texgen_factory = std::make_unique<GuestTexgenFactory>();
  b->guest_pixel_fog = std::make_unique<GuestPixelFogFactory>();
  b->guest_alpha_test = std::make_unique<GuestAlphaTestFactory>();
  b->guest_lighting = std::make_unique<GuestLightingFactory>();
  for (auto* factory : {b->guest_texgen_factory.get(), b->guest_pixel_fog.get(),
                        b->guest_alpha_test.get(), b->guest_lighting.get()})
    Ogre::RTShader::ShaderGenerator::getSingleton().addSubRenderStateFactory(factory);
  Ogre::MeshManager::setBonesUseObjectSpace(true);  // see GenerateProgram (skinning)
  b->params_source = std::make_unique<Ogre::AutoParamDataSource>();
  b->scene = b->root->createSceneManager();
  Ogre::RTShader::ShaderGenerator::getSingleton().addSceneManager(b->scene);
  b->camera = b->scene->createCamera("tl_camera");
  b->scene->getRootSceneNode()->createChildSceneNode()->attachObject(b->camera);
  b->camera->setCustomViewMatrix(true, Ogre::Affine3::IDENTITY);  // world = guest world-view
  b->light = b->scene->createLight("tl_light", Ogre::Light::LT_DIRECTIONAL);
  b->light->setSpecularColour(Ogre::ColourValue::Black);
  b->light_node = b->scene->getRootSceneNode()->createChildSceneNode();
  b->light_node->attachObject(b->light);
  auto& textures = Ogre::TextureManager::getSingleton();
  b->placeholder_2d = textures.createManual("tl_rtss_2d", group, Ogre::TEX_TYPE_2D, 1, 1, 0,
                                            Ogre::PF_A8R8G8B8);
  b->placeholder_cube = textures.createManual("tl_rtss_cube", group, Ogre::TEX_TYPE_CUBE_MAP, 1, 1,
                                              0, Ogre::PF_A8R8G8B8);
}

// The GPU `gpu` (a platform GPU id) for render systems that take one (Direct3D 11's "Rendering
// Device"): resolved against the devices the render system itself offers. Without a match the
// option is left alone (OGRE's "(default)": the system's choice), and OGRE's log says so.
void SelectGpu(Ogre::RenderSystem* rs, const char* gpu) {
  if (!gpu || !*gpu) return;
  const Ogre::ConfigOptionMap& options = rs->getConfigOptions();
  const auto device = options.find("Rendering Device");
  if (device == options.end()) {
    Ogre::LogManager::getSingleton().logMessage(
        std::string("tl_backend: GPU ") + gpu + " ignored: " + rs->getName() +
        " renders on the GPU the system gives it");
    return;
  }
  const std::vector<std::string> devices(device->second.possibleValues.begin(),
                                         device->second.possibleValues.end());
  const std::string name = torchlight::platform::RenderingDeviceForGpu(gpu, devices);
  if (name.empty()) {
    Ogre::LogManager::getSingleton().logMessage(
        std::string("tl_backend: GPU ") + gpu + " not among the render system's devices; automatic",
        Ogre::LML_CRITICAL);
    return;
  }
  rs->setConfigOption("Rendering Device", name);
  Ogre::LogManager::getSingleton().logMessage(std::string("tl_backend: GPU ") + gpu + ": " + name);
}

tl_backend* CreateBackend(tl_render_system render_system, const char* gpu, uint32_t width,
                          uint32_t height, const char* log_path, const char* window_title,
                          const tl_native_window* parent, uint32_t window_width,
                          uint32_t window_height, bool vsync, char* error, uint32_t error_size) {
  tl_backend* b = new tl_backend();
  b->visible_window = window_title != nullptr;
  b->child_window = parent != nullptr;
  torchlight::platform::NativeWindow parent_window;
  if (parent) parent_window = {parent->system, parent->window, parent->display};
  b->width = width;
  b->height = height;
  try {
    // OGRE's log, made before the root so the root does not make its own next to the process.
    if (!Ogre::LogManager::getSingletonPtr()) {
      b->log_manager = new Ogre::LogManager();
      b->log_manager->createLog(log_path ? log_path : "tl_backend_ogre.log", true, true,
                                log_path == nullptr);
    }
    b->root = new Ogre::Root("", "", "");
    std::string plugins = torchlight::platform::OgrePluginDir();
    if (!plugins.empty() && plugins.back() == '/') plugins.pop_back();
    if (render_system == TL_RENDER_SYSTEM_D3D11) {
      b->root->loadPlugin(plugins + "/RenderSystem_Direct3D11");
      b->depth_bias_factor = conv::kD3D11DepthBiasFactor;
    } else {
      b->root->loadPlugin(torchlight::platform::OgreRenderSystemDir(
                              plugins, parent ? &parent_window : nullptr) +
                          "/RenderSystem_GL3Plus");
    }
    // Image files the guest loads are DDS (built into OgreMain); a few are PNG.
    b->root->loadPlugin(plugins + "/Codec_STBI");
    const Ogre::RenderSystemList& list = b->root->getAvailableRenderers();
    if (list.empty()) {
      SetError(error, error_size, "no OGRE render system available");
      tl_backend_destroy(b);
      return nullptr;
    }
    b->rs = list.front();
    b->root->setRenderSystem(b->rs);
    SelectGpu(b->rs, gpu);
    b->rs->setConfigOption("Full Screen", "No");
    b->root->initialise(false);
    Ogre::NameValuePairList misc;
    misc["title"] = window_title ? window_title : "torchlight replay";
    // Only a window that is the game's display waits for vertical sync; otherwise the window must
    // never pace the backend.
    misc["vsync"] = vsync ? "true" : "false";
    if (!b->visible_window) misc["hidden"] = "true";
    uint32_t window_w = b->visible_window ? width : 64, window_h = b->visible_window ? height : 64;
    if (b->child_window) {
      // Inside the application's window: that window keeps the focus, so key and pointer events
      // (not selected here) reach the application.
      for (const auto& [key, value] : torchlight::platform::OgreWindowParams(parent_window)) {
        misc[key] = value;
      }
      window_w = window_width;
      window_h = window_height;
    } else {
      b->top_level = torchlight::platform::OgreTopLevelWindow::Create(
          misc["title"], window_w, window_h, b->visible_window);
      if (b->top_level) {
        for (const auto& [key, value] :
             torchlight::platform::OgreWindowParams(b->top_level->native())) {
          misc[key] = value;
        }
      }
    }
    b->window = b->root->createRenderWindow("tl_backend", window_w, window_h, false, &misc);
    b->window->setAutoUpdated(false);
    if (b->visible_window) {
      b->window_viewport = b->window->addViewport(nullptr);
      b->window_viewport->setClearEveryFrame(false);
      b->window_viewport->setOverlaysEnabled(false);
      if (!b->child_window) {
        Ogre::RenderWindow* window = b->window;
        b->keyboard = torchlight::platform::KeyReader::ForOgreWindow(
            [window](const char* name, void* out) { window->getCustomAttribute(name, out); });
      }
    }
    if (!MakeTarget(b, "tl_main", width, height, true, b->main)) {
      SetError(error, error_size, "cannot create the main render texture");
      tl_backend_destroy(b);
      return nullptr;
    }
    if (!MakeTarget(b, "tl_output", width, height, true, b->output)) {
      SetError(error, error_size, "cannot create the output render texture");
      tl_backend_destroy(b);
      return nullptr;
    }
    RegisterMedia();
    CreateDisplayGamma(b);
    CreateClear(b);
    b->current = &b->main;
    for (int i = 0; i < TL_MAX_STAGES; ++i) b->projectors[i] = std::make_unique<Ogre::Frustum>();
    CreateProgramGenerator(b);
    const Ogre::RenderSystemCapabilities* caps = b->rs->getCapabilities();
    b->renderer = caps->getDeviceName() + " | " +
                  Ogre::RenderSystemCapabilities::vendorToString(caps->getVendor()) + " | " +
                  caps->getDriverVersion().toString();
  } catch (Ogre::Exception& e) {
    SetError(error, error_size, e.getFullDescription());
    tl_backend_destroy(b);
    return nullptr;
  }
  return b;
}

}  // namespace

extern "C" {

tl_backend* tl_backend_create(tl_render_system render_system, const char* gpu, uint32_t width,
                              uint32_t height, const char* log_path, const char* window_title,
                              char* error, uint32_t error_size) {
  return CreateBackend(render_system, gpu, width, height, log_path, window_title, nullptr, 0, 0, false,
                       error, error_size);
}

tl_backend* tl_backend_create_child(tl_render_system render_system, const char* gpu,
                                    uint32_t width, uint32_t height, const char* log_path,
                                    const tl_native_window* parent, uint32_t window_width,
                                    uint32_t window_height, int vsync, char* error,
                                    uint32_t error_size) {
  return CreateBackend(render_system, gpu, width, height, log_path, "", parent, window_width,
                       window_height, vsync != 0, error, error_size);
}

void tl_backend_destroy(tl_backend* b) {
  if (!b) return;
  b->host_vertex.clear();
  b->host_index.clear();
  b->vertex_layouts.clear();
  b->textures.clear();
  b->targets.clear();
  b->samplers.clear();
  b->main.texture.reset();
  b->output.texture.reset();
  b->quad.reset();
  b->ui_textures.clear();
  b->ui_white.reset();
  b->ui_vertex_buffer.reset();
  b->ui_index_buffer.reset();
  b->ui_vertex_params.reset();
  b->ui_fragment_params.reset();
  b->ui_vertex_program.reset();
  b->ui_fragment_program.reset();
  b->window_quad.reset();
  b->gamma_params.reset();
  b->copy_params.reset();
  b->gamma_program.reset();
  b->copy_program.reset();
  b->quad_vertex_program.reset();
  b->clear_vertex_params.reset();
  b->clear_fragment_params.reset();
  b->clear_vertex_program.reset();
  b->clear_fragment_program.reset();
  b->gamma_lut.reset();
  b->programs.clear();
  b->placeholder_2d.reset();
  b->placeholder_cube.reset();
  b->params_source.reset();
  // The generator destroys its render states through the factories, so ours stays registered
  // until then.
  if (Ogre::RTShader::ShaderGenerator::getSingletonPtr()) Ogre::RTShader::ShaderGenerator::destroy();
  for (auto* factory : {b->guest_texgen_factory.get(), b->guest_pixel_fog.get(),
                        b->guest_alpha_test.get(), b->guest_lighting.get()})
    if (factory) factory->destroyAllInstances();
  b->guest_texgen_factory.reset();
  b->guest_pixel_fog.reset();
  b->guest_alpha_test.reset();
  b->guest_lighting.reset();
  if (b->query) b->rs->destroyHardwareOcclusionQuery(b->query);
  for (auto& p : b->projectors) p.reset();
  b->keyboard.reset();
  delete b->root;
  b->top_level.reset();  // after OGRE's window, which draws in it
  delete b->log_manager;
  delete b;
}

const char* tl_backend_renderer(tl_backend* b) { return b->renderer.c_str(); }

int tl_backend_vertex_buffer(tl_backend* b, uint64_t id, const void* data, uint32_t size,
                             uint32_t fetch_endian) {
  tl_backend::Buffer& buf = b->vertex_buffers[id];
  buf.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
  conv::SwapFetchEndian(buf.bytes.data(), buf.bytes.size(), fetch_endian);
  buf.index_size = 0;
  return 0;
}

int tl_backend_index_buffer(tl_backend* b, uint64_t id, const void* data, uint32_t size,
                            uint32_t index_size) {
  if (index_size != 2 && index_size != 4) return 1;
  tl_backend::Buffer& buf = b->index_buffers[id];
  buf.bytes.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
  conv::SwapIndices(buf.bytes.data(), buf.bytes.size(), index_size);
  buf.index_size = index_size;
  return 0;
}

int tl_backend_texture_file(tl_backend* b, uint64_t id, const char* name, const void* data,
                            uint32_t size, const char* extension, uint32_t type, char* error,
                            uint32_t error_size) {
  try {
    Ogre::DataStreamPtr stream =
        std::make_shared<Ogre::MemoryDataStream>(const_cast<void*>(data), size, false, true);
    Ogre::Image image;
    image.load(stream, extension);
    char unique[32];
    std::snprintf(unique, sizeof(unique), "#%u", ++b->texture_serial);
    const int mipmaps = Ogre::PixelUtil::isCompressed(image.getFormat())
                            ? int(conv::CompressedMipmapsForHost(image.getWidth(),
                                                                 image.getHeight(),
                                                                 image.getNumMipmaps()))
                            : int(Ogre::MIP_DEFAULT);
    b->textures[id] = Ogre::TextureManager::getSingleton().loadImage(
        std::string(name) + unique, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME,
        image, type == TL_TEXTURE_CUBE ? Ogre::TEX_TYPE_CUBE_MAP : Ogre::TEX_TYPE_2D, mipmaps);
  } catch (Ogre::Exception& e) {
    SetError(error, error_size, e.getDescription());
    return 1;
  }
  return 0;
}

int tl_backend_texture_linear(tl_backend* b, uint64_t id, uint32_t width, uint32_t height,
                              uint32_t format, const void* data, uint32_t size) {
  Ogre::PixelFormat pf;
  switch (format) {
    case TL_LINEAR_BGRA8: pf = Ogre::PF_A8R8G8B8; break;  // bytes B,G,R,A on little-endian
    case TL_LINEAR_DXT1: pf = Ogre::PF_DXT1; break;
    case TL_LINEAR_DXT3: pf = Ogre::PF_DXT3; break;
    case TL_LINEAR_DXT5: pf = Ogre::PF_DXT5; break;
    default: return 1;
  }
  if (Ogre::PixelUtil::getMemorySize(width, height, 1, pf) > size) return 2;
  try {
    auto existing = b->textures.find(id);
    if (existing != b->textures.end() && !b->targets.count(id)) {
      const Ogre::TexturePtr& texture = existing->second;
      if (texture->getTextureType() == Ogre::TEX_TYPE_2D && texture->getWidth() == width &&
          texture->getHeight() == height && texture->getFormat() == pf &&
          texture->getNumMipmaps() == 0) {
        Ogre::PixelBox box(width, height, 1, pf, const_cast<void*>(data));
        texture->getBuffer()->blitFromMemory(box);
        return 0;
      }
      tl_backend_release_texture(b, id);
    }
    Ogre::Image image;
    image.loadDynamicImage(static_cast<Ogre::uchar*>(const_cast<void*>(data)), width, height, 1,
                           pf, false);
    char unique[32];
    std::snprintf(unique, sizeof(unique), "tl_linear#%u", ++b->texture_serial);
    b->textures[id] = Ogre::TextureManager::getSingleton().loadImage(
        unique, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME, image,
        Ogre::TEX_TYPE_2D, 0);
  } catch (Ogre::Exception&) {
    return 3;
  }
  return 0;
}

int tl_backend_render_target(tl_backend* b, uint64_t id, uint32_t width, uint32_t height) {
  if (b->targets.count(id)) return 0;
  char name[64];
  std::snprintf(name, sizeof(name), "tl_rt#%llu", (unsigned long long)id);
  tl_backend::Target t;
  try {
    if (!MakeTarget(b, name, width, height, false, t)) return 1;
  } catch (Ogre::Exception&) {
    return 1;
  }
  b->targets[id] = t;
  b->textures[id] = t.texture;
  return 0;
}

int tl_backend_set_render_scale(tl_backend* b, float scale) {
  if (!(scale >= 0.25f && scale <= 4.0f)) return 1;
  if (scale == b->scale.factor) return 0;
  b->scale.factor = scale;
  try {
    // Nothing may keep an old target alive: its render texture is registered under the same name
    // as the new one, and destroying it later would take the new one down with it.
    b->rs->_disableTextureUnitsFrom(0);
    b->enabled_units = 0;
    RemakeTarget(b, b->main);
    RemakeTarget(b, b->output);
  } catch (Ogre::Exception& e) {
    Ogre::LogManager::getSingleton().logError("render scale: " + e.getDescription());
    return 1;
  }
  b->active = nullptr;
  b->current = &b->main;
  return 0;
}

int tl_backend_set_guest_size(tl_backend* b, uint32_t width, uint32_t height) {
  if (!width || !height) return 1;
  if (width == b->width && height == b->height) return 0;
  b->width = width;
  b->height = height;
  for (tl_backend::Target* t : {&b->main, &b->output}) {
    t->guest_width = width;
    t->guest_height = height;
  }
  try {
    // As in tl_backend_set_render_scale: nothing may keep an old target alive.
    b->rs->_disableTextureUnitsFrom(0);
    b->enabled_units = 0;
    RemakeTarget(b, b->main);
    RemakeTarget(b, b->output);
  } catch (Ogre::Exception& e) {
    Ogre::LogManager::getSingleton().logError("guest size: " + e.getDescription());
    return 1;
  }
  b->active = nullptr;
  b->current = &b->main;
  return 0;
}

void tl_backend_begin(tl_backend* b) {
  SetViewport(b, b->main.viewport);
  b->rs->_beginFrame();
}

void tl_backend_end(tl_backend* b) {
  try {
    DisplayGammaPass(b);
    // Offscreen there is no window to present the UI on: it goes over the output.
    if (!b->visible_window) DrawUiPass(b, b->output.viewport);
  } catch (Ogre::Exception&) {
  }
  b->rs->_endFrame();
}

int tl_backend_present(tl_backend* b) {
  if (!b->visible_window || b->window->isClosed()) return 0;
  try {
    // The output texture (top row first, see DisplayGammaPass) on a full-window quad (copy
    // program): the window is not a render texture, so no y flip applies.
    Ogre::RenderSystem* rs = b->rs;
    // The output keeps its aspect: black bars around it when the window's aspect differs.
    b->window_viewport->setDimensions(0, 0, 1, 1);
    SetViewport(b, b->window_viewport);
    rs->_beginFrame();
    SetColourBlend(b, Ogre::SBF_ONE, Ogre::SBF_ZERO, 0xF);
    rs->clearFrameBuffer(Ogre::FBT_COLOUR, Ogre::ColourValue::Black);
    float window_aspect =
        float(b->window->getWidth()) / float(std::max(1u, b->window->getHeight()));
    float output_aspect = float(b->width) / float(b->height);
    if (window_aspect > output_aspect) {
      float w = output_aspect / window_aspect;
      b->window_viewport->setDimensions((1 - w) / 2, 0, w, 1);
    } else {
      float h = window_aspect / output_aspect;
      b->window_viewport->setDimensions(0, (1 - h) / 2, 1, h);
    }
    SetViewport(b, b->window_viewport);
    FullScreenPassState(b, b->copy_program, b->copy_params);
    rs->_setTexture(0, true, b->output.texture);
    // One output pixel per window pixel: point sampling is exact. Scaled to the window: bilinear,
    // as the emulated GPU's presenter does by default. Clamped: bilinear filtering at the edges
    // must not blend in the opposite edge.
    bool scaled = b->window_viewport->getActualWidth() != int(b->output.rt->getWidth()) ||
                  b->window_viewport->getActualHeight() != int(b->output.rt->getHeight());
    Ogre::FilterOptions filter = scaled ? Ogre::FO_LINEAR : Ogre::FO_POINT;
    rs->_setSampler(0, *GetSampler(b, filter, filter, Ogre::FO_NONE, Ogre::TAM_CLAMP,
                                   Ogre::TAM_CLAMP, Ogre::TAM_CLAMP, 1));
    rs->_disableTextureUnitsFrom(1);
    DrawQuad(b, b->window_quad);
    EndFullScreenPass(b);
    DrawUiPass(b, b->window_viewport);
    rs->_endFrame();
    b->window->swapBuffers();
  } catch (Ogre::Exception&) {
    return 0;
  }
  if (b->keyboard && b->keyboard->TakeF9()) b->keys |= TL_KEY_F9;
  return 1;
}

int tl_backend_ui_texture(tl_backend* b, uint64_t id, uint32_t width, uint32_t height, int linear,
                          int repeat, const uint8_t* rgba) {
  if (!id || !width || !height || !rgba) return 1;
  try {
    tl_backend_ui_release_texture(b, id);
    tl_backend::UiTexture& t = b->ui_textures[id];
    t.texture = MakeUiTexture("tl_ui#" + std::to_string(id), width, height, rgba);
    t.linear = linear != 0;
    t.repeat = repeat != 0;
  } catch (Ogre::Exception& e) {
    b->ui_textures.erase(id);
    Ogre::LogManager::getSingleton().logError("UI texture: " + e.getDescription());
    return 1;
  }
  return 0;
}

void tl_backend_ui_release_texture(tl_backend* b, uint64_t id) {
  auto it = b->ui_textures.find(id);
  if (it == b->ui_textures.end()) return;
  std::string name = it->second.texture->getName();
  b->ui_textures.erase(it);
  Ogre::TextureManager::getSingleton().remove(name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
}

void tl_backend_set_ui_frame(tl_backend* b, float space_width, float space_height,
                             const tl_ui_vertex* vertices, uint32_t vertex_count,
                             const uint16_t* indices, uint32_t index_count, const tl_ui_cmd* cmds,
                             uint32_t cmd_count) {
  tl_backend::UiFrame& ui = b->ui;
  ui.space_width = space_width;
  ui.space_height = space_height;
  ui.vertices.assign(vertices, vertices + (vertices ? vertex_count : 0));
  ui.indices.assign(indices, indices + (indices ? index_count : 0));
  ui.cmds.assign(cmds, cmds + (cmds ? cmd_count : 0));
}

uint32_t tl_backend_take_keys(tl_backend* b) {
  uint32_t keys = b->keys;
  b->keys = 0;
  return keys;
}

void tl_backend_set_vsync(tl_backend* b, int vsync) {
  if (b->visible_window && b->window) b->window->setVSyncEnabled(vsync != 0);
}

void tl_backend_resize_window(tl_backend* b, uint32_t width, uint32_t height) {
  if (!b->child_window || !width || !height) return;
  // OGRE 14 resizes the window itself (X11EGLWindow::resize, a child window; WaylandEGLWindow::
  // resize, the EGL window on the application's surface) and updates its viewports.
  b->window->resize(width, height);
}

void tl_backend_release_buffer(tl_backend* b, uint64_t id) {
  b->vertex_buffers.erase(id);
  b->index_buffers.erase(id);
  // Host buffers of draws that had no owner: the content id stood for it.
  tl_backend_release_buffer_owner(b, id);
}

void tl_backend_release_buffer_owner(tl_backend* b, uint64_t owner) {
  b->host_index.erase(owner);
  auto it = b->host_vertex.lower_bound({owner, 0, 0});
  while (it != b->host_vertex.end() && it->first.owner == owner) it = b->host_vertex.erase(it);
}

void tl_backend_release_texture(tl_backend* b, uint64_t id) {
  auto it = b->textures.find(id);
  if (it == b->textures.end()) return;
  std::string name = it->second->getName();
  b->textures.erase(it);
  auto t = b->targets.find(id);
  if (t != b->targets.end()) {
    if (b->current == &t->second) b->current = &b->main;
    b->targets.erase(t);
  }
  Ogre::TextureManager::getSingleton().remove(
      name, Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);
}

void tl_backend_set_gamma_ramp(tl_backend* b, int pwl, const uint16_t values[768]) {
  uint8_t lut[256 * 3];
  conv::GammaRampLut(pwl != 0, values, lut);
  UploadGammaLut(b, lut);
}

int tl_backend_set_target(tl_backend* b, uint64_t target_id) {
  tl_backend::Target* t = &b->main;
  if (target_id != 0) {
    auto it = b->targets.find(target_id);
    if (it == b->targets.end()) return 1;
    t = &it->second;
  }
  b->current = t;
  t->viewport->setDimensions(0, 0, 1, 1);
  t->viewport_width = int32_t(t->guest_width);
  t->viewport_height = int32_t(t->guest_height);
  SetViewport(b, t->viewport);
  return 0;
}

void tl_backend_set_viewport(tl_backend* b, int32_t left, int32_t top, int32_t width,
                             int32_t height) {
  // Relative to the guest's size of the target: the same area at any render scale.
  tl_backend::Target* t = b->current;
  Ogre::Real w = Ogre::Real(t->guest_width), h = Ogre::Real(t->guest_height);
  t->viewport->setDimensions(left / w, top / h, width / w, height / h);
  t->viewport_width = width;
  t->viewport_height = height;
  SetViewport(b, t->viewport);
}

void tl_backend_clear(tl_backend* b, uint32_t buffers, const float colour[4], float depth,
                      uint32_t stencil) {
  unsigned int ogre_buffers = 0;
  if (buffers & 1) ogre_buffers |= Ogre::FBT_COLOUR;
  if (buffers & 2) ogre_buffers |= Ogre::FBT_DEPTH;
  if (buffers & 4) ogre_buffers |= Ogre::FBT_STENCIL;
  // The colour write mask also masks clears; clears write every channel.
  Ogre::SceneBlendFactor src = Ogre::SBF_ONE, dst = Ogre::SBF_ZERO;
  ToOgre(b->state.blend_src, src);
  ToOgre(b->state.blend_dst, dst);
  SetColourBlend(b, src, dst, 0xF);
  Ogre::CompareFunction depth_func = Ogre::CMPF_LESS_EQUAL;
  ToOgre(conv::DepthFunctionForHost(b->state.depth_func_requested, b->state.depth_func_effective),
         depth_func);
  b->rs->_setDepthBufferParams(b->state.depth_check != 0, true, depth_func);
  const Ogre::ColourValue clear_colour(colour[0], colour[1], colour[2], colour[3]);
  if (const auto strip = SceneClip16x9(b)) {
    // A clear takes the viewport's area (ClearViewport): the whole target black first, then the
    // strip as asked.
    Ogre::Viewport* viewport = b->current->viewport;
    const Ogre::Real left = viewport->getLeft(), top = viewport->getTop(),
                     width = viewport->getWidth(), height = viewport->getHeight();
    const Ogre::Real target_width = Ogre::Real(b->main.rt->getWidth());
    viewport->setDimensions(0, 0, 1, 1);
    SetViewport(b, viewport);
    ClearViewport(b, ogre_buffers | Ogre::FBT_COLOUR, Ogre::ColourValue::Black,
                  conv::ClearDepthForHost(depth), Ogre::uint16(stencil));
    if (ogre_buffers & Ogre::FBT_COLOUR) {
      viewport->setDimensions(strip->left / target_width, 0, strip->width() / target_width, 1);
      SetViewport(b, viewport);
      ClearViewport(b, Ogre::FBT_COLOUR, clear_colour, 1.0f, 0);
    }
    viewport->setDimensions(left, top, width, height);
    SetViewport(b, viewport);
    return;
  }
  ClearViewport(b, ogre_buffers, clear_colour, conv::ClearDepthForHost(depth),
                Ogre::uint16(stencil));
}

void tl_backend_set_scene_clip(tl_backend* b, int enabled) { b->scene_clip = enabled != 0; }

void tl_backend_set_state(tl_backend* b, const tl_state* s) {
  b->state = *s;
  Ogre::SceneBlendFactor src = Ogre::SBF_ONE, dst = Ogre::SBF_ZERO;
  ToOgre(s->blend_src, src);
  ToOgre(s->blend_dst, dst);
  SetColourBlend(b, src, dst, s->colour_write);
  Ogre::CompareFunction depth = Ogre::CMPF_LESS_EQUAL;
  ToOgre(conv::DepthFunctionForHost(s->depth_func_requested, s->depth_func_effective), depth);
  b->rs->_setDepthBufferParams(s->depth_check != 0, s->depth_write != 0, depth);
  b->rs->setInvertVertexWinding(s->invert_winding != 0);
  b->rs->_setCullingMode(ToOgreCull(conv::CullModeForHost(s->cull_mode, s->invert_winding != 0)));
  // The alpha test is part of the draw's program (GuestAlphaTest).
  b->rs->_setPolygonMode(ToOgrePolygon(s->polygon_mode));
  b->rs->_setDepthBias(conv::DepthBiasConstantForHost(s->depth_bias_constant, b->depth_bias_factor),
                       s->depth_bias_slope);
}

int tl_backend_set_texture(tl_backend* b, uint32_t unit, uint64_t texture_id,
                           const tl_sampler* sampler) {
  if (texture_id == 0) {
    b->rs->_disableTextureUnit(unit);
    if (unit < 32) b->enabled_units &= ~(1u << unit);
    return 0;
  }
  auto it = b->textures.find(texture_id);
  if (it == b->textures.end()) {
    b->rs->_disableTextureUnit(unit);
    if (unit < 32) b->enabled_units &= ~(1u << unit);
    return 1;
  }
  if (unit < 32) b->enabled_units |= 1u << unit;
  if (unit < TL_MAX_STAGES) {
    b->unit_cube[unit] = it->second->getTextureType() == Ogre::TEX_TYPE_CUBE_MAP;
    b->unit_coord_set[unit] = sampler->coord_set;
  }
  b->rs->_setTexture(unit, true, it->second);
  b->rs->_setSampler(
      unit, *GetSampler(b, ToOgreFilter(sampler->min_filter), ToOgreFilter(sampler->mag_filter),
                        ToOgreFilter(sampler->mip_filter), ToOgreAddress(sampler->address_u),
                        ToOgreAddress(sampler->address_v), ToOgreAddress(sampler->address_w),
                        sampler->max_anisotropy ? sampler->max_anisotropy : 1));
  return 0;
}

static int Draw(tl_backend* b, const tl_draw* d) {
  Ogre::RenderOperation::OperationType op_type;
  if (!ToOgre(d->primitive, op_type)) return TL_SKIP_UNKNOWN_PRIMITIVE;
  Ogre::HardwareBufferManager& mgr = Ogre::HardwareBufferManager::getSingleton();
  b->notes = 0;
  // Skinning layout: UBYTE4 indices and float weights in one stream (BlendLanesForHost rewrites
  // it for the RTSS), the palette within what the vertex program can hold.
  bool skin = d->skinning.influence_count > 0;
  int32_t blend_stream = -1;
  uint32_t index_offset = 0, weight_offset = 0, weight_components = 0;
  if (skin) {
    int32_t weight_stream = -1;
    for (uint32_t i = 0; i < d->element_count; ++i) {
      const tl_vertex_element& e = d->elements[i];
      if (e.semantic == 2 && e.type == 9) {
        blend_stream = int32_t(e.stream);
        index_offset = e.offset;
      } else if (e.semantic == 1 && e.type <= 3) {
        weight_stream = int32_t(e.stream);
        weight_offset = e.offset;
        weight_components = e.type + 1u;
      }
    }
    if (blend_stream < 0 || weight_stream != blend_stream ||
        uint32_t(blend_stream) >= d->stream_count ||
        d->skinning.influence_count > weight_components) {
      skin = false;
      b->notes |= TL_NOTE_SKINNING_LAYOUT;
    }
    // Three registers per bone, and room for the rest of the program's constants.
    size_t registers = b->rs->getCapabilities()->getConstantFloatCount(Ogre::GPT_VERTEX_PROGRAM);
    if (skin && (d->skinning.bone_count > Ogre::RTShader::HardwareSkinningFactory::
                                               getMaxCalculableBoneCount() ||
                 3 * size_t(d->skinning.bone_count) + kProgramRegisterReserve > registers)) {
      return TL_SKIP_TOO_MANY_BONES;
    }
  }
  try {
    uint64_t layout_key = 1469598103934665603ull;
    for (uint32_t i = 0; i < d->element_count; ++i) {
      const tl_vertex_element& e = d->elements[i];
      for (uint64_t v : {uint64_t(e.stream), uint64_t(e.offset), uint64_t(e.index),
                         uint64_t(e.type) << 8 | e.semantic}) {
        layout_key = (layout_key ^ v) * 1099511628211ull;
      }
    }
    tl_backend::VertexLayout& layout = b->vertex_layouts[layout_key];
    const bool new_layout = !layout.data;
    if (new_layout) {
      layout.data = std::make_unique<Ogre::VertexData>();
      ++b->counters.vertex_layouts_created;
    }
    Ogre::VertexData& vertex_data = *layout.data;
    std::vector<uint64_t> bound(d->stream_count, 0);
    // Colour elements (converted per stream below): (stream, offset).
    std::vector<std::pair<uint32_t, uint32_t>> colour_elements;
    for (uint32_t i = 0; i < d->element_count; ++i) {
      const tl_vertex_element& e = d->elements[i];
      Ogre::VertexElementType type;
      Ogre::VertexElementSemantic semantic;
      bool is_colour = false;
      if (!ToOgre(e.type, type, is_colour) || !ToOgre(e.semantic, semantic)) {
        b->vertex_layouts.erase(layout_key);
        return TL_SKIP_UNKNOWN_VERTEX_TYPE;
      }
      if (new_layout) {
        vertex_data.vertexDeclaration->addElement(Ogre::ushort(e.stream), e.offset, type,
                                                  semantic, Ogre::ushort(e.index));
      }
      if (is_colour) colour_elements.push_back({e.stream, e.offset});
    }
    vertex_data.vertexBufferBinding->unsetAllBindings();
    std::vector<uint32_t> colours;
    for (uint32_t s = 0; s < d->stream_count; ++s) {
      if (d->stream_buffers[s] == 0) continue;
      auto it = b->vertex_buffers.find(d->stream_buffers[s]);
      if (it == b->vertex_buffers.end()) return TL_SKIP_MISSING_BUFFER;
      uint32_t stride = d->stream_strides[s];
      if (stride == 0 || it->second.bytes.size() < stride) return TL_SKIP_BAD_RANGE;
      colours.clear();
      for (const auto& [stream, offset] : colour_elements) {
        if (stream == s) colours.push_back(offset);
      }
      bool blend = skin && int32_t(s) == blend_stream;
      // Layout: what the conversion below does to the bytes (FNV-1a over its parameters).
      uint64_t layout = 1469598103934665603ull;
      auto mix = [&layout](uint64_t v) { layout = (layout ^ v) * 1099511628211ull; };
      for (uint32_t c : colours) mix(c);
      if (blend) {
        const tl_skinning& k = d->skinning;
        mix(0x100000000ull | k.influence_count);
        mix(index_offset);
        mix(weight_offset);
        mix(weight_components);
        for (int lane = 0; lane < 4; ++lane) mix(uint64_t(k.index_lane[lane]) << 8 | k.weight_lane[lane]);
      }
      const uint64_t content = d->stream_buffers[s];
      const uint64_t owner = d->stream_owners && d->stream_owners[s] ? d->stream_owners[s] : content;
      tl_backend::HostVertex& host = b->host_vertex[{owner, stride, layout}];
      if (!host.buffer || host.content != content) {
        size_t count = it->second.bytes.size() / stride;
        std::vector<uint8_t> converted(it->second.bytes.begin(),
                                       it->second.bytes.begin() + count * stride);
        for (size_t v = 0; v < count; ++v) {
          for (uint32_t c : colours) {
            if (c + 4 <= stride) conv::VertexColourD3DToGl(&converted[v * stride + c]);
          }
        }
        if (blend &&
            !conv::BlendLanesForHost(converted.data(), converted.size(), stride, index_offset,
                                     weight_offset, weight_components,
                                     d->skinning.influence_count, d->skinning.index_lane,
                                     d->skinning.weight_lane)) {
          return TL_SKIP_BAD_RANGE;
        }
        // The first content of a guest buffer goes to a static buffer; once it changes, to a
        // dynamic one rewritten in place while the size holds.
        const bool rewritten = host.content != 0;
        if (!host.buffer || host.buffer->getNumVertices() != count ||
            (rewritten && host.buffer->getUsage() != Ogre::HBU_CPU_TO_GPU)) {
          host.buffer = mgr.createVertexBuffer(
              stride, count, rewritten ? Ogre::HBU_CPU_TO_GPU : Ogre::HBU_GPU_ONLY);
          host.serial = ++b->host_buffer_serial;
          ++b->counters.gpu_buffers_created;
        }
        host.buffer->writeData(0, converted.size(), converted.data(), true);
        host.content = content;
      }
      vertex_data.vertexBufferBinding->setBinding(Ogre::ushort(s), host.buffer);
      bound[s] = host.serial;
    }
    if (bound != layout.bound) {
      layout.bound = bound;
      Ogre::VertexDeclaration* decl = vertex_data.vertexDeclaration;
      if (decl->getElementCount() > 0) {
        const Ogre::VertexElement e = *decl->getElement(0);
        decl->modifyElement(0, e.getSource(), e.getOffset(), e.getType(), e.getSemantic(),
                            e.getIndex());  // unchanged: only marks the VAO for re-specification
      }
    }
    vertex_data.vertexStart = d->vertex_start;
    vertex_data.vertexCount = d->vertex_count;

    Ogre::IndexData index_data;
    Ogre::RenderOperation op;
    op.operationType = op_type;
    op.vertexData = &vertex_data;
    op.useIndexes = d->index_buffer != 0;
    if (op.useIndexes) {
      auto it = b->index_buffers.find(d->index_buffer);
      if (it == b->index_buffers.end()) return TL_SKIP_MISSING_BUFFER;
      const uint64_t owner = d->index_owner ? d->index_owner : d->index_buffer;
      tl_backend::HostIndex& host = b->host_index[owner];
      size_t count = it->second.bytes.size() / it->second.index_size;
      const auto type = it->second.index_size == 4 ? Ogre::HardwareIndexBuffer::IT_32BIT
                                                   : Ogre::HardwareIndexBuffer::IT_16BIT;
      if (!host.buffer || host.content != d->index_buffer) {
        const bool rewritten = host.content != 0;
        if (!host.buffer || host.buffer->getNumIndexes() != count ||
            host.buffer->getType() != type ||
            (rewritten && host.buffer->getUsage() != Ogre::HBU_CPU_TO_GPU)) {
          host.buffer = mgr.createIndexBuffer(
              type, count, rewritten ? Ogre::HBU_CPU_TO_GPU : Ogre::HBU_GPU_ONLY);
          ++b->counters.gpu_buffers_created;
        }
        host.buffer->writeData(0, count * it->second.index_size, it->second.bytes.data(), true);
        host.content = d->index_buffer;
      }
      if (size_t(d->index_start) + d->index_count > count) return TL_SKIP_BAD_RANGE;
      index_data.indexBuffer = host.buffer;
      index_data.indexStart = d->index_start;
      index_data.indexCount = d->index_count;
      op.indexData = &index_data;
    }

    // Transform: guest clip as the host projection, or (programmed draws with a known
    // projection) guest world-view as the host world so lighting and fog have a view space.
    ProgramInputs ff;
    float guest_clip[16];  // the position transform
    float guest_projection[16];
    float world_view[16];
    bool view_space = false;
    if (d->fixed_function) {
      conv::GuestFixedFunctionClip(d->world, d->view, d->projection, guest_clip);
    } else {
      for (int i = 0; i < 16; ++i) guest_clip[i] = d->guest_wvp[i];
    }
    if (!d->fixed_function && d->has_guest_projection &&
        conv::GuestWorldViewFromWvp(d->guest_wvp, d->guest_projection, world_view,
                                    guest_projection)) {
      view_space = true;
    } else {
      for (int i = 0; i < 16; ++i) guest_projection[i] = guest_clip[i];
    }
    // Half a guest pixel (RenderScale): the guest's geometry is laid out in its own pixels.
    const int viewport_width = b->current->viewport_width;
    const int viewport_height = b->current->viewport_height;
    float gl[16];
    conv::D3DPixelCentresToGl(guest_clip, viewport_width, viewport_height);
    conv::GuestClipToGl(guest_clip, gl);
    ff.clip = ToMatrix(gl);
    conv::D3DPixelCentresToGl(guest_projection, viewport_width, viewport_height);
    conv::GuestClipToGl(guest_projection, gl);
    ff.projection = ToMatrix(gl);
    if (view_space) ff.world = ToMatrix(world_view);
    ToOgre(b->state.alpha_func, ff.alpha_function);
    ff.alpha_reference = b->state.alpha_ref;
    if (skin) {
      ff.skin_bones = d->skinning.bone_count;
      ff.skin_weights = d->skinning.influence_count;
      ff.skin_palette = d->skinning.bones;
    }

    // Colour before texturing.
    bool wants_view_space = false;
    if (d->colour_source == TL_COLOUR_LIGHTING) {
      const tl_lighting& l = d->lighting;
      // GuestLighting: the derived scene colour (emissive = the guest's scene colour, alpha from
      // the diffuse) plus the light's diffuse (the guest's derived diffuse), not clamped.
      ff.lighting = true;
      ff.tracking = l.diffuse_from_vertex ? Ogre::TVC_DIFFUSE : Ogre::TVC_NONE;
      ff.diffuse = Ogre::ColourValue(1, 1, 1, l.scene_colour[3]);
      ff.emissive = Ogre::ColourValue(l.scene_colour[0], l.scene_colour[1], l.scene_colour[2],
                                      l.scene_colour[3]);
      // The program has the light the guest's has whether or not the view space is known (the
      // same program then serves the loading screens, where it is not, and the game); without it
      // the light gets a neutral value (no diffuse).
      if (l.light_count > 0) {
        wants_view_space = true;
        ff.light = true;
        if (view_space) {
          for (int i = 0; i < 3; ++i) ff.towards_light[i] = l.towards_light_view[i];
          ff.light_diffuse =
              Ogre::ColourValue(l.light_diffuse[0], l.light_diffuse[1], l.light_diffuse[2]);
        } else {
          ff.light_diffuse = Ogre::ColourValue::Black;
        }
      }
    }
    if (d->fog.enabled) {
      // Same for fog: always in the program; without the view space a range whose factor
      // saturates to exactly 1 ((end - d) / (end - start) = 2 for any distance in the scene).
      wants_view_space = true;
      ff.fog = true;
      ff.fog_colour =
          Ogre::ColourValue(d->fog.colour[0], d->fog.colour[1], d->fog.colour[2], d->fog.colour[3]);
      if (view_space) {
        conv::LinearFogRangeForHost(d->fog.end, d->fog.inverse_range, &ff.fog_start, &ff.fog_end);
      } else {
        ff.fog_start = 0.5e30f;
        ff.fog_end = 1e30f;
      }
    }
    if (wants_view_space && !view_space) b->notes |= TL_NOTE_NO_VIEW_SPACE;
    if (!ff.lighting && d->colour_source == TL_COLOUR_VERTEX) {
      // Unlit vertex colour: the program reads it only with diffuse tracking.
      for (uint32_t i = 0; i < d->element_count; ++i) {
        if (d->elements[i].semantic == 4) ff.tracking = Ogre::TVC_DIFFUSE;
      }
    }

    UnitDesc units[TL_MAX_STAGES];
    uint32_t unit_count =
        DescribeUnits(b, d, view_space ? world_view : nullptr, units);
    if (!ApplyProgram(b, ff, units, unit_count)) return TL_SKIP_NO_PROGRAM;

    // OGRE GL binds texture coordinate arrays only for units below this bound (the scene manager
    // normally sets it per pass).
    size_t units_in_use = 0;
    for (uint32_t u = 0; u < 32; ++u)
      if (b->enabled_units & (1u << u)) units_in_use = u + 1;
    b->rs->_disableTextureUnitsFrom(units_in_use);
    const auto strip = SceneClip16x9(b);
    if (strip) b->rs->setScissorTest(true, *strip);
    b->rs->_render(op);
    if (strip) b->rs->setScissorTest(false);
    b->probe_samples = 0;
    if (b->probe && b->current == &b->main) {
      if (!b->query) b->query = b->rs->createHardwareOcclusionQuery();
      SetColourBlend(b, Ogre::SBF_ONE, Ogre::SBF_ZERO, 0);
      b->rs->_setDepthBufferParams(false, false);
      // The probe runs on the main target, which is scaled.
      b->rs->setScissorTest(true, Ogre::Rect(b->scale.Host(b->probe_rect[0]),
                                             b->scale.Host(b->probe_rect[1]),
                                             b->scale.Host(b->probe_rect[2]),
                                             b->scale.Host(b->probe_rect[3])));
      b->query->beginOcclusionQuery();
      b->rs->_render(op);
      b->query->endOcclusionQuery();
      unsigned int samples = 0;
      b->query->pullOcclusionQuery(&samples);
      b->probe_samples = samples;
      b->rs->setScissorTest(false);
      tl_backend_set_state(b, &b->state);
    }
  } catch (Ogre::Exception&) {
    return TL_SKIP_OGRE_EXCEPTION;
  }
  return TL_DRAWN;
}

// A draw that generated a program is timed whole: the driver may compile or link on first use.
int tl_backend_draw(tl_backend* b, const tl_draw* d) {
  const uint32_t generated = b->counters.programs_generated;
  const auto start = std::chrono::steady_clock::now();
  const int result = Draw(b, d);
  if (b->counters.programs_generated != generated) {
    b->counters.program_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  }
  return result;
}

void tl_backend_take_counters(tl_backend* b, tl_backend_counters* out) {
  *out = b->counters;
  b->counters = {};
}

uint32_t tl_backend_draw_notes(tl_backend* b) { return b->notes; }

void tl_backend_set_probe(tl_backend* b, int enabled, uint32_t left, uint32_t top, uint32_t right,
                          uint32_t bottom) {
  b->probe = enabled != 0;
  b->probe_rect[0] = left;
  b->probe_rect[1] = top;
  b->probe_rect[2] = right;
  b->probe_rect[3] = bottom;
}

uint32_t tl_backend_probe_samples(tl_backend* b) { return b->probe_samples; }

int tl_backend_read_rgba(tl_backend* b, void* rgba, uint32_t stride) {
  try {
    ReadTarget(b->output, rgba, stride);
  } catch (Ogre::Exception&) {
    return 1;
  }
  return 0;
}

int tl_backend_read_target_rgba(tl_backend* b, uint64_t target_id, uint32_t width,
                                uint32_t height, void* rgba, uint32_t stride) {
  auto it = b->targets.find(target_id);
  if (it == b->targets.end()) return 1;
  if (width != it->second.guest_width || height != it->second.guest_height) return 1;
  try {
    ReadTarget(it->second, rgba, stride);
  } catch (Ogre::Exception&) {
    return 1;
  }
  return 0;
}

}  // extern "C"
