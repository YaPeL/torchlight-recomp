// The deferral of layout props' forced bone evaluation (hooks/prop_bones_hooks.cpp), apart from the
// guest so it can be tested: when to defer, and the counts of evaluations done, deferred and later
// required by the render path in the same frame.

#pragma once

#include <cstdint>
#include <unordered_set>

namespace torchlight::hooks {

// Whether the forced Entity::updateAnimation of an entity may be skipped: the option is on, the call
// comes inside a layout prop's update, and the entity has no child object attached to a bone (no
// TagPoint, light or effect that would follow it). The render path still evaluates the pose before
// drawing the entity (Entity::_updateRenderQueue runs the full updateAnimation for every skeletal
// entity it queues), in any camera, shadows included.
inline bool ShouldDeferPropBones(bool enabled, bool in_prop_update, uint32_t child_objects) {
  return enabled && in_prop_update && child_objects == 0;
}

// Per frame: the entities deferred (and their skeletons), whether the render path evaluated one of
// them afterwards, and whether guest code looked up a bone by name (Skeleton::getBone(String), the
// game's way to read a bone's transform) on a deferred skeleton before that: such a reader would see
// the last evaluated pose.
class PropBonesCounts {
 public:
  void NewFrame(uint32_t frame) {
    if (frame == frame_) return;
    frame_ = frame;
    deferred_now_.clear();
    skeletons_now_.clear();
  }
  // A forced update that ran (a unit's, or a prop with attachments; it may still find nothing to do).
  void Evaluated() { ++evaluated_; }
  void Deferred(uint32_t entity, uint32_t skeleton) {
    ++deferred_;
    deferred_now_.insert(entity);
    if (skeleton) skeletons_now_.insert(skeleton);
  }
  // The render path's call for `entity`: counted as required when it was deferred this frame.
  void RenderPath(uint32_t entity, uint32_t skeleton) {
    if (deferred_now_.erase(entity)) ++required_;
    skeletons_now_.erase(skeleton);
  }
  // A bone looked up by name on `skeleton`: true (and counted) when its pose is deferred.
  bool BoneLookup(uint32_t skeleton) {
    if (!skeletons_now_.count(skeleton)) return false;
    ++read_while_deferred_;
    return true;
  }
  uint64_t evaluated() const { return evaluated_; }
  uint64_t deferred() const { return deferred_; }
  uint64_t required() const { return required_; }
  uint64_t read_while_deferred() const { return read_while_deferred_; }
  void ResetTotals() { evaluated_ = deferred_ = required_ = read_while_deferred_ = 0; }

 private:
  uint32_t frame_ = 0;
  std::unordered_set<uint32_t> deferred_now_, skeletons_now_;
  uint64_t evaluated_ = 0, deferred_ = 0, required_ = 0, read_while_deferred_ = 0;
};

}  // namespace torchlight::hooks
