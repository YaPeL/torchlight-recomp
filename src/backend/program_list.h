// The programs this device has generated before, kept in the shader cache folder so the backend can
// generate them again during a level load (tl_backend_precompile_programs) instead of at their first
// draw in play. The driver's own cache of compiled programs is emptied by every update of the game
// (and of the driver) while this list is not, so after an update the first loads compile what play
// would otherwise compile (docs/ARCHITECTURE.md, "Shader compilation").
//
// The file: a header line ("tl-programs <format> <key size>"), then one program key per line, in
// hex, in the order they were first generated. A header that does not match (another format or
// key layout) makes the list empty, and the next append starts the file again. No OGRE types.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace torchlight::backend {

// The keys in `path`, oldest first; empty when the file is missing, unreadable or of another
// format or key size, and without lines that are not a key of `key_size` bytes.
std::vector<std::string> ReadProgramList(const std::filesystem::path& path, size_t key_size);

// Adds `key` at the end of `path`, writing the header first when the file is missing or was of
// another format (`fresh`: ReadProgramList found nothing usable). False when it cannot be written.
bool AppendProgram(const std::filesystem::path& path, const std::string& key, size_t key_size,
                   bool fresh);

}  // namespace torchlight::backend
