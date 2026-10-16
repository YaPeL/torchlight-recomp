// The replacements' files the guest's directory search does not return (mods/replacements_guest_view.h:
// in the native mode, the images the pak also has that no imageset names), so the guest's resource
// index keeps the pak's files for those names. Set once before the guest runs; read by the search
// hooks (find_file_hooks.cpp) on any guest thread.

#pragma once

#include <string>
#include <unordered_set>

namespace torchlight::hooks {

// Keys as mods::GuestViewKey gives them, relative to the replacements folder.
void SetHiddenReplacements(std::unordered_set<std::string> keys);

}  // namespace torchlight::hooks
