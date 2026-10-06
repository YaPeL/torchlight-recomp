// Line breaks for the first start's dialogs (platform::AskChoice, ShowError): SDL's message boxes
// do not wrap. On Windows the box is as wide as the longest line (SDL_windowsmessagebox.c sizes
// the TaskDialog to its content, or measures the text with DrawTextW without word breaks), so a
// paragraph on one line made it wider than a small display and left its buttons off screen.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace torchlight::platform {

// The longest line the dialogs get, in characters.
inline constexpr size_t kDialogLineWidth = 70;

// `text` with each line broken at spaces so that none is longer than `width` characters (UTF-8
// code points, so translations with accents count as they show). Existing line breaks stay; a word
// longer than the width (a path) gets a line of its own, unbroken. Spaces where a line is broken
// are dropped.
std::string WrapText(std::string_view text, size_t width = kDialogLineWidth);

}  // namespace torchlight::platform
