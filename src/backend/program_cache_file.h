// The program cache file (tl_backend_open_program_cache): OGRE's serialised microcode cache with a
// trailer of our own, its length and a checksum, so a file cut short or damaged is recognised before
// OGRE reads it. OGRE's loadMicrocodeCache swallows some read errors (leaving the cache empty, as
// if loaded) and throws on others after part of the entries are in. No OGRE types.

#pragma once

#include <optional>
#include <string>

namespace torchlight::backend {

// `payload` (OGRE's bytes) with the trailer appended.
std::string WrapProgramCache(const std::string& payload);

// OGRE's bytes, when `file` ends in a trailer that matches them; nothing for a file that is empty,
// cut short, damaged or written by something else.
std::optional<std::string> UnwrapProgramCache(const std::string& file);

}  // namespace torchlight::backend
