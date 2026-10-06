// Our achievement texts in the player's language (data/ui/tl_achievement_strings.txt, the video
// menu's strings format), shared by the game-UI toast and the ImGui list. Loaded once, on first use.
#pragma once
#include <string>
#include <string_view>
namespace torchlight::achievements {
std::string Translate(std::string_view english);
}  // namespace torchlight::achievements
