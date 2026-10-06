// Identity of guest resources: (kind, guest address, generation). Thread-safe.
//
// A new generation starts whenever the object at an address is a different one: a construction
// (constructor hooks), a program created under another name at a reused address (programs have no
// constructor or destructor hook; see createGpuProgram in resource_hooks), or a texture loaded
// again after its first load (unload + loadImpl can change its name, size or content). The id it
// replaces is returned as retired, so consumers drop it as if destroyed.

#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "commands/types.h"
#include "live/measured_mutex.h"

namespace torchlight::capture {

struct BufferInfo {
  commands::ResourceId id;
  uint32_t element_size = 0;  // vertex size or index size
  uint32_t count = 0;         // vertices or indexes
  uint32_t usage = 0;
};

class ResourceRegistry {
 public:
  // A construction at `address`. Returns the id of an object still registered there (its
  // destruction was not seen), now retired.
  std::optional<commands::ResourceId> Create(commands::ResourceKind kind, uint32_t address,
                                             BufferInfo info = {});
  // A destruction. Returns the destroyed id.
  std::optional<commands::ResourceId> Destroy(uint32_t address);
  std::optional<BufferInfo> Lookup(commands::ResourceKind kind, uint32_t address) const;

  struct Renewal {
    commands::ResourceId id;                     // current identity
    std::optional<commands::ResourceId> retired;  // replaced identity, if any
  };
  // The program at `address` was created as `name`: a new generation unless it already is that
  // program.
  Renewal Program(uint32_t address, const std::string& name);
  // The texture at `address` (registered by its constructor) finished loading: the first load
  // keeps its generation, every later one starts a new generation. Nullopt if not registered.
  std::optional<Renewal> TextureLoaded(uint32_t address);

 private:
  struct Live {
    commands::ResourceKind kind;
    BufferInfo info;
    std::string name;     // programs
    bool loaded = false;  // textures: a load completed since construction
  };
  commands::ResourceId NewGeneration(commands::ResourceKind kind, uint32_t address);

  mutable live::MeasuredMutex mutex_{"resource registry"};
  std::unordered_map<uint32_t, Live> live_;
  std::unordered_map<uint32_t, uint32_t> generations_;
};

}  // namespace torchlight::capture
