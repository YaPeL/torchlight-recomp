// Defers the forced bone evaluation of layout props with nothing attached to their bones
// (hooks/prop_bones.h): the game's layout update (CGenericModelDescriptor, 0x821DEDF8) advances each
// animated prop's animation player, which calls Entity::updateAnimation and so evaluates the pose
// every frame, on screen or not; the render path evaluates it anyway, lazily, for the entities it
// draws. Off by default (--defer_prop_bones). The animation player still advances the animation
// states; only its call to the trimmed updateAnimation is skipped for those entities, which leaves
// them as if nothing had forced them, and the render path's full updateAnimation evaluates the pose
// when it draws them (hardware or software animation). docs/game-thread-parallelism.md, 11.2.

#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "guest_abi/entity_animation.h"
#include "guest_abi/ogre_layout.h"
#include "hooks/prop_bones.h"

REXCVAR_DEFINE_BOOL(defer_prop_bones, false, "Torchlight",
                    "Experiment: skip the forced bone evaluation of layout props with nothing "
                    "attached to their bones; the render path evaluates the pose when it draws them");

namespace {

namespace abi = torchlight::guest_abi;
namespace anim = abi::entity_animation;

// Both only touched on the game's thread (the layout update and the render path run there).
bool g_in_prop_update = false;
torchlight::hooks::PropBonesCounts g_counts;
std::chrono::steady_clock::time_point g_last_log{};

// Root's next frame number, as cacheBoneMatrices reads it (@0x821B93C4..0x821B93D8).
uint32_t NextFrame(const uint8_t* base) {
  const uint32_t root = abi::ReadU32(base, 0x835067D4u);
  return root ? abi::ReadU32(base, root + 0xE0) : 0;
}

}  // namespace

extern "C" {

REX_EXTERN(__imp__sub_821DEDF8);
REX_FUNC(sub_821DEDF8) {
  static_assert(anim::kGenericModelUpdate == 0x821DEDF8u);
  const bool outer = g_in_prop_update;
  g_in_prop_update = true;
  __imp__sub_821DEDF8(ctx, base);
  g_in_prop_update = outer;
}

// The forced path: OGRE's trimmed Entity::updateAnimation (guest_abi
// entity_animation::kForcedUpdateAnimation), called by the animation player. Skipped as a whole for a
// layout prop with nothing attached to its bones: the entity is left as if nothing had forced it
// (mFrameAnimationLastUpdated and the parent transform cache untouched), so the render path's full
// updateAnimation, which _updateRenderQueue runs for every skeletal entity it queues, evaluates the
// pose itself, hardware or software animation alike.
REX_EXTERN(__imp__sub_821B9478);
REX_FUNC(sub_821B9478) {
  static_assert(anim::kForcedUpdateAnimation == 0x821B9478u);
  static const bool enabled = REXCVAR_GET(defer_prop_bones);
  if (!enabled) {
    __imp__sub_821B9478(ctx, base);
    return;
  }
  const uint32_t entity = ctx.r3.u32;
  g_counts.NewFrame(NextFrame(base));
  const uint32_t children = abi::ReadU32(base, entity, anim::entity::kChildObjectCount);
  if (torchlight::hooks::ShouldDeferPropBones(true, g_in_prop_update, children)) {
    g_counts.Deferred(entity, abi::ReadU32(base, entity, anim::entity::kSkeletonInstance));
  } else {
    g_counts.Evaluated();
    __imp__sub_821B9478(ctx, base);
  }
  // The counts every 10 s.
  const auto now = std::chrono::steady_clock::now();
  if (g_last_log.time_since_epoch().count() == 0) {
    g_last_log = now;
    REXLOG_INFO("prop bones: deferring the forced evaluation of layout props (--defer_prop_bones)");
  } else if (now - g_last_log >= std::chrono::seconds(10)) {
    REXLOG_INFO("prop bones (10 s): forced updates run {}, deferred {}, deferred then evaluated by "
                "the render path {}, bones looked up by name while deferred {}",
                g_counts.evaluated(), g_counts.deferred(), g_counts.required(),
                g_counts.read_while_deferred());
    g_counts.ResetTotals();
    g_last_log = now;
  }
}

// Entity::cacheBoneMatrices: only counted here, for the render path's evaluations of deferred props.
REX_EXTERN(__imp__sub_821B93B0);
REX_FUNC(sub_821B93B0) {
  static_assert(anim::kCacheBoneMatrices == 0x821B93B0u);
  static const bool enabled = REXCVAR_GET(defer_prop_bones);
  if (enabled) {
    const uint32_t entity = ctx.r3.u32;
    g_counts.NewFrame(NextFrame(base));
    g_counts.RenderPath(entity, abi::ReadU32(base, entity, anim::entity::kSkeletonInstance));
  }
  __imp__sub_821B93B0(ctx, base);
}

// Skeleton::getBone(const String&) (vtable +0xCC of Skeleton 0x820043AC and SkeletonInstance
// 0x820044FC; guest_abi entity_animation::kGetBoneByName): a lookup on a deferred skeleton is logged
// with its caller, the first ones in full, so a direct reader of a deferred prop's bones shows up.
REX_EXTERN(__imp__sub_824E90B0);
REX_FUNC(sub_824E90B0) {
  static_assert(anim::kGetBoneByName == 0x824E90B0u);
  static const bool enabled = REXCVAR_GET(defer_prop_bones);
  static uint32_t logged = 0;
  if (enabled && g_counts.BoneLookup(ctx.r3.u32) && logged < 20) {
    ++logged;
    REXLOG_WARN("prop bones: a bone looked up by name on a deferred skeleton {:08X}, caller {:08X}",
                ctx.r3.u32, uint32_t(ctx.lr));
  }
  __imp__sub_824E90B0(ctx, base);
}

}  // extern "C"
