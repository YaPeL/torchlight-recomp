// Immutable snapshots of guest resource content for the live mode.
//
// One snapshot per (resource, version): the guest thread copies a buffer's content once per
// version (after the first draw that uses it, see capture/session.h), the backend thread reads it.
// Snapshots are shared: the store keeps only the latest version of each live resource, and every
// frame that references a snapshot holds it, so a superseded or destroyed resource's content is
// freed when the last frame that uses it has been rendered. Thread-safe.

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "commands/types.h"
#include "live/measured_mutex.h"

namespace torchlight::live {

class SnapshotStore {
 public:
  using Content = std::shared_ptr<const commands::Blob>;

  // Content key of (resource, version), used as BufferSnapshot::blob / TextureDesc::content.
  static commands::Hash Key(const commands::ResourceId& id, uint32_t version);

  // The snapshot of `id` at `version`, or null if the store does not hold that version.
  Content Find(const commands::ResourceId& id, uint32_t version) const;
  // Stores the snapshot of `id` at `version` (its hash is set to Key(id, version)) and drops the
  // store's reference to any previous version of `id`.
  Content Put(const commands::ResourceId& id, uint32_t version, commands::Blob blob);
  // The resource was destroyed: drops the store's reference to its content.
  void Release(const commands::ResourceId& id);

  size_t resources() const;
  size_t bytes() const;  // content held by the store itself (not by frames)

 private:
  struct Entry {
    uint32_t version = 0;
    Content content;
  };
  static uint64_t Identity(const commands::ResourceId& id);

  mutable MeasuredMutex mutex_{"snapshot store"};
  std::unordered_map<uint64_t, Entry> entries_;
  size_t bytes_ = 0;
};

}  // namespace torchlight::live
