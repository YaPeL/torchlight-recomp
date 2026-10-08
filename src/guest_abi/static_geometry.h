// The guest's scene culling and OGRE StaticGeometry, as the bucket culling reads them
// (hooks/bucket_cull_hooks.cpp). Same conventions as ogre_layout.h (big-endian, MSVC layout,
// confidence per entry). Header citations are relative to ~/ogre-1.7.0/OgreMain/include; the
// octree plugin is PlugIns/OctreeSceneManager. docs/guest-hot-paths.md, "Culling", has the walk.

#pragma once

#include <cstdint>

#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::static_geometry {

// Runic's OctreeSceneManager::_findVisibleObjects(camera, ...) (vtable 0x82005B9C slot 122; the
// base SceneManager's is 0x82442468): clears the render queue, updates the camera and walks the
// octree (0x821C7158), queueing every object of every visible node.
inline constexpr uint32_t kFindVisibleObjects = 0x821A6070;
// RenderQueue::addRenderable(renderable, group, priority): Entity::_updateRenderQueue 0x821E0D18
// calls it per sub-entity (@0x821E0DD0) with (queue, renderable, group, priority); every geometry
// bucket of a visible StaticGeometry region arrives here (MaterialBucket::addRenderables,
// OgreStaticGeometry.cpp:1338).
inline constexpr uint32_t kAddRenderable = 0x821C3730;

namespace scene_manager {
// [confirmed] MovableObject::isVisible 0x821C3660 reads the current viewport at +0xA8
// (@0x821C36AC); SceneManager::_renderScene sets it per viewport (OgreSceneManager.h mCurrentViewport).
inline constexpr Field kCurrentViewport{0xA8, Confidence::kConfirmed};
}  // namespace scene_manager

namespace frustum {
// [confirmed] Frustum::isVisible(AABB) 0x821C74A0 (the walk's node test) tests six planes
// {normal xyz, d} at +0x100..+0x150 (@0x821C7518..). OgreFrustum.h:62 order: near, far, left,
// right, top, bottom; normals point inside.
inline constexpr Field kPlanes{0x100, Confidence::kConfirmed};
inline constexpr uint32_t kPlaneCount = 6;
}  // namespace frustum

namespace camera {
// [confirmed] mCullFrustum (OgreCamera.h:175): Camera::isVisible and friends delegate to it when
// set (0x821C0E50, 0x8244B140, 0x8244B1E0) and the walk's node test uses its planes
// (@0x821C72A8); the ctor 0x8244A530 zeroes it.
inline constexpr Field kCullFrustum{0x4C8, Confidence::kConfirmed};
}  // namespace camera

namespace geometry_bucket {
// [confirmed] StaticGeometry::GeometryBucket (OgreStaticGeometry.h:139): the ctor 0x8247F528
// (between MaterialBucket::build 0x8247F3E0 and the bucket dump 0x824801E8) stores this vptr at
// +0 (@0x8247F560); the destructor 0x8247F728 restores it (@0x8247F750).
inline constexpr uint32_t kVtable = 0x820016B4;
inline constexpr uint32_t kDestructor = 0x8247F728;
// [confirmed] mParent (the MaterialBucket), stored from r4 by the ctor @0x8247F584; read by
// getWorldTransforms 0x821D2620 (slot 6) @0x821D2630.
inline constexpr Field kParent{0x44, Confidence::kConfirmed};
// [confirmed] mVertexData (the built copy): the ctor stores the clone @0x8247F5AC; the
// destructor reads it to delete it @0x8247F740.
inline constexpr Field kVertexData{0x64, Confidence::kConfirmed};
// [confirmed] mIndexData (the built copy): the ctor stores the clone @0x8247F5C0.
inline constexpr Field kIndexData{0x68, Confidence::kConfirmed};
}  // namespace geometry_bucket

namespace material_bucket {
// [confirmed] mParent (the LODBucket): getWorldTransforms 0x821D2620 @0x821D2638.
inline constexpr Field kParent{0x04, Confidence::kConfirmed};
}  // namespace material_bucket

namespace lod_bucket {
// [confirmed] mParent (the Region): getWorldTransforms 0x821D2620 @0x821D263C.
inline constexpr Field kParent{0x04, Confidence::kConfirmed};
}  // namespace lod_bucket

namespace movable_object {
// [confirmed] mParentNode: MovableObject::_getParentNodeFullTransform 0x821D2698 (the region's
// slot 34, which GeometryBucket::getWorldTransforms copies) reads it at +0x28 and returns
// node->_getFullTransform() (slot 50) or the identity.
inline constexpr Field kParentNode{0x28, Confidence::kConfirmed};
}  // namespace movable_object

namespace node {
// [confirmed] Node::_getFullTransform 0x821D3AF8: when +0x124 (mCachedTransformOutOfDate, bool)
// is set it rebuilds mCachedTransform (Matrix4, row-major) at +0xE4 and clears the flag
// (@0x821D3B6C); it returns +0xE4.
inline constexpr Field kCachedTransform{0xE4, Confidence::kConfirmed};
inline constexpr Field kCachedTransformOutOfDate{0x124, Confidence::kConfirmed};
}  // namespace node

}  // namespace torchlight::guest_abi::static_geometry
