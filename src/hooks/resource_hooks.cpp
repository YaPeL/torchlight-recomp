// Resource lifetime and frame boundary overrides.
//
// Constructors register a guest object (new generation at its address) after they ran, so the
// object's fields are initialised; destructors unregister it before they run. Each constructor
// has a single caller (its factory), so every instance is seen; this also covers the
// HardwareBufferManager create slots 10/11, which are those factories.
//
// Buffer and pixel buffer writes (lock/unlock, blitFromMemory) bump a content version, and
// texture loads are announced, for the live mode's one-copy-per-version snapshots.
//
// The display gamma ramp D3D writes when it presents is kept as state (in every capture's
// baseline).
//
// RTSS program creation records each program's source once, by name, so a capture taken later
// can carry the source of the programs it binds.

#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "capture/session.h"
#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_enums.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/video_mode_hooks.h"
#include "live/frame_timing.h"

namespace {

using torchlight::capture::BufferInfo;
using torchlight::capture::Session;
using torchlight::commands::ProgramSource;
using torchlight::commands::ResourceKind;
namespace abi = torchlight::guest_abi;
namespace fn = torchlight::guest_abi::functions;
namespace ogre = torchlight::guest_abi::ogre;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

using torchlight::capture::Hook;
// Times the scope as `hook`'s recording cost (live mode and armed captures).
torchlight::capture::HookTimer Timed(Hook hook) {
  Session& s = Session::Get();
  return {s.hook_costs(), uint32_t(hook), s.measuring()};
}

}  // namespace

extern "C" {

// ---- vertex buffers --------------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kD3D9VertexBufferCtor, 825829F8);
REX_EXTERN(__imp__sub_825829F8);
REX_FUNC(sub_825829F8) {
  uint32_t self = ctx.r3.u32;
  __imp__sub_825829F8(ctx, base);
  auto timer = Timed(Hook::kResourceLifetime);
  BufferInfo info;
  info.element_size = abi::ReadU32(base, self, ogre::hardware_vertex_buffer::kVertexSize);
  info.count = abi::ReadU32(base, self, ogre::hardware_vertex_buffer::kNumVertices);
  info.usage = abi::ReadU32(base, self, ogre::hardware_buffer::kUsage);
  Session::Get().OnCreated(ResourceKind::kVertexBuffer, self, info);
}

FUNCTION_ADDRESS_CHECK(kD3D9VertexBufferDtor, 82582BB8);
REX_EXTERN(__imp__sub_82582BB8);
REX_FUNC(sub_82582BB8) {
  {
    auto timer = Timed(Hook::kResourceLifetime);
    Session::Get().OnDestroyed(ctx.r3.u32);
  }
  __imp__sub_82582BB8(ctx, base);
}

// ---- index buffers ---------------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kD3D9IndexBufferCtor, 82581EB8);
REX_EXTERN(__imp__sub_82581EB8);
REX_FUNC(sub_82581EB8) {
  uint32_t self = ctx.r3.u32;
  __imp__sub_82581EB8(ctx, base);
  auto timer = Timed(Hook::kResourceLifetime);
  BufferInfo info;
  info.element_size = abi::ReadU32(base, self, ogre::hardware_index_buffer::kIndexSize);
  info.count = abi::ReadU32(base, self, ogre::hardware_index_buffer::kNumIndexes);
  info.usage = abi::ReadU32(base, self, ogre::hardware_buffer::kUsage);
  Session::Get().OnCreated(ResourceKind::kIndexBuffer, self, info);
}

FUNCTION_ADDRESS_CHECK(kD3D9IndexBufferDtor, 82582090);
REX_EXTERN(__imp__sub_82582090);
REX_FUNC(sub_82582090) {
  {
    auto timer = Timed(Hook::kResourceLifetime);
    Session::Get().OnDestroyed(ctx.r3.u32);
  }
  __imp__sub_82582090(ctx, base);
}

// ---- textures --------------------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kD3D9TextureCtor, 8257A400);
REX_EXTERN(__imp__sub_8257A400);
REX_FUNC(sub_8257A400) {
  uint32_t self = ctx.r3.u32;
  __imp__sub_8257A400(ctx, base);
  auto timer = Timed(Hook::kResourceLifetime);
  Session::Get().OnCreated(ResourceKind::kTexture, self);
}

FUNCTION_ADDRESS_CHECK(kD3D9TextureDtor, 8257A5B0);
REX_EXTERN(__imp__sub_8257A5B0);
REX_FUNC(sub_8257A5B0) {
  {
    auto timer = Timed(Hook::kResourceLifetime);
    Session::Get().OnDestroyed(ctx.r3.u32);
  }
  __imp__sub_8257A5B0(ctx, base);
}

// ---- vertex declarations ---------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kD3D9VertexDeclarationCtor, 82580660);
REX_EXTERN(__imp__sub_82580660);
REX_FUNC(sub_82580660) {
  uint32_t self = ctx.r3.u32;
  __imp__sub_82580660(ctx, base);
  auto timer = Timed(Hook::kResourceLifetime);
  Session::Get().OnCreated(ResourceKind::kVertexDeclaration, self);
}

FUNCTION_ADDRESS_CHECK(kD3D9VertexDeclarationDtor, 82580758);
REX_EXTERN(__imp__sub_82580758);
REX_FUNC(sub_82580758) {
  {
    auto timer = Timed(Hook::kResourceLifetime);
    Session::Get().OnDestroyed(ctx.r3.u32);
  }
  __imp__sub_82580758(ctx, base);
}

// ---- RTSS programs ---------------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kRtssCreateGpuProgram, 823F7068);
REX_EXTERN(__imp__sub_823F7068);
REX_FUNC(sub_823F7068) {
  uint32_t result = ctx.r3.u32;
  uint32_t language = ctx.r7.u32;
  ProgramSource p;
  p.language = ogre::ReadString(base, language);  // read before the call reuses the registers
  __imp__sub_823F7068(ctx, base);
  auto timer = Timed(Hook::kCreateGpuProgram);
  uint32_t program = abi::ReadU32(base, result, ogre::shared_ptr::kPRep);
  if (program == 0) return;  // compile error
  p.name = ogre::ReadString(base, program + ogre::resource::kName.offset);
  Session& session = Session::Get();
  // Identity of the program the render system binds (its assembler program, same name): a new
  // generation when a different program now lives at that address.
  uint32_t assembler = abi::ReadU32(
      base, program + ogre::high_level_gpu_program::kAssemblerProgram.offset +
                ogre::shared_ptr::kPRep.offset);
  if (assembler != 0) session.OnProgramObject(assembler, p.name);
  if (session.HasProgramSource(p.name)) return;  // existing program returned by name
  p.source = ogre::ReadString(base, program + ogre::gpu_program::kSource.offset, 1u << 20);
  if (p.language == "hlsl") {
    p.entry_point =
        ogre::ReadString(base, program + ogre::d3d9_hlsl_program::kEntryPoint.offset);
    p.target = ogre::ReadString(base, program + ogre::d3d9_hlsl_program::kTarget.offset);
  }
  session.OnProgramCreated(std::move(p));
}

// ---- content versions --------------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kHardwareBufferLock, 821A7928);
REX_EXTERN(__imp__sub_821A7928);
REX_FUNC(sub_821A7928) {
  {
    auto timer = Timed(Hook::kLock);
    uint32_t buffer = ctx.r3.u32, options = ctx.r6.u32;
    Session& session = Session::Get();
    session.OnLock(buffer, options != ogre::lock_options::kReadOnly.value);
    // A lock that keeps the contents (anything but discard / no-overwrite) of a render target's
    // pixel buffer can read what the GPU rendered.
    if ((abi::ReadU32(base, buffer, ogre::hardware_buffer::kUsage) &
         ogre::texture_usage::kRenderTarget.value) &&
        options != ogre::lock_options::kDiscard.value &&
        options != ogre::lock_options::kNoOverwrite.value) {
      session.gpu_events().render_target_locks.fetch_add(1, std::memory_order_relaxed);
    }
  }
  __imp__sub_821A7928(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kHardwareBufferUnlock, 821A7AF0);
REX_EXTERN(__imp__sub_821A7AF0);
REX_FUNC(sub_821A7AF0) {
  uint32_t buffer = ctx.r3.u32;
  __imp__sub_821A7AF0(ctx, base);
  auto timer = Timed(Hook::kUnlock);
  Session::Get().OnUnlock(buffer);
}

FUNCTION_ADDRESS_CHECK(kD3D9PixelBufferBlitFromMemory, 82586FD8);
REX_EXTERN(__imp__sub_82586FD8);
REX_FUNC(sub_82586FD8) {
  uint32_t buffer = ctx.r3.u32;
  __imp__sub_82586FD8(ctx, base);
  auto timer = Timed(Hook::kBlitFromMemory);
  Session::Get().OnContentWritten(buffer);
}

FUNCTION_ADDRESS_CHECK(kD3D9PixelBufferBlitToMemory, 82587548);
REX_EXTERN(__imp__sub_82587548);
REX_FUNC(sub_82587548) {
  {
    auto timer = Timed(Hook::kBlitToMemory);
    if (abi::ReadU32(base, ctx.r3.u32, ogre::hardware_buffer::kUsage) &
        ogre::texture_usage::kRenderTarget.value) {
      Session::Get().gpu_events().render_target_blits_to_memory.fetch_add(
          1, std::memory_order_relaxed);
    }
  }
  __imp__sub_82587548(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kD3D9TextureLoadImpl, 8257AB70);
REX_EXTERN(__imp__sub_8257AB70);
REX_FUNC(sub_8257AB70) {
  uint32_t texture = ctx.r3.u32;
  __imp__sub_8257AB70(ctx, base);
  auto timer = Timed(Hook::kTextureLoad);
  Session::Get().OnTextureLoaded(base, texture);
}

// ---- display gamma ramp ----------------------------------------------------------------------
namespace {
void RecordGammaRamp(const uint8_t* base, uint32_t ramp, bool pwl) {
  torchlight::commands::SetGammaRamp c;
  c.pwl = pwl;
  for (uint32_t i = 0; i < c.values.size(); ++i) c.values[i] = abi::ReadU16(base, ramp + 2 * i);
  Session& s = Session::Get();
  s.set_membase(base);
  s.State(torchlight::capture::ShadowKey(torchlight::commands::CommandPayload(c)), c);
}
}  // namespace

FUNCTION_ADDRESS_CHECK(kGammaRampWriteTable, 82776500);
REX_EXTERN(__imp__sub_82776500);
REX_FUNC(sub_82776500) {
  RecordGammaRamp(base, ctx.r4.u32, false);
  __imp__sub_82776500(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kGammaRampWritePwl, 827765F8);
REX_EXTERN(__imp__sub_827765F8);
REX_FUNC(sub_827765F8) {
  RecordGammaRamp(base, ctx.r4.u32, true);
  __imp__sub_827765F8(ctx, base);
}

// ---- frame boundary --------------------------------------------------------------------------
FUNCTION_ADDRESS_CHECK(kDeviceSwap, 821F31C8);
REX_EXTERN(__imp__sub_821F31C8);
REX_FUNC(sub_821F31C8) {
  {
    auto timer = Timed(Hook::kSwap);
    torchlight::live::FrameTiming::Get().OnSwap();
    Session::Get().OnSwapBegin();
  }
  __imp__sub_821F31C8(ctx, base);
  auto timer = Timed(Hook::kSwap);
  Session& s = Session::Get();
  s.OnSwapEnd();
  // A host decision for the next frame's draws: where the front end's scene is clipped (only
  // sent when it changes).
  const torchlight::commands::SetSceneClip clip{torchlight::hooks::SceneClipViewport(base)};
  s.State(torchlight::capture::ShadowKey(torchlight::commands::CommandPayload(clip)), clip);
}

}  // extern "C"
