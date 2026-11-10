// A language pack's fonts for the Xbox 360's own Serif14 (docs/translations.md). The 360 has a
// Serif14 font that the PC version lacks, so a pack made from a PC translation (GOG's Russian, for
// instance) replaces Serif, FrizQuadrata and the others with fonts that have its script, but not
// Serif14, which keeps the game's BRLNSDB.TTF (Latin only): every text in Serif14 (the settings
// menu's, the skills', the character sheet's, the waypoints'...) came out blank in Cyrillic
// (Zolrex, 2026-11-08). When a pack has a Serif.font and no Serif14.font, a Serif14.font is made
// from its Serif.font at size 14 and served with the generated layouts (video_menu.cpp), found
// before the game's. Plain files and strings, tested without the game (pack_fonts_test.cpp).

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace torchlight::game_menu {

// The font definition `font_xml` (a CEGUI .font file) as the font `name` at `size`: its <Font>
// element's Name and Size attributes replaced, everything else kept. Nothing when it has no
// <Font element with both.
std::optional<std::string> DerivedFont(std::string_view font_xml, std::string_view name, int size);

// `pack_dir` (translations/<code>/) has media/UI/Serif.font (any case) and no Serif14.font: writes
// `out_dir`/media/UI/Serif14.font from it and returns true. False when nothing is needed or it
// cannot be done (`error` says why for the latter).
bool WriteMissingSerif14(const std::filesystem::path& pack_dir, const std::filesystem::path& out_dir,
                         std::string& error);

}  // namespace torchlight::game_menu
