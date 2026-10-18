// OGRE Entity's skeletal animation as the guest runs it, for the deferral of layout props' forced
// bone evaluation (hooks/prop_bones_hooks.cpp). Same conventions as ogre_layout.h (big-endian, MSVC
// layout, confidence per entry). Header and source citations are relative to ~/ogre-1.7.0/OgreMain;
// addresses were read in the recompiled code (tools/guest_re/gdis.py). docs/game-thread-parallelism.md,
// section 11.2, has the layout update that drives it.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::entity_animation {

// [confirmed] Entity::cacheBoneMatrices() (src/OgreEntity.cpp): if *mFrameBonesLastUpdated differs
// from Root's next frame number (Root singleton 0x835067D4, +0xE0 @0x821B93D8) or the skeleton's
// manual bones are dirty (vtable +0x134), applies the animation state set unless
// mSkipAnimStateUpdates (SkeletonInstance vtable +0xFC: Skeleton::setAnimationState 0x821CC498,
// @0x821B943C), builds the bone matrices (vtable +0x108, 0x821CB3C0, @0x821B9454) and stores the
// frame number. Three callers: Entity::updateAnimation (@0x821B9504, the forced path) and the render
// path, Entity::_updateRenderQueue 0x821E0D18 (@0x821E0DFC) and 0x821E0EA8 (@0x821E11B0).
inline constexpr uint32_t kCacheBoneMatrices = 0x821B93B0;
// Return address of the forced path's call: Entity::updateAnimation 0x821B9478 calls
// cacheBoneMatrices with `bl` at 0x821B9504.
inline constexpr uint32_t kForcedCallReturn = 0x821B9508;
// [confirmed] Entity::updateAnimation() 0x821B9478: only caller the game's animation player update
// 0x821BB578 (@0x821BC3AC; tools/guest_re/xref.py). It caches the bone matrices when the animation
// state set's dirty frame (+0 of the set, @0x821B94A4) differs from mFrameAnimationLastUpdated, then,
// when the animation or the parent node's full transform changed, notifies every child object
// attached to a bone (the map walk @0x821B95A0..0x821B95F0: child->getParentNode()->needUpdate).
inline constexpr uint32_t kUpdateAnimation = 0x821B9478;
// [confirmed] Skeleton::reset(bool) 0x821A7428 (src/OgreSkeleton.cpp): walks mBoneList (+0xBC/+0xC0)
// and resets each bone not manually controlled (Bone +0x136); called by setAnimationState.
inline constexpr uint32_t kSkeletonReset = 0x821A7428;
// The game's CGenericModelDescriptor update (layout descriptors, slot 7; game-thread-parallelism.md
// 11.2): for each child that is an animation player it calls 0x821BB578 (@0x821DEE70). Only layout
// props (the level's decor) come through it.
inline constexpr uint32_t kGenericModelUpdate = 0x821DEDF8;

// [confirmed] Skeleton::getBone(const String&) (src/OgreSkeleton.cpp; throws "Skeleton::getBone" /
// "Bone named '" @0x824E90F4/@0x824E9104), vtable +0xCC of Skeleton (vtable 0x820043AC, whose +0xFC is
// setAnimationState 0x821CC498 and +0xDC reset 0x821A7428) and of SkeletonInstance (0x820044FC);
// +0xD0 is getBone(unsigned short), 0x821CCDC0. No direct callers (tools/guest_re/xref.py): only
// through the vtable.
inline constexpr uint32_t kGetBoneByName = 0x824E90B0;

namespace entity {
// [confirmed] mChildObjectList (OgreEntity.h ChildObjectList, a std::map): its head node at +0x20C
// (walked @0x821B95A0) and its size at +0x210 (tested @0x821B9508), stl_tree layout at +0x208.
inline constexpr Field kChildObjectCount{0x208 + ogre::stl_tree::kSize.offset, Confidence::kConfirmed};
// [confirmed] mSkeletonInstance (+0x1BC, @0x821B94B0, @0x821B93E4).
inline constexpr Field kSkeletonInstance{0x1BC, Confidence::kConfirmed};
}  // namespace entity

}  // namespace torchlight::guest_abi::entity_animation
