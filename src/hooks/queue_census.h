// Stage 0 of the native render queue (docs/perf/native-render-queue.md): how much of the guest's
// render queue walk a native path could take, read from the queue itself without changing it. For
// each collection SceneManager::renderObjects draws, every pass group (grouped walk) or entry
// (sorted walks) is classified against the stage 1 criteria that guest data decides today; the
// totals say how many draws and how much of the walk's time would stay native with a fallback by
// whole collection and with a fallback by pass group, and how often groups mix or start at -1.0.
//
// Upper bounds: the supported lists of automatic constants and RTSS sub-render states, the texture
// unit kinds and the per-object criteria (negative scale, texture coordinate generation, identity
// view or projection) are not decided here, and a group is counted whole even when the guest's
// pass visit would end it early; every pass and object that passes the rest counts as native. In
// the per-reason counts, "qualifying" is every draw that passes the criteria (including those of a
// group the per-group fallback leaves to the guest because it starts at -1.0), and a unit's
// failing draws all go to its first failing object's reason. Pure over a guest memory base, so it is tested on a fake memory.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace torchlight::hooks {

// The first stage 1 criterion an object fails, in this order (kNative when none).
enum class QueueReason : uint8_t {
  kNative,
  kNotMainScene,      // the viewport draws to a render texture (the projector passes)
  kNotSolidsBasic,    // transparents (stage 3)
  kNoPrograms,        // a vertex or fragment program missing
  kGeometryProgram,
  kIteration,         // more than one pass iteration
  kPerLight,          // iterate per light
  kLightScissorClip,  // light scissoring or light clip planes
  kDepthBias,
  kRenderableClass,   // not a SubEntity or a StaticGeometry GeometryBucket
  kSkinned,           // a SubEntity of an entity with bone matrices
  kCount,
};
const char* QueueReasonName(QueueReason r);

// Which of renderBasicQueueGroupObjects' three renderObjects calls (0x821DD098): solids basic,
// transparents unsorted, transparents.
enum class QueueCollection : uint8_t { kSolidsBasic, kTransparentsUnsorted, kTransparents };

struct QueueCensusTotals {
  uint64_t collections = 0, mixed_collections = 0;
  uint64_t groups = 0, mixed_groups = 0, groups_from_minus_one = 0;
  uint64_t draws = 0, draws_native_whole = 0, draws_native_group = 0;
  double ms = 0, ms_native_whole = 0, ms_native_group = 0;
  std::array<uint64_t, static_cast<std::size_t>(QueueReason::kCount)> draws_by_reason{};
  void Add(const QueueCensusTotals& o);
};

// Classifies one collection as acceptVisitor would walk it with organisation mode `om` (the
// guest's fall-back included). `ms` is the host time the guest's walk of it took; a group's share
// of it is taken as its share of the collection's draws. Adds to `totals`.
void ClassifyCollection(const uint8_t* membase, uint32_t collection, uint32_t om, bool main_scene,
                        QueueCollection kind, double ms, QueueCensusTotals& totals);

// The reasons for one pass and one renderable (exposed for the test).
QueueReason ClassifyPass(const uint8_t* membase, uint32_t pass);
QueueReason ClassifyRenderable(const uint8_t* membase, uint32_t renderable);

}  // namespace torchlight::hooks
