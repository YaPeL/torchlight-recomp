// The classes and pets the character creation screen offers with mods (docs/mods.md, section 7l).
// PC lists them as the buttons of media/UI/charactercreate.layout: classes with onClick
// guiSelect1, each named after its class's player unit, and pets with onClick guiPet1, each named
// after its pet's unit (a monster); mods that add classes or pets ship their own copy of that
// layout. Each list is the game's three first, then every other such button of the enabled mods'
// layouts, in the mods' search order and the layout's order, kept only when a unit of that name
// exists in the right group. No guest, OGRE or platform types.

#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "mods/mod_list.h"

namespace torchlight::mods {

// The Xbox screen's three classes, in its order (its buttons and pad grid, game_ui.h new_game_menu).
inline constexpr std::string_view kGameClasses[] = {"Destroyer", "Alchemist", "Vanquisher"};
// The Xbox screen's three pets, in its order (its pet row, the grid's third).
inline constexpr std::string_view kGamePets[] = {"Dog", "Cat", "Chakawary"};

// Class names are UTF-8 here; the game's are UTF-16 (its std::wstring, its unit NAMEs). A lone
// surrogate or an invalid UTF-8 sequence becomes U+FFFD.
std::string Utf16ToUtf8(std::u16string_view text);
std::u16string Utf8ToUtf16(std::string_view text);
// A class name as names are compared (a layout's button against a unit's NAME, the list's repeats):
// ASCII and Latin-1 letters in upper case (the game's unit NAMEs are upper case), the rest as it is.
std::string ClassKey(std::string_view name);
// A layout file's text as UTF-8: UTF-16 (with its byte order mark, either order) converted, a UTF-8
// byte order mark dropped, anything else as it is.
std::string LayoutText(const std::vector<uint8_t>& bytes);

// The names of a CEGUI layout's windows whose "onClick" property is guiSelect1 (any case), in the
// layout's order.
std::vector<std::string> ClassButtons(std::string_view layout_xml);

// The game's classes, then the other buttons in order, without repeats (names compared in any
// case, ClassKey) and without those whose name is not a player unit's (`player_units`: ClassKeys
// of the units' NAMEs).
std::vector<std::string> ClassList(const std::vector<std::vector<std::string>>& buttons_by_mod,
                                   const std::set<std::string>& player_units);

// The player units' NAMEs (as ClassKeys) the enabled mods of `plan` define (*.dat under
// media/units/players/, text definitions).
std::set<std::string> ModPlayerUnits(const std::filesystem::path& mods_folder, const ModPlan& plan);

// Each enabled mod's class buttons, in the plan's search order (media/UI/charactercreate.layout,
// any case; a mod without one gives none).
std::vector<std::vector<std::string>> ModClassButtons(const std::filesystem::path& mods_folder,
                                                      const ModPlan& plan);

// The same for pets: buttons with onClick guiPet1 (PC's) or GUISELECT2 (the Xbox screen's), and the
// enabled mods' monster units (*.dat under media/units/monsters/).
std::vector<std::string> PetButtons(std::string_view layout_xml);
std::vector<std::string> PetList(const std::vector<std::vector<std::string>>& buttons_by_mod,
                                 const std::set<std::string>& monster_units);
std::set<std::string> ModMonsterUnits(const std::filesystem::path& mods_folder, const ModPlan& plan);
// The name the game shows for each of those monster units (its DISPLAYNAME, else its NAME), by the
// NAME's ClassKey: a mod pet's button label.
std::map<std::string, std::string> ModMonsterDisplayNames(const std::filesystem::path& mods_folder,
                                                          const ModPlan& plan);
std::vector<std::vector<std::string>> ModPetButtons(const std::filesystem::path& mods_folder, const ModPlan& plan);

// A mod class's own icon (docs/mods.md 7l, "option B"): PC's creation layout gives each class
// button the images of a CEGUI imageset ("set:<imageset> image:<image>"; a radio button's
// UnselectedImage, HoverImage and SelectedImage, or a button's NormalImage, HoverImage and
// PushedImage), and the imageset is a mod's .imageset file (XML: its Name, its Imagefile, a path in
// the mod, and its images).
struct ButtonImages {
  std::string imageset;
  std::string normal, hover, pushed;  // image names; hover and pushed are the normal one when missing
};
// The class buttons' images of a layout (ClassButtons' windows), by the button's ClassKey; a button
// whose images are missing, unreadable or from more than one imageset has none.
std::map<std::string, ButtonImages> ClassButtonImages(std::string_view layout_xml);

struct ImagesetInfo {
  std::string name;
  std::string image_file;  // as written (a path in the mod, '/' or '\\')
  std::set<std::string> images;
};
// An imageset file's text (LayoutText); none when it has no Imageset element with a Name and an
// Imagefile.
std::optional<ImagesetInfo> ParseImageset(std::string_view xml);

struct ModImageset {
  std::string mod_folder;      // the mod's folder name in the mods folder
  std::string file;            // the .imageset's path in the mod, '/' separated, as found on disk
  ImagesetInfo info;
};
// The enabled mods' imagesets (*.imageset under media/ui/, any case), by Name; only those whose
// image file exists in the same mod. A Name two files define is left out: the game loads the mods'
// item icon imagesets itself, and which file it took is not known.
std::map<std::string, ModImageset> ModImagesets(const std::filesystem::path& mods_folder, const ModPlan& plan);

// The Names of the imagesets in the game's pak (*.imageset under media/ui/): an imageset of one of
// those names is the game's, whatever a mod defines. Empty, with `error` set, when the pak does not
// read.
std::set<std::string> PakImagesetNames(const std::filesystem::path& pak, std::string* error);

struct ClassIcon {
  ButtonImages images;
  ModImageset imageset;
};
// The mods' classes that have their own icon: each enabled mod's layout class buttons (ModClassButtons'
// order, the first mod with a button for a class kept), whose imageset is one of `imagesets` and
// holds every image the button names. By the class's ClassKey.
std::map<std::string, ClassIcon> ModClassIcons(const std::filesystem::path& mods_folder, const ModPlan& plan,
                                               const std::map<std::string, ModImageset>& imagesets);

}  // namespace torchlight::mods
