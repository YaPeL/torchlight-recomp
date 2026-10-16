// The game's pad prompts in the style of the player's pad: the Xbox 360 button images inside the
// game's widget imagesets (a_button, left_bumper, start_button...) drawn with PlayStation or
// Nintendo icons instead (data/ui/prompts/, Kenney's "Input Prompts", CC0). The imagesets are not
// touched: their textures are rewritten with each replaced image's region cleared and the icon
// scaled into it (aspect kept, centred), so every window that names an image keeps its size and
// place. The help screen's controller picture ("controller") becomes the style's controller
// silhouette. The sticks and the d-pad are drawn the same on every pad and stay the game's.
//
// The app writes the textures before the guest runs and serves them like the replacements folder
// (game_menu/mods_install.cpp): the game loads its imagesets once, so a change of style takes
// effect at the next start.
//
// Nintendo pads: buttons work by position, as the pad reports them (the bottom one is the 360's A),
// and the icon shows the letter of that physical button (the bottom one is B). With
// "swap confirm/cancel" the right button (A) confirms and the bottom one (B) cancels, and the icons
// follow.
//
// No guest, OGRE or platform types.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mods/icon_atlas.h"

namespace torchlight::mods {

enum class PadStyle { kXbox, kPlayStation, kNintendo };
// The kind of pad connected, as the platform reports it.
enum class PadKind { kNone, kXbox, kPlayStation, kNintendo, kOther };

// The style in effect: the one chosen, or with none (automatic) the connected pad's; Xbox for
// anything else or no pad.
PadStyle EffectivePadStyle(std::optional<PadStyle> chosen, PadKind connected);
// Whether confirm and cancel are swapped: only with Nintendo's style.
bool SwapsConfirmCancel(PadStyle style, bool swap_setting);
// "xbox" / "playstation" / "nintendo": also the icons' folder below data/ui/prompts/.
std::string_view PadStyleName(PadStyle style);

// Confirm and cancel swapped in what the guest reads: the A and B bits of a pad state's buttons
// (`a_bit`, `b_bit`), or a keystroke's virtual key A <-> B; other values unchanged.
uint16_t SwapButtonBits(uint16_t buttons, uint16_t a_bit, uint16_t b_bit);
uint16_t SwapVirtualKey(uint16_t key, uint16_t key_a, uint16_t key_b);

struct PromptIcon {
  std::string image;  // the game's image name ("a_button")
  std::string file;   // our icon, below the style's folder, without ".dds" ("cross")
};
// The game's images a style replaces and the icon for each; none for Xbox.
std::vector<PromptIcon> PromptIcons(PadStyle style, bool swap_confirm_cancel);

// `icon` scaled into `width` x `height` (an area filter on premultiplied alpha), its aspect kept,
// centred, transparent around it.
RgbaImage FitIcon(const RgbaImage& icon, uint32_t width, uint32_t height);

// Transparent texels kept around an icon inside its region, like the 360's own button art.
inline constexpr long kPromptMargin = 1;

// The imageset's texture with every image of `icons` (by image name) redrawn in its region, inside
// a kPromptMargin border; the other pixels as they were. An uncompressed A8R8G8B8 DDS. None, with `why`, when the texture does
// not decode or an image lies outside it.
struct NamedIcon {
  std::string image;
  RgbaImage icon;
};
std::optional<std::vector<uint8_t>> PatchPromptTexture(std::string_view imageset_xml,
                                                       const std::vector<uint8_t>& texture,
                                                       const std::vector<NamedIcon>& icons, size_t* replaced,
                                                       std::string* why);

struct WrittenPrompts {
  std::string texture;  // the texture's path in the pak, as the imageset names it
  size_t replaced = 0;  // its images redrawn
};
// For every imageset of the pak under media/ui/ that has one of the style's images, its texture
// patched (PatchPromptTexture) and written into `out` at the texture's path, as a file of the pak
// would be in the replacements folder. The icons are read from `icons_dir`/<style name>/. `out`
// is emptied first; nothing is written for Xbox. Problems go to `log` (one line each).
std::vector<WrittenPrompts> WritePromptTextures(const std::filesystem::path& pak, const std::filesystem::path& icons_dir,
                                                PadStyle style, bool swap_confirm_cancel,
                                                const std::filesystem::path& out, std::vector<std::string>& log);

}  // namespace torchlight::mods
