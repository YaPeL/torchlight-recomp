// OGRE 1.7's Node, Bone and Skeleton fields and virtual slots, the Xbox build, as the node and bone
// updates read and write them (hooks/native_skeleton.cpp). [confirmed] from the recompiled
// functions themselves (generated/default) and torchlight-nx 1.0.3, which identified them on this
// executable and verified its native versions bit for bit:
// - Node::_update 0x821C9F68: clears +0x4A (mParentNotified), reads +0x48/+0x49 (the parent and
//   child update flags), walks mChildrenToUpdate (a std::set at +0x34: head +0x38, size +0x3C) or
//   mChildren (a hash_map's list at +0x0C; list nodes: next +0, Node* +0x24), calling each child's
//   vtable +0xCC (_update; 0x821CA01C, 0x821CA088), clears the set and +0x49.
// - Node::needUpdate 0x821CA0B8: sets +0x48, +0x49 and +0x124 (mCachedTransformOutOfDate, also
//   guest_abi static_geometry.h node), asks the parent (+0x04) through vtable +0x100
//   (requestUpdate) when +0x4A is clear or forced, then sets +0x4A. Bone::needUpdate 0x821CA140
//   calls it directly (0x821CA154), then, when the bone is manually controlled (+0x136), its
//   Skeleton (+0x138) through vtable +0x12C, whose OGRE function 0x824E8F60
//   (_notifyManualBonesDirty) sets +0x108 (mManualBonesDirty).
// - Node::updateFromParentImpl 0x821C9770 (called directly by SceneNode and TagPoint, 0x821E4848,
//   0x821BFB18): from mOrientation +0x68 (w x y z), mPosition +0x78, mScale +0x84 and the
//   parent's derived values (vtable +0xBC/+0xC0/+0xC4, the getters 0x821C9BB0/0x821C8518/
//   0x821C99D0, which return +0x94/+0xA4/+0xB0 after updating from the parent when +0x48 is set),
//   with mInheritOrientation +0x90 and mInheritScale +0x91, it writes the derived orientation
//   +0x94, position +0xA4 and scale +0xB0, sets +0x124 and clears +0x48.
// - Node::_updateFromParent 0x821C9F08: vtable +0x08 (updateFromParentImpl), then mListener
//   (+0x128) vtable +0x04 (nodeUpdated); reached through vtable +0x04.
// - Quaternion * Vector3 0x821C9BF8 uses 2.0 from the game's float constants at 0x820AA2C0 + 0x7C.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::ogre_node {

inline constexpr Field kParent{0x04, Confidence::kConfirmed};
inline constexpr Field kChildren{0x0C, Confidence::kConfirmed};          // hash_map list head
inline constexpr Field kChildListNext{0x00, Confidence::kConfirmed};     // in a list node
inline constexpr Field kChildListNode{0x24, Confidence::kConfirmed};     // in a list node
inline constexpr Field kChildrenToUpdate{0x34, Confidence::kConfirmed};  // std::set
inline constexpr Field kNeedParentUpdate{0x48, Confidence::kConfirmed};
inline constexpr Field kNeedChildUpdate{0x49, Confidence::kConfirmed};
inline constexpr Field kParentNotified{0x4A, Confidence::kConfirmed};
inline constexpr Field kOrientation{0x68, Confidence::kConfirmed};
inline constexpr Field kPosition{0x78, Confidence::kConfirmed};
inline constexpr Field kScale{0x84, Confidence::kConfirmed};
inline constexpr Field kInheritOrientation{0x90, Confidence::kConfirmed};
inline constexpr Field kInheritScale{0x91, Confidence::kConfirmed};
inline constexpr Field kDerivedOrientation{0x94, Confidence::kConfirmed};
inline constexpr Field kDerivedPosition{0xA4, Confidence::kConfirmed};
inline constexpr Field kDerivedScale{0xB0, Confidence::kConfirmed};
inline constexpr Field kCachedTransformOutOfDate{0x124, Confidence::kConfirmed};
inline constexpr Field kListener{0x128, Confidence::kConfirmed};
inline constexpr Field kBoneManuallyControlled{0x136, Confidence::kConfirmed};
inline constexpr Field kBoneCreator{0x138, Confidence::kConfirmed};
inline constexpr Field kSkeletonManualBonesDirty{0x108, Confidence::kConfirmed};

// Vtable byte offsets (slot * 4).
inline constexpr uint32_t kVtUpdateFromParent = 0x04;
inline constexpr uint32_t kVtUpdateFromParentImpl = 0x08;
inline constexpr uint32_t kVtDerivedOrientation = 0xBC;
inline constexpr uint32_t kVtDerivedPosition = 0xC0;
inline constexpr uint32_t kVtDerivedScale = 0xC4;
inline constexpr uint32_t kVtUpdate = 0xCC;
inline constexpr uint32_t kVtNeedUpdate = 0xFC;
inline constexpr uint32_t kVtRequestUpdate = 0x100;
inline constexpr uint32_t kVtListenerNodeUpdated = 0x04;
inline constexpr uint32_t kVtSkeletonNotifyManualBonesDirty = 0x12C;

inline constexpr uint32_t kFloatConstants = 0x820AA2C0;
inline constexpr uint32_t kFloatTwo = kFloatConstants + 0x7C;

}  // namespace torchlight::guest_abi::ogre_node
