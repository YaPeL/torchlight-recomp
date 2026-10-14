// The guest RTSS's per-object listener, as hooks/rtss_listener_hooks.cpp reads it: the pass's user
// object bindings (a std::map<String, Any>), the SGPass entry and its render state. Same
// conventions as ogre_layout.h (big-endian, MSVC layout, confidence per entry). Header citations
// are relative to ~/ogre-1.7.0 (Components/RTShaderSystem, OgreMain).

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::rtss_listener {

// [confirmed] SGRenderObjectListener::notifyRenderSingleObject (vtable 0x8200xxxx slot 1,
// OgreShaderGenerator.h): the per-object listener SceneManager::fireRenderSingleObject calls
// (0x821C4EE0, slot 13). Arguments: r3 this, r4 Renderable*, r5 Pass*, r6 AutoParamDataSource*,
// r7 LightList*, r8 suppressRenderStateChanges. Its body is ShaderGenerator::
// notifyRenderSingleObject (OgreShaderGenerator.cpp:744) inlined.
inline constexpr uint32_t kNotifyRenderSingleObject = 0x821C4F50;
// [confirmed] the listener's mOwner (ShaderGenerator*), read @0x821C4F60.
inline constexpr Field kOwner{0x04, Confidence::kConfirmed};
// [confirmed] ShaderGenerator::mActiveViewportValid (byte), read @0x821C4F74: false returns.
inline constexpr Field kActiveViewportValid{0x148, Confidence::kConfirmed};
// [confirmed] Pass::mUserObjectBindings (OgrePass.h): the listener passes pass + 368 to
// UserObjectBindings::getUserAny 0x821C0568 (@0x821C4F80).
inline constexpr Field kPassUserObjectBindings{0x170, Confidence::kConfirmed};
// [confirmed] UserObjectBindings::mAttributes (OgreUserObjectBindings.h): getUserAny reads +4
// (@0x821C0578) and, when null, allocates 12 bytes (Attributes: an Any of 8 bytes and the map
// pointer) and stores them there (@0x821C05A4..). A null here is left to the guest.
inline constexpr Field kBindingsAttributes{0x04, Confidence::kConfirmed};
// [confirmed] Attributes::mUserObjectsMap (std::map<String, Any>*): read @0x821C05D0; null means
// no bindings (getUserAny returns the static empty Any @0x821C0610).
inline constexpr Field kAttributesMap{0x08, Confidence::kConfirmed};
// [confirmed] map<String, Any> node (ogre_layout stl_tree): the key string at +12 (compared
// @0x821BFDF0.. with std::string::compare 0x821C0298), the Any at +40 (`addi r3,r11,40`
// @0x821C0600), _Isnil at +49 (`lbz r11,49(r31)` @0x821BFDE4 in the lower_bound walk 0x821BFDC8).
inline constexpr Field kNodeKey{0x0C, Confidence::kConfirmed};
inline constexpr Field kNodeAny{0x28, Confidence::kConfirmed};
inline constexpr Field kNodeIsNil{0x31, Confidence::kConfirmed};
// [confirmed] SGPass::UserKey, a static std::string filled at startup (empty in the image): the
// listener builds its address @0x821C4F78..0x821C4F84.
inline constexpr uint32_t kUserKey = 0x8349D2A4;
// [confirmed] Any::mContent (placeholder*) at +4, after the virtual destructor's vptr:
// getUserAny's result is tested at +4 (@0x821C4F8C); any_cast reads it (@0x821BFCA8).
inline constexpr Field kAnyContent{0x04, Confidence::kConfirmed};
// [confirmed] the vtable of Any::holder<SGPass*> (RTTI `?$holder@PAVSGPass@ShaderGenerator@
// RTShader@Ogre@@@Any@Ogre@@`); the held pointer follows the vptr. Any other holder type is a bad
// cast, left to the guest.
inline constexpr uint32_t kSgPassHolderVtable = 0x82000B8C;
inline constexpr Field kHolderHeld{0x04, Confidence::kConfirmed};
// [confirmed] SGPass: mDstPass at +12 (`lwz r29,12(r3)` @0x821C4FB4), mTargetRenderState at +20
// (`lwz r30,20(r3)` @0x821C4F98; a null returns).
inline constexpr Field kSgPassDstPass{0x0C, Confidence::kConfirmed};
inline constexpr Field kSgPassTargetRenderState{0x14, Confidence::kConfirmed};
// [confirmed] RenderState::mSubRenderStateList (vector<SubRenderState*>): begin at +8, end at
// +12 (@0x821C4FA8, @0x821C4FAC); the loop calls each one's vtable slot 5 (vt+0x14 @0x821C4FD4)
// with (sub, renderable, mDstPass, source, lights): SubRenderState::updateGpuProgramsParams.
inline constexpr Field kRenderStateSubStatesBegin{0x08, Confidence::kConfirmed};
inline constexpr Field kRenderStateSubStatesEnd{0x0C, Confidence::kConfirmed};
inline constexpr VtableSlot kUpdateGpuProgramsParams{5, Confidence::kConfirmed};

}  // namespace torchlight::guest_abi::rtss_listener
