// What the guest is not shown of the replacements folder (tlrepl:) in the native mode: the image
// files the pak also has, unless an imageset names them.
//
// The guest's OGRE loads every texture it finds into the console's physical memory (512 MB) at the
// file's size, though in the native mode the host draws from its own copy of the file (the texture
// resolver, texture_files.h). A texture pack's larger images (TNNR: up to 8 times per side) then
// exhausted the guest's physical heap on a level change, and a texture creation failed with a fatal
// exception. Hidden from the guest's listing of tlrepl:, those names stay in the guest's resource
// index as the pak's (smaller) files, which the guest loads; the host still resolves the
// replacement and draws it. An imageset's texture stays visible: CEGUI places its images by the
// texture's size in pixels, so the guest needs the replacement's own. Files that are not images
// (meshes, materials, layouts) and images the pak does not have are left as they are.
//
// With Xenos the guest's copy is the one drawn: nothing is hidden there. No guest, OGRE or platform
// types.

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace torchlight::mods {

// The key a path is compared by: upper case, '/' separators, no leading separator (as
// TextureNameKey; any case and either separator match, as the guest's file system does).
std::string GuestViewKey(std::string_view path);

// Whether a file name is an image the guest's OGRE loads as a texture (by its extension).
bool IsImageFile(std::string_view path);

// The replacements' files (paths relative to the folder) to hide from the guest: images the pak has
// (`pak_has`, by GuestViewKey) that no imageset names (`imageset_textures`, GuestViewKey'd: a full
// path, or a bare file name, which then keeps every file of that name visible). Keys.
std::unordered_set<std::string> GuestHiddenReplacements(const std::vector<std::string>& files,
                                                        const std::function<bool(const std::string&)>& pak_has,
                                                        const std::unordered_set<std::string>& imageset_textures);

}  // namespace torchlight::mods
