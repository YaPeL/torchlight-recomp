// A mod's CEGUI imageset at the same path as one of the game's, merged with the game's (docs/pc-ui.md
// 1b). The game's data loader takes the mod's file for the path (the mods' file maps come first),
// so a mod made for PC hides every image the Xbox build's own file has and the PC file lacks: the
// Xbox build moved icons into `itemicons13/14/16` that PC keeps elsewhere, and the 360's own items
// and skills drew empty frames with Enhanced Edition or JCC - Main. On PC each mod file holds every
// image of PC's own, so nothing is lost there.
//
// The merge is a new atlas: an imageset names one image file, and both atlases use the same places
// in their textures. The game's texture is put at the left of a new one and the mod's at its right
// (both decoded, the new one written uncompressed), the game's images keep their places and the
// mod's are moved by the game texture's width; an image both files name is the mod's (the mod wins,
// as with its other files). The imageset keeps its name, its native resolution and its images'
// names, which the game's data refers to. Made only when the game's file has images the mod's
// lacks; otherwise the mod's file is already a superset and is used as it is.
//
// No guest, OGRE or platform types: the app serves the files it writes (game_menu/mods_install.cpp).

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mods/mod_list.h"

namespace torchlight::mods {

struct RgbaImage {
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> rgba;  // width * height * 4, rows top to bottom
};

// A DDS file's top level as RGBA: DXT1, DXT3, DXT5, or uncompressed 32 or 24 bits per pixel with
// their channel masks. None for anything else (or a short file).
std::optional<RgbaImage> DecodeDds(const std::vector<uint8_t>& dds);
// An uncompressed A8R8G8B8 DDS (no mipmaps), the format of the pak's own `envmap.dds`.
std::vector<uint8_t> EncodeDdsArgb(const RgbaImage& image);

// An imageset's elements, attributes kept in their order and spelling.
struct AtlasXml {
  using Attributes = std::vector<std::pair<std::string, std::string>>;
  Attributes imageset;              // the Imageset element's
  std::vector<Attributes> images;   // each Image element's
  std::string Attribute(const Attributes& a, std::string_view name) const;
};
std::optional<AtlasXml> ParseAtlas(std::string_view xml);

struct MergedAtlas {
  std::string xml;
  std::vector<uint8_t> dds;
  size_t game_only = 0;   // the game's images the mod's file lacks, kept
  size_t from_mod = 0;    // the mod's images
};
// The merge of the game's imageset (its text and its texture file) with a mod's; the new imageset
// names `texture_name` as its image file. None, with `why`, when the two cannot be merged (another
// imageset name or native resolution, a texture that does not decode, an image outside its
// texture) or need not be (the mod's file names every image of the game's).
std::optional<MergedAtlas> MergeAtlases(std::string_view game_xml, const std::vector<uint8_t>& game_texture,
                                        std::string_view mod_xml, const std::vector<uint8_t>& mod_texture,
                                        const std::string& texture_name, std::string* why);

struct WrittenAtlas {
  std::string key;         // the imageset's path as the game asks for it ("MEDIA/UI/ITEMICONS/ITEMICONS16.IMAGESET")
  std::string mod_folder;  // the mod whose file is merged
  size_t game_only = 0, from_mod = 0;
};
// For every imageset of the pak under media/ui/ that an enabled mod also has (the first in the
// search order, as the game's data loader takes it), the merge written into `out` at the path's key
// (upper case), its texture next to it as "<KEY without .IMAGESET>.DDS", named `device` + that key
// in the imageset (the device the app mounts `out` as, "tlatlas:"). The mod's texture is the file
// its Imagefile names in its folder, or the pak's when the mod has none. `out` is emptied first.
// Problems go to `log` (one line each).
std::vector<WrittenAtlas> WriteMergedAtlases(const std::filesystem::path& pak, const std::filesystem::path& mods_folder,
                                             const ModPlan& plan, const std::filesystem::path& out,
                                             const std::string& device, std::vector<std::string>& log);

}  // namespace torchlight::mods
