// The guest's render queue walk, as the native render queue reads it (docs/perf/
// native-render-queue.md). Same conventions as ogre_layout.h (big-endian, MSVC layout, confidence
// per entry). Header citations are relative to ~/ogre-1.7.0/OgreMain; the walk is
// OgreSceneManager.cpp's renderBasicQueueGroupObjects -> renderObjects -> acceptVisitor ->
// SceneMgrQueuedRenderableVisitor.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::render_queue {

// SceneManager::renderBasicQueueGroupObjects(group, om) (OctreeSceneManager vtable 0x82005B9C
// slot 26; OgreSceneManager.cpp:3008). Per priority group: RenderPriorityGroup::sort inlined (six
// calls of kCollectionSort, @0x821DD0E8..0x821DD12C), then kRenderObjects (vtable slot 27,
// vt+0x6C) on solids basic, transparents unsorted and transparents with OM_SORT_DESCENDING
// (@0x821DD154, @0x821DD17C, @0x821DD1A4; r6 = r7 = 1, r8 = 0: scissoring and light iteration
// on, no manual light list), as upstream.
inline constexpr uint32_t kRenderBasicQueueGroupObjects = 0x821DD098;
// QueuedRenderableCollection::sort(camera) (OgreRenderQueueSortingGrouping.cpp:326): only with
// OM_SORT_DESCENDING in the organisation mode (byte +0 & 2 @0x821DCFE4); radix sort above 2000
// entries (cmplwi 0x7D0 @0x821DD004), std::stable_sort below. The guest's, kept.
inline constexpr uint32_t kCollectionSort = 0x821DCFC8;
// SceneManager::renderObjects(collection, om, scissoring, doLightIteration, manualLightList)
// (vtable slot 27; OgreSceneManager.cpp:2934): writes the active visitor's fields and tail-calls
// kAcceptVisitor (@0x821DD1E4). The native render queue's cut.
inline constexpr uint32_t kRenderObjects = 0x821DD1C0;
inline constexpr VtableSlot kRenderObjectsSlot{27, Confidence::kConfirmed};
// QueuedRenderableCollection::acceptVisitor(visitor, om) (OgreRenderQueueSortingGrouping.cpp:396):
// upstream's fall-back of an organisation mode not notified, then OM_PASS_GROUP (1) ->
// kAcceptVisitorGrouped (@0x821C09B8), OM_SORT_DESCENDING (2) -> visitor slot 3 forward over the
// sorted list (@0x821C0978..), OM_SORT_ASCENDING (6) -> slot 3 backward (@0x821C0938..).
inline constexpr uint32_t kAcceptVisitor = 0x821C08B8;
inline constexpr uint32_t kOrganisationPassGroup = 1;
inline constexpr uint32_t kOrganisationSortDescending = 2;
// OgreRenderQueueSortingGrouping.h:133: OM_SORT_ASCENDING = 6 (it implies the descending list).
inline constexpr uint32_t kOrganisationSortAscending = 6;
// [Runic difference] acceptVisitorGrouped (OgreRenderQueueSortingGrouping.cpp:430). Upstream visits
// each pass of the map once, then its renderables. Runic's copies each group's renderables into a
// static vector (0x83582B5C, @0x821CC7BC), sorts them with the comparator 0x82190520
// (@0x821CC7F4) and visits the pass again (visitor slot 2) every time a renderable's float at
// kRenderableGroupKey differs from the previous one's (@0x821CC81C..0x821CC840), then the
// renderable (slot 1, @0x821CC860). A false from the pass visit ends the group (@0x821CC850).
inline constexpr uint32_t kAcceptVisitorGrouped = 0x821CC700;
// [Runic difference] The float the grouped walk sorts and splits by (lfs +0x1C @0x821CC81C). What
// Runic stores there is not read yet.
inline constexpr Field kRenderableGroupKey{0x1C, Confidence::kRunicDifference};

namespace scene_manager {
// [confirmed] renderBasicQueueGroupObjects passes +0xA4 to every collection sort as the camera
// (@0x821DD0D8; OgreSceneManager.h mCameraInProgress).
inline constexpr Field kCameraInProgress{0xA4, Confidence::kConfirmed};
// [confirmed] renderObjects reads the visitor pointer at +0x433C four times (@0x821DD1C0..
// 0x821DD1E0; OgreSceneManager.h mActiveQueuedRenderableVisitor).
inline constexpr Field kActiveVisitor{0x433C, Confidence::kConfirmed};
}  // namespace scene_manager

namespace render_priority_group {
// [confirmed] The six collections, 0x28 apart from +0x08 (OgreRenderQueueSortingGrouping.h:
// mSolidsBasic, mSolidsDiffuseSpecular, mSolidsDecal, mSolidsNoShadowReceive,
// mTransparentsUnsorted, mTransparents after mParent and three bools). renderBasicQueueGroupObjects
// sorts +0x08, +0x58, +0x30, +0x80, +0xA8, +0xD0 (@0x821DD0DC..0x821DD120) and draws +0x08, +0xA8,
// +0xD0.
inline constexpr Field kSolidsBasic{0x08, Confidence::kConfirmed};
inline constexpr Field kSolidsDiffuseSpecular{0x30, Confidence::kConfirmed};
inline constexpr Field kSolidsDecal{0x58, Confidence::kConfirmed};
inline constexpr Field kSolidsNoShadowReceive{0x80, Confidence::kConfirmed};
inline constexpr Field kTransparentsUnsorted{0xA8, Confidence::kConfirmed};
inline constexpr Field kTransparents{0xD0, Confidence::kConfirmed};
}  // namespace render_priority_group

namespace queued_renderable_collection {
// [confirmed] The organisation mode byte: sort tests bit 2 (@0x821DCFE4), acceptVisitor ands it
// with the requested mode (@0x821C08CC).
inline constexpr Field kOrganisationMode{0x00, Confidence::kConfirmed};
// [confirmed] mGrouped, a std::map<Pass*, RenderableList*> (stl_tree; its head node at +0x08 is
// what the grouped walk loads, @0x821CC770). Node key: the Pass*; value: the RenderableList*, a
// std::vector<Renderable*> (@0x821CC798..0x821CC7A0; node key at +0x0C @0x821CC830).
inline constexpr Field kGrouped{0x04, Confidence::kConfirmed};
// [confirmed] mSortedDescending, a std::vector<RenderablePass> (8 bytes an entry: renderable,
// pass): sort sizes it with >> 3 (@0x821DD000), acceptVisitor walks +0x18..+0x1C by 8
// (@0x821C0938..0x821C09A0).
inline constexpr Field kSortedDescending{0x18, Confidence::kConfirmed};
inline constexpr uint32_t kRenderablePassSize = 8;
inline constexpr Size kSize{0x28, Confidence::kConfirmed};
}  // namespace queued_renderable_collection

namespace visitor {
// SceneMgrQueuedRenderableVisitor (OgreSceneManager.h). [confirmed] renderObjects stores
// doLightIteration at +0x0C, manualLightList at +0x10 and scissoring at +0x14 (@0x821DD1CC,
// @0x821DD1D4, @0x821DD1DC). [Runic difference] Upstream's renderObjects also clears
// transparentShadowCastersMode; Runic's writes nothing else.
inline constexpr Field kAutoLights{0x0C, Confidence::kConfirmed};
inline constexpr Field kManualLightList{0x10, Confidence::kConfirmed};
inline constexpr Field kScissoring{0x14, Confidence::kConfirmed};
// The overloads sit in reverse order in the vtable (ogre_layout.h). Slot 1, visit(Renderable*)
// 0x821CCB88: validateRenderableForRendering (SceneManager vt+0x10), then renderSingleObject
// (vt+0x54). Slot 2, visit(const Pass*) 0x821D27F8: validatePassForRendering (vt+0x0C,
// @0x821D2824), then _setPass (vt+0x398, @0x821D2858). Slot 3, visit(RenderablePass*) 0x821C0A50:
// validateRenderableForRendering only (vt+0x10, @0x821C0A7C), then _setPass (@0x821C0AA8) and
// renderSingleObject (@0x821C0AD4), as upstream (OgreSceneManager.cpp:2861) less its
// transparentShadowCastersMode test [Runic difference], which matches renderObjects not writing
// that field.
inline constexpr VtableSlot kVisitRenderable{1, Confidence::kConfirmed};
inline constexpr VtableSlot kVisitPass{2, Confidence::kConfirmed};
inline constexpr VtableSlot kVisitRenderablePass{3, Confidence::kConfirmed};
}  // namespace visitor

}  // namespace torchlight::guest_abi::render_queue
