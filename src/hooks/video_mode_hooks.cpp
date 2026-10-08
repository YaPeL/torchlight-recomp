// The guest's frame size for display aspect ratios the game has no mode for (guest_abi
// video_mode): before the game's 16:9 mode descriptor reaches the video mode list or the D3D9
// driver record, its width and height are replaced with the frame for the video mode the SDK
// reports (FrameSizeForVideoMode); and since the render system picks and parses that mode by its
// name, the name literal is rewritten to match before the guest runs (InstallVideoMode). The back
// buffer, viewports and the camera's aspect follow from there. The game's own modes (16:9, 4:3)
// are left alone; if the name cannot be rewritten, nothing is. Only in the native mode: the guest
// D3D's EDRAM bookkeeping (10 MB) has no room for a wider back buffer next to the game's render
// targets, so a render target it rejects for lack of EDRAM is created again aliased at EDRAM base
// 0 (D3DSURFACE_PARAMETERS); with the null GPU no EDRAM exists and the GPU commands that would use
// the base are discarded. With Xenos the game keeps its 16:9.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"
#include "guest_abi/game_ui.h"
#include "hooks/video_mode.h"
#include "hooks/video_mode_hooks.h"
#include "live/install.h"

namespace {

namespace abi = torchlight::guest_abi;
namespace fn = torchlight::guest_abi::functions;
namespace vm = torchlight::guest_abi::functions::video_mode;
using torchlight::hooks::FrameSize;

#define FUNCTION_ADDRESS_CHECK(entry, addr) \
  static_assert(fn::entry.address == 0x##addr##u, "guest_functions mismatch")

// The frame for the video mode the SDK reports (its video_mode_width/height cvars), decided once.
const std::optional<FrameSize>& TargetFrame() {
  static const std::optional<FrameSize> frame = [] {
    const auto* w = rex::cvar::GetFlagInfo("video_mode_width");
    const auto* h = rex::cvar::GetFlagInfo("video_mode_height");
    if (!w || !h || !torchlight::live::OnlyMode()) return std::optional<FrameSize>();
    auto f = torchlight::hooks::FrameSizeForVideoMode(
        uint32_t(rex::cvar::Query<int32_t>("video_mode_width")),
        uint32_t(rex::cvar::Query<int32_t>("video_mode_height")));
    if (f) REXLOG_INFO("video mode: guest frame {}x{} instead of the game's 16:9", f->width,
                       f->height);
    return f;
  }();
  return frame;
}

// Set by InstallVideoMode once the mode's name was rewritten: the descriptors follow only then.
bool name_rewritten = false;
// D3DSURFACE_PARAMETERS in guest memory for aliased render targets (all fields 0: base 0).
uint32_t aliased_parameters = 0;
// Set when CreateRenderTarget, on this thread, was refused EDRAM.
thread_local bool edram_refused = false;

// Rewrites a descriptor of the game's 16:9 mode at `width_addr` / `height_addr`.
void Retarget(uint8_t* base, uint32_t width_addr, uint32_t height_addr) {
  const auto& frame = TargetFrame();
  if (!frame || !name_rewritten) return;
  if (abi::ReadU32(base, width_addr) != vm::kTableWidthWide ||
      abi::ReadU32(base, height_addr) != vm::kTableHeight) {
    return;  // the 4:3 mode, or already rewritten
  }
  abi::WriteU32(base, width_addr, frame->width);
  abi::WriteU32(base, height_addr, frame->height);
}

}  // namespace

namespace torchlight::hooks {

void InstallVideoMode() {
  const auto& frame = TargetFrame();
  if (!frame) return;
  char name[64];
  std::snprintf(name, sizeof(name), fn::kModeNameFormat, frame->width, frame->height);
  const std::string_view original(fn::kWideModeNameText);
  if (std::strlen(name) != original.size()) {
    REXLOG_ERROR("video mode: '{}' is not as long as '{}'; the guest keeps 16:9", name, original);
    return;
  }
  auto* memory = REX_KERNEL_MEMORY();
  uint8_t* membase = memory->virtual_membase();
  if (std::memcmp(membase + fn::kWideModeName, original.data(), original.size()) != 0) {
    REXLOG_ERROR("video mode: the mode name at {:08X} is not '{}'; the guest keeps 16:9",
                 fn::kWideModeName, original);
    return;
  }
  // Read-only data: open the page for the write, then restore its protection.
  auto* heap = memory->LookupHeap(fn::kWideModeName);
  uint32_t old_protect = 0;
  if (!heap || !heap->Protect(fn::kWideModeName, uint32_t(original.size()),
                              rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                              &old_protect)) {
    REXLOG_ERROR("video mode: cannot open {:08X} for writing; the guest keeps 16:9",
                 fn::kWideModeName);
    return;
  }
  aliased_parameters = memory->SystemHeapAlloc(fn::kSurfaceParametersSize);
  if (!aliased_parameters) {
    REXLOG_ERROR("video mode: no guest memory for surface parameters; the guest keeps 16:9");
    return;
  }
  std::memset(membase + aliased_parameters, 0, fn::kSurfaceParametersSize);
  abi::WriteBytes(membase, fn::kWideModeName, name, original.size());
  heap->Protect(fn::kWideModeName, uint32_t(original.size()), old_protect);
  name_rewritten = true;
  REXLOG_INFO("video mode: '{}' renamed '{}'", original, name);
}

bool WiderThan16x9() {
  const auto& frame = TargetFrame();
  return name_rewritten && frame && frame->width * 9 > frame->height * 16;
}

namespace {
uint32_t scene_viewport = 0;  // CreateViewports' scene viewport

bool InLevel(const uint8_t* base) {
  uint32_t p = abi::ReadU32(base, torchlight::guest_abi::game_ui::kGameUiGlobal);
  for (uint32_t offset : fn::kInLevelChain) {
    if (!p) return false;
    p = abi::ReadU32(base, p + offset);
  }
  return p && abi::ReadU32(base, p + fn::kInLevelFlag) == 1;
}
}  // namespace

void ViewsCreated(uint32_t viewport) { scene_viewport = viewport; }

uint32_t SceneViewport() { return scene_viewport; }

uint32_t SceneClipViewport(const uint8_t* base) {
  if (!scene_viewport || !WiderThan16x9() || InLevel(base)) return 0;
  return scene_viewport;
}

}  // namespace torchlight::hooks

extern "C" {

FUNCTION_ADDRESS_CHECK(kVideoModeListAdd, 82583228);
REX_EXTERN(__imp__sub_82583228);
REX_FUNC(sub_82583228) {
  const uint32_t descriptor = ctx.r4.u32;
  Retarget(base, descriptor + vm::kDescriptorWidth, descriptor + vm::kDescriptorHeight);
  __imp__sub_82583228(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kD3D9DriverCopy, 825841A8);
REX_EXTERN(__imp__sub_825841A8);
REX_FUNC(sub_825841A8) {
  const uint32_t mode = ctx.r4.u32 + fn::kD3D9DriverDesktopMode;
  Retarget(base, mode + vm::kDescriptorWidth, mode + vm::kDescriptorHeight);
  __imp__sub_825841A8(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kEdramAllocate, 82776A00);
REX_EXTERN(__imp__sub_82776A00);
REX_FUNC(sub_82776A00) {
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_82776A00(ctx, base);
  if (ctx.r3.u32 == 0 && caller == fn::kCreateRenderTargetAfterAllocate) edram_refused = true;
}

FUNCTION_ADDRESS_CHECK(kEdramFree, 827769B0);
REX_EXTERN(__imp__sub_827769B0);
REX_FUNC(sub_827769B0) {
  if (uint32_t(ctx.lr) == fn::kCreateRenderTargetAfterOverLimitFree) edram_refused = true;
  __imp__sub_827769B0(ctx, base);
}

FUNCTION_ADDRESS_CHECK(kD3DCreateRenderTarget, 8276A858);
REX_EXTERN(__imp__sub_8276A858);
REX_FUNC(sub_8276A858) {
  if (!name_rewritten || ctx.r7.u32 != 0) {
    __imp__sub_8276A858(ctx, base);
    return;
  }
  const uint32_t width = ctx.r3.u32, height = ctx.r4.u32, format = ctx.r5.u32,
                 multisample = ctx.r6.u32;
  const uint64_t lr = ctx.lr;
  edram_refused = false;
  __imp__sub_8276A858(ctx, base);
  if (ctx.r3.u32 != 0 || !edram_refused) return;  // created, or failed for another reason
  edram_refused = false;
  ctx.r3.u64 = width;
  ctx.r4.u64 = height;
  ctx.r5.u64 = format;
  ctx.r6.u64 = multisample;
  ctx.r7.u64 = aliased_parameters;
  ctx.lr = lr;
  __imp__sub_8276A858(ctx, base);
  REXLOG_INFO("video mode: {}x{} render target (D3D format {:08X}) aliased in EDRAM: {}", width,
              height, format, ctx.r3.u32 ? "created" : "failed");
}

// Read only: the scene viewport CreateViewports kept (the game's behaviour does not change).
FUNCTION_ADDRESS_CHECK(kCreateViewports, 8220A470);
REX_EXTERN(__imp__sub_8220A470);
REX_FUNC(sub_8220A470) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_8220A470(ctx, base);
  torchlight::hooks::ViewsCreated(abi::ReadU32(base, self + fn::kViewsSceneViewport));
}

}  // extern "C"
