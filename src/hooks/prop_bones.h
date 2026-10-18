// The deferral of layout props' forced bone evaluation (hooks/prop_bones_hooks.cpp), apart from the
// guest so it can be tested: when to defer, and the counts of evaluations done, deferred and later
// required by the render path in the same frame.

#pragma once

#include <cstdint>
#include <unordered_set>

namespace torchlight::hooks {

// Whether the forced path's Entity::cacheBoneMatrices may be skipped: the option is on, the call
// comes from the forced path (Entity::updateAnimation) inside a layout prop's update, and the entity
// has no child object attached to a bone (no TagPoint, light or effect that would follow it). The
// render path still evaluates the pose lazily before drawing the entity (Entity::_updateRenderQueue
// calls cacheBoneMatrices itself), in any camera, shadows included.
inline bool ShouldDeferPropBones(bool enabled, bool in_prop_update, bool forced_call,
                                 uint32_t child_objects) {
  return enabled && in_prop_update && forced_call && child_objects == 0;
}

// Per frame: the entities deferred, and whether the render path evaluated one of them afterwards.
class PropBonesCounts {
 public:
  void NewFrame(uint32_t frame) {
    if (frame == frame_) return;
    frame_ = frame;
    deferred_now_.clear();
  }
  void Evaluated() { ++evaluated_; }
  void Deferred(uint32_t entity) {
    ++deferred_;
    deferred_now_.insert(entity);
  }
  // The render path's call for `entity`: counted as required when it was deferred this frame.
  void RenderPath(uint32_t entity) {
    if (deferred_now_.erase(entity)) ++required_;
  }
  uint64_t evaluated() const { return evaluated_; }
  uint64_t deferred() const { return deferred_; }
  uint64_t required() const { return required_; }
  void ResetTotals() { evaluated_ = deferred_ = required_ = 0; }

 private:
  uint32_t frame_ = 0;
  std::unordered_set<uint32_t> deferred_now_;
  uint64_t evaluated_ = 0, deferred_ = 0, required_ = 0;
};

}  // namespace torchlight::hooks
