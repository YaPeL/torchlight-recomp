// The content key each resource currently has in a session recording. Writer and reader apply
// the same rule: a resource's previous key is forgotten when it gets a new one or is destroyed, so
// neither side keeps the content of a whole session (session_file.h).

#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "commands/types.h"

namespace torchlight::live {

class ContentKeys {
 public:
  // `id` now has content `key`; returns the key it replaces, if any.
  std::optional<commands::Hash> Set(const commands::ResourceId& id, commands::Hash key) {
    auto [it, inserted] = keys_.try_emplace(Identity(id), key);
    if (inserted || it->second == key) return std::nullopt;
    commands::Hash old = it->second;
    it->second = key;
    return old;
  }
  // `id` was destroyed; returns its last key, if any.
  std::optional<commands::Hash> Erase(const commands::ResourceId& id) {
    auto it = keys_.find(Identity(id));
    if (it == keys_.end()) return std::nullopt;
    commands::Hash old = it->second;
    keys_.erase(it);
    return old;
  }

 private:
  static uint64_t Identity(const commands::ResourceId& id) {
    return uint64_t(id.guest_address) << 32 | id.generation;
  }
  std::unordered_map<uint64_t, commands::Hash> keys_;
};

}  // namespace torchlight::live
