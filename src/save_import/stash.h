// The shared stash (sharedstash.bin / sharedstashh.bin): the checks a PC stash must pass before it
// replaces the recomp's. The port of tools/save_convert/convert_stash.py.

#pragma once

#include <string>
#include <vector>

#include "save_import/save_tree.h"

namespace torchlight::save_import {

// Items that would compete for one slot of the stash container (the game keeps one and deletes the
// others, sub_822E29F8). Items without a unit GUID (discarded by the game) and the automatic slots
// (-1, 999) do not count.
std::vector<std::string> StashSlotProblems(const Parsed& stash);

// How many items a 360 stash holds, or -1 if it cannot be read.
int StashItemCount(const Schema& schema, std::span<const uint8_t> data);

}  // namespace torchlight::save_import
