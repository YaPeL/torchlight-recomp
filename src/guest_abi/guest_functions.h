// Guest functions hooked outside the RenderSystem vtable, with their evidence.
//
// RenderSystem slot implementations are listed in ogre_layout.h (kRenderSystemSlots).

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::functions {

struct GuestFunction {
  uint32_t address;
  Confidence confidence;
};

// [confirmed] Xbox D3D device swap: the only caller of the VdSwap import (bl 0x8302638C at
// 0x821F3488). Called once per presented frame; the menu issues several _endFrame per swap.
inline constexpr GuestFunction kDeviceSwap{0x821F31C8, Confidence::kConfirmed};

// Xbox D3D: one step of the wait for the GPU's progress (r3: the wait state, xbox_d3d::gpu_wait;
// returns 1 to keep waiting, 0 to stop). [confirmed] 0x82774170: a pause of 32 db16cyc (a no-op
// when recompiled, @0x82774190..@0x827741B0), then: if bit 0x2 of device+11069 is set, return 0
// (@0x827741B8..@0x827741C0); read the GPU's progress word *(device+11024) (@0x827741C4,
// @0x827741D0) and, when it differs from the state's last value (+8, @0x827741CC), store it and
// the time (+8, +12; @0x827741E4, @0x827741E8); after 5000 time units without progress, call
// 0x8277F958 (@0x82774228); else return 1. Its callers loop on it until the progress word passes
// a target, e.g. 0x821A5C10 (@0x821A5CA4, condition @0x821A5CB0..@0x821A5CC8), called by
// kDeviceSwap: no caller counts the steps or depends on how long the pause takes, and they run it
// only while the target is not reached, so the step may sleep (hooks/gpu_wait_hooks.cpp).
inline constexpr GuestFunction kGpuProgressWaitStep{0x82774170, Confidence::kConfirmed};

// Resource constructors and destructors (this in r3). Each constructor has one caller, the
// creating factory, so every instance passes through them.
// [confirmed] D3D9HardwareVertexBuffer ctor: stores vtable 0x8200118C (@0x82582A84); called
// only by D3D9HardwareBufferManagerBase::createVertexBuffer @0x82580FE4.
inline constexpr GuestFunction kD3D9VertexBufferCtor{0x825829F8, Confidence::kConfirmed};
// [confirmed] D3D9HardwareVertexBuffer dtor: stores vtable 0x8200118C (@0x82582BD0); called by
// the deleting dtor (vtable slot 2, 0x82582B60 @0x82582B7C).
inline constexpr GuestFunction kD3D9VertexBufferDtor{0x82582BB8, Confidence::kConfirmed};
// [confirmed] D3D9HardwareIndexBuffer ctor: stores vtable 0x820F6914 (@0x82581F58); called only
// by createIndexBuffer @0x825810C8.
inline constexpr GuestFunction kD3D9IndexBufferCtor{0x82581EB8, Confidence::kConfirmed};
// [confirmed] D3D9HardwareIndexBuffer dtor: stores vtable 0x820F6914 (@0x825820A8).
inline constexpr GuestFunction kD3D9IndexBufferDtor{0x82582090, Confidence::kConfirmed};
// [confirmed] D3D9Texture ctor: calls the Texture ctor and stores vtable 0x820049F4
// (@0x8257A44C); called only by sub_82583650 @0x825836A0.
inline constexpr GuestFunction kD3D9TextureCtor{0x8257A400, Confidence::kConfirmed};
// [confirmed] D3D9Texture dtor: stores vtable 0x820049F4 (@0x8257A5C4).
inline constexpr GuestFunction kD3D9TextureDtor{0x8257A5B0, Confidence::kConfirmed};
// [confirmed] D3D9VertexDeclaration ctor: stores vtables 0x820F6640/0x820F6674 (@0x825806D8);
// called only by D3D9HardwareBufferManagerBase::createVertexDeclarationImpl @0x82581190.
inline constexpr GuestFunction kD3D9VertexDeclarationCtor{0x82580660, Confidence::kConfirmed};
// [confirmed] D3D9VertexDeclaration deleting dtor (vtable slot 0; stores the vtables
// @0x8258076C).
inline constexpr GuestFunction kD3D9VertexDeclarationDtor{0x82580758, Confidence::kConfirmed};

// [confirmed] D3D9HardwareBufferManagerBase::createVertexBuffer / createIndexBuffer (manager
// vtable slots 10 and 11): r3 = return slot, r4 = this, r5.. = arguments.
inline constexpr GuestFunction kCreateVertexBuffer{0x82580F68, Confidence::kConfirmed};
inline constexpr GuestFunction kCreateIndexBuffer{0x82581058, Confidence::kConfirmed};

// [confirmed] HardwareBuffer::lock / unlock (buffer vtable slots 3 and 5), shared by every
// buffer class that does not override them (all guest vertex/index buffers do not).
// lock: r3 = this, r4 = offset, r5 = length, r6 = LockOptions, passed on to lockImpl (vtable slot 0,
// @0x821A794C). unlock calls unlockImpl (vtable slot 1, @0x821A7B10); pixel buffers use the same
// unlock.
// [confirmed] D3D9HardwareVertexBuffer::unlockImpl 0x821A7B30 (VB vtable 0x8200118C slot 1): with a
// system memory buffer (+0x60) it copies it to the device buffer (sub_821A71E8 @0x821A7BA8) only
// when that resource was used in the previous frame (@0x821A7B90); otherwise D3D9 copies it when
// the buffer is next bound for drawing. Without one, the device buffer was locked directly and is
// unlocked here (@0x821A7BD8).
inline constexpr GuestFunction kHardwareBufferLock{0x821A7928, Confidence::kConfirmed};
inline constexpr GuestFunction kHardwareBufferUnlock{0x821A7AF0, Confidence::kConfirmed};

// [confirmed] D3D9Texture::loadImpl(IDirect3DDevice9*) (references "D3D9Texture::loadImpl"
// @0x8257ADB0): r3 = this. Render targets (usage bit 0x200) go to createInternalResources
// (@0x8257AB94). After it returns the texture has its name, size and format.
inline constexpr GuestFunction kD3D9TextureLoadImpl{0x8257AB70, Confidence::kConfirmed};
// [confirmed] D3D9HardwarePixelBuffer::blitFromMemory (references its name @0x8258734C): r3 = this.
// Writes the surface without lock/unlock.
inline constexpr GuestFunction kD3D9PixelBufferBlitFromMemory{0x82586FD8,
                                                             Confidence::kConfirmed};
// [confirmed] D3D9HardwarePixelBuffer::blitToMemory (the only function that loads its name,
// 0x820F7178, e.g. @0x82587548's exception paths): r3 = this. Reads the surface back to memory.
inline constexpr GuestFunction kD3D9PixelBufferBlitToMemory{0x82587548, Confidence::kConfirmed};

// [confirmed] The guest's graphics interrupt callback, registered with
// VdSetGraphicsInterruptCallback (lis @0x82780248 / addi @0x82780254 -> 0x82775610, call
// @0x82780258): r3 = source, r4 = device. Source 1 (command processor interrupt, cmplwi r3,1 at
// entry) calls the device's notification callback; source 0 (vblank, @0x8277567C) reads the
// vblank status register and runs the vblank handler 0x82774B28 (vblank count, pending flips).
inline constexpr GuestFunction kGraphicsInterruptCallback{0x82775610, Confidence::kConfirmed};
inline constexpr uint32_t kInterruptSourceVblank = 0;
inline constexpr uint32_t kInterruptSourceCommand = 1;

// [confirmed] RTSS ProgramManager::createGpuProgram (OgreShaderProgramManager.cpp:366, with a
// Runic hash cache): appends "_VS"/"_FS" to the source hash (@0x823F715C, @0x823F7170), then sets
// "entry_point" (@0x823F7554), "target" for language "hlsl" (@0x823F7588, @0x823F75EC),
// "column_major_matrices" (@0x823F763C) and "profiles" (@0x823F7684). r3 = returned
// GpuProgramPtr (null pRep on a compile error, @0x823F7474), r4 = this, r5 = Program*,
// r6 = ProgramWriter*, r7 = language (String*), r8 = profiles, r9 = profile list, r10 = cache
// path. The created program is the high-level one; the program bound for a draw is its
// assembler program, created under the same name (OgreD3D9HLSLProgram.cpp:234).
inline constexpr GuestFunction kRtssCreateGpuProgram{0x823F7068, Confidence::kConfirmed};

// Gamma ramp. D3D writes the display LUT into the command buffer on every present that needs it:
// the device swap 0x821F31C8 calls sub_82774DF8 (@0x821F36C8), which calls sub_82774AB8
// (@0x82774FC0; device, front buffer format, D3DGAMMARAMP* or null for a linear ramp). That one
// picks the PWL writer for format 0x28280136 and the 256-entry writer otherwise
// (@0x82774B04..@0x82774B1C). Both writers: r3 = device, r4 = ramp (0x600 bytes, see
// ogre_layout.h d3d_gamma_ramp).
// [confirmed] 256-entry table: DC_LUT_RW_MODE = 0 (@0x8277652C), DC_LUT_WRITE_EN_MASK = 7
// (@0x8277653C), then 256 x DC_LUT_30_COLOR (0x1925 @0x82776570) from 16-bit R/G/B >> 6.
inline constexpr GuestFunction kGammaRampWriteTable{0x82776500, Confidence::kConfirmed};
// [confirmed] 128-entry PWL: DC_LUT_RW_MODE = 1 (@0x8277663C), 3 x DC_LUT_PWL_DATA (0x1924
// @0x82776670) per entry, each (delta << 16) | base for R, G, B (@0x82776680..@0x827766AC).
inline constexpr GuestFunction kGammaRampWritePwl{0x827765F8, Confidence::kConfirmed};

// [confirmed] Shared `li r3,0; blr` used as RenderTarget::requiresTextureFlipping (vtable slot 40)
// by both concrete guest targets: D3D9RenderWindow (vtable 0x82003FDC) and D3D9RenderTexture
// (vtable 0x8200394C). Reading the slot instead of calling it tells the answer without running
// guest code.
inline constexpr GuestFunction kReturnFalse{0x828AD748, Confidence::kConfirmed};

// XDK runtime wrappers that block a guest thread.
// [confirmed] WaitForSingleObjectEx(handle r3, milliseconds r4, alertable r5): converts r4 with
// sub_82885500 and calls the NtWaitForSingleObjectEx import, again while it returns 257 (alerted)
// and r5 is set. WaitForSingleObject is sub_8287E968 (r5 = 0, then this).
inline constexpr GuestFunction kWaitForSingleObjectEx{0x82882B58, Confidence::kConfirmed};
// [inferred] SleepEx(milliseconds r3, alertable r4): the only caller of the KeDelayExecutionThread
// import; Sleep is sub_8287D878 (r4 = 0) and sub_8287D840 passes r4 & 0xFF.
inline constexpr GuestFunction kSleepEx{0x82883C50, Confidence::kInferred};
// [confirmed] Sleep(milliseconds r3): sets r4 = 0 and calls kSleepEx.
inline constexpr GuestFunction kSleep{0x8287D878, Confidence::kConfirmed};
// [confirmed] WriteFile(handle r3, buffer r4, length r5, written r6, overlapped r7): calls the
// NtWriteFile import, then NtWaitForSingleObjectEx on the handle while it returns 259 (pending).
inline constexpr GuestFunction kWriteFile{0x82882DB0, Confidence::kConfirmed};
// [inferred] ReadFile: same shape and arguments as kWriteFile, with the system call made through
// a table (the pointer at 0x8304ECB8, entry +16) instead of an import; its callers pass read
// buffers.
inline constexpr GuestFunction kReadFile{0x8287F408, Confidence::kInferred};
// [confirmed] Xbox D3D internal waits: the callers of the KeWaitForSingleObject import
// (0x827359E0, 0x82762190, 0x8277E840) and of KeWaitForMultipleObjects (0x82735170, 0x827388B8)
// in the D3D runtime's address range; arguments not decoded.
inline constexpr GuestFunction kD3DWaitA{0x827359E0, Confidence::kConfirmed};
inline constexpr GuestFunction kD3DWaitB{0x82762190, Confidence::kConfirmed};
inline constexpr GuestFunction kD3DWaitC{0x8277E840, Confidence::kConfirmed};
inline constexpr GuestFunction kD3DWaitMultipleA{0x82735170, Confidence::kConfirmed};
inline constexpr GuestFunction kD3DWaitMultipleB{0x827388B8, Confidence::kConfirmed};
// [confirmed] Thunks of the XAM content imports (each holds only that call).
inline constexpr GuestFunction kXamContentCreateEx{0x8287E548, Confidence::kConfirmed};
inline constexpr GuestFunction kXamContentDelete{0x8287E5D0, Confidence::kConfirmed};
inline constexpr GuestFunction kXamContentClose{0x8287E5D8, Confidence::kConfirmed};
inline constexpr GuestFunction kXamContentFlush{0x8287E5E0, Confidence::kConfirmed};

// The guest's video mode (Runic's Xbox D3D9 render system). Its frame size is not the console's
// video mode: [confirmed] both the driver detection (sub_82583D90, between the "D3D9: Driver
// Detection Starts/Ends" logs) and the video mode list (sub_82583070) call XGetVideoMode, read only
// its fIsWideScreen (+0xC, @0x82583E2C / @0x825830C8), and build a mode descriptor {width, height,
// refresh, format 0x18280186} with height 720 and width 1280 when widescreen, else 960
// (@0x82583E30..@0x82583E4C, @0x825830CC..@0x825830E8). The back buffer, viewports and the camera's
// aspect follow that mode (a 4:3 video mode gives a 960x720 frame).
namespace video_mode {
inline constexpr uint32_t kTableHeight = 720;
inline constexpr uint32_t kTableWidthWide = 1280;
inline constexpr uint32_t kTableWidthNarrow = 960;
// [confirmed] Descriptor fields.
inline constexpr uint32_t kDescriptorWidth = 0x0;
inline constexpr uint32_t kDescriptorHeight = 0x4;
}  // namespace video_mode
// [confirmed] Adds a mode descriptor (r4) to the video mode list (r3); only caller sub_82583070
// (@0x8258312C), with its widescreen-table descriptor.
inline constexpr GuestFunction kVideoModeListAdd{0x82583228, Confidence::kConfirmed};
// [confirmed] Copies a D3D9 driver record (0x5A0 bytes) from r4 to r3: the driver detection pushes
// its new record with it (@0x82583F30) and the driver vector's growth moves records with it
// (sub_825840B8). The record holds the detection's mode descriptor at +0x588 (copied there from
// the stack @0x82583E80..@0x82583EB0: descriptor at sp+0x658, record at sp+0xD0).
inline constexpr GuestFunction kD3D9DriverCopy{0x825841A8, Confidence::kConfirmed};
inline constexpr uint32_t kD3D9DriverDesktopMode = 0x588;
// [confirmed] Runic's D3D9 render system picks its OGRE "Video Mode" option by name, the literal
// of the widescreen mode (else "960 x 720 @ 32-bit colour" at 0x820F46BC) chosen by the same
// fIsWideScreen: in initConfigOptions (sub_8256E848 @0x8256EA90), refreshD3DSettings
// (sub_8256F210 @0x8256F4D4) and setConfigOption (sub_8256F510 @0x8256F6AC), its only users. The
// mode list names its modes from their size ("%d x %d @ %d-bit colour", 0x820F6C00, in
// D3D9VideoMode::getDescription sub_82584238), and initialise (sub_8256FEA0) parses the option's
// text into the window's width and height (find '@' and ' ' @0x825703E0/@0x82570420,
// StringConverter::parseInt sub_824394E0 @0x82570488/@0x825704BC), as OGRE 1.7 does
// (OgreD3D9RenderSystem.cpp:563-576). The literal lives in read-only data.
inline constexpr uint32_t kWideModeName = 0x820F46A0;
inline constexpr char kWideModeNameText[] = "1280 x 720 @ 32-bit colour";
inline constexpr char kModeNameFormat[] = "%u x %u @ 32-bit colour";

// The Xbox D3D's EDRAM bookkeeping (XDK runtime). [confirmed] CreateRenderTarget(width r3,
// height r4, D3D format r5, multisample r6, D3DSURFACE_PARAMETERS* r7): without parameters it asks
// its EDRAM allocator for the surface's tiles (@0x8276A8F8) and fails (frees the surface, returns
// NULL) when the allocator has no room or the range ends past 0x800 tiles (10 MB, @0x8276A918,
// freeing the range @0x8276A928); only then does it mark the surface as owning EDRAM (bit
// 0x80000000 of +0, @0x8276A938). With parameters it places the surface at their Base (+0,
// HierarchicalZBase +4, ColorExpBias +8, read @0x8276A35C..@0x8276A364) without asking the
// allocator (@0x8276A8C8), the XDK's way to alias EDRAM. The only other EDRAM release is the
// surface destructor sub_8276B848, and only for surfaces with that bit (@0x8276B8C8..@0x8276B8E4).
inline constexpr GuestFunction kD3DCreateRenderTarget{0x8276A858, Confidence::kConfirmed};
inline constexpr GuestFunction kEdramAllocate{0x82776A00, Confidence::kConfirmed};
inline constexpr GuestFunction kEdramFree{0x827769B0, Confidence::kConfirmed};
inline constexpr uint32_t kCreateRenderTargetAfterAllocate = 0x8276A8FC;
inline constexpr uint32_t kCreateRenderTargetAfterOverLimitFree = 0x8276A92C;
inline constexpr uint32_t kSurfaceParametersSize = 0x10;

// The game's scene views. [confirmed] CreateViewports (virtual, this in r3; logs "CreateViewports
// AspectRatio message"): adds the scene viewport for the camera of this+0x1C's +8 (@0x8220A4AC,
// RenderTarget::addViewport through vt+0x2C of this+0x4C) and keeps it at this+0x48 (@0x8220A514),
// then the UI viewport for its +0xC camera (scheme "SMUIShaderScheme"). The menus draw their 3D
// scene in that scene viewport, and the level does too (one address in captures of both).
inline constexpr GuestFunction kCreateViewports{0x8220A470, Confidence::kConfirmed};
inline constexpr uint32_t kViewsSceneViewport = 0x48;
// [confirmed] Whether the game is in a level with its player (sub_82374D98, which the rich presence
// uses to report "frontend" otherwise, @0x82194468): the CGameUI instance (0x835594F8) +0x38, +0x60,
// +0xC, +0x170, then +0x1404 == 1.
inline constexpr uint32_t kInLevelChain[] = {0x38, 0x60, 0xC, 0x170};
inline constexpr uint32_t kInLevelFlag = 0x1404;

// The guest's memory copies (dst r3, src r4, size r5; return the dst in r3). Both copy front to
// back with plain loads and stores and cache hints (dcbt, dcbf; no dcbz), so a copy between ranges
// that do not overlap is exactly a host memcpy (hooks/guest_copy.h). No caller reads a volatile
// register other than r3 after either returns (every call site in generated/default checked).
// [confirmed] CRT memcpy 0x82860A50: saves r3 (@0x82860A50) and reloads it before every return
// (@0x82860B24, @0x82860CE8, @0x82860DF4); aligns the destination to 8 with byte or word copies
// (@0x82860A88, @0x82860AA0), then copies by doublewords (@0x82860AF4, unrolled by 128 bytes at
// @0x82860BEC), words (@0x82860CC0, @0x82860D70) or bytes (@0x82860DE8, @0x82860E78) by the
// source's alignment, then the tail bytes. Size 0 touches no memory (@0x82860ADC..@0x82860B24).
inline constexpr GuestFunction kMemcpy{0x82860A50, Confidence::kConfirmed};
// [confirmed] Large copy 0x821A7138 (XMemCpy-like): below 256 bytes it is kMemcpy (@0x821A7164);
// else kMemcpy up to a 128-byte aligned destination (@0x821A7184), whole 128-byte blocks by the
// vector loops 0x82884644 (unaligned source, @0x821A71B4) or 0x82884320 (16-byte aligned source,
// @0x821A71BC), the tail by kMemcpy (@0x821A71D4), and returns the destination (mr r3,r27
// @0x821A71D8). Its one direct caller (0x821A71E8 @0x821A7270) copies into a buffer it has just
// locked.
inline constexpr GuestFunction kLargeCopy{0x821A7138, Confidence::kConfirmed};

// Xbox D3D device calls of the D3D9 render system's _render (0x821C4058) that only feed the Xenos
// GPU (hooks/guest_d3d_skip.h; evidence in docs/native-skip-guest-d3d.md, "Draw and bindings").
// [confirmed] DrawIndexedPrimitive: flushes the dirty device state into the ring (0x821CFF38),
// allocates ring space and fences (0x821EA6F0, 0x821F3D78), waits for ring space (0x821A5C10);
// called only by _render (@0x821C43F4). DrawPrimitive: the same without indices (@0x821C4490).
inline constexpr GuestFunction kD3DDrawIndexed{0x821CF830, Confidence::kConfirmed};
inline constexpr GuestFunction kD3DDraw{0x821D0A10, Confidence::kConfirmed};
// [confirmed] SetIndices (device in r3, buffer in r4): stores the buffer at device+12684
// (@0x821C3A7C); the buffer it replaces gets the current fence in its +8 (@0x821C3A24) or a
// pending fence entry (@0x821C3A74). Callers: _render (@0x821C4368, @0x821C4500) and 0x827746B0.
inline constexpr GuestFunction kD3DSetIndices{0x821C39F8, Confidence::kConfirmed};
// [confirmed] SetStreamSource (device r3, stream r4, buffer r5, offset r6, stride r7): writes the
// stream's vertex fetch constant and dirty bit (@0x821C3DB4..@0x821C3DC4), stores the buffer at
// device+4*(stream+3177) (@0x821C3E44) and its stride (@0x821C3E50); the replaced buffer gets its
// fence like SetIndices (@0x821C3DE8, @0x821C3E38). Callers: setVertexBufferBinding 0x821C3E78,
// _render (unbinding), the device's unbind-all 0x821CECF0 (from _beginFrame) and 0x827746B0.
inline constexpr GuestFunction kD3DSetStreamSource{0x821C3D58, Confidence::kConfirmed};

}  // namespace torchlight::guest_abi::functions
