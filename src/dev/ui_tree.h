// Development only: the game UI's window tree as script_input.cpp reads it from the guest's CEGUI, and
// its JSON (dump_ui in input scripts, docs/dev-input-script.md). Pure, so it can be tested; the
// guest reading is in script_input.cpp.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace torchlight::dev {

struct UiRect {
  float left = 0, top = 0, right = 0, bottom = 0;
};

struct UiImage {
  std::string imageset, image;
};

struct UiWindow {
  std::string name, type, class_name;  // CEGUI name and type; the C++ class (RTTI)
  bool visible = false;  // its own flag
  bool shown = false;    // visible with every parent
  bool active = false;   // its own active flag: the pad focus is the deepest active window
  UiRect rect;           // unclipped, guest pixels
  UiRect clipped;        // clipped by the parents
  std::optional<float> alpha;  // its own
  float effective_alpha = 1;   // with the parents' (CEGUI inherits alpha by default)
  std::string font, text;
  std::map<std::string, UiImage> images;           // Image, NormalImage, HoverImage, ...
  std::map<std::string, std::string> properties;  // the other properties read, as CEGUI gives them
  std::vector<UiWindow> children;
};

struct UiMenu {
  std::string class_name, root, focus;  // the open menu's class, its layout's root window, the focus
  std::vector<std::string> focus_texts;  // the focus window's text and its children's
};

struct UiDump {
  std::string name;
  uint64_t frame = 0;
  float display_width = 0, display_height = 0;  // the root window's size: CEGUI's display, guest pixels
  std::vector<UiMenu> menus;
  std::vector<UiWindow> windows;  // the GUI sheet's children
};

// A CEGUI image property ("set:NAME image:NAME"); none when it is not one.
std::optional<UiImage> ParseImageProperty(std::string_view value);
// UTF-32 code points to UTF-8 (invalid ones as U+FFFD).
std::string Utf32ToUtf8(const std::vector<uint32_t>& code_points);
// The JSON (UTF-8, indented); every window also carries its name without the layout instance prefix
// ("base_name"); rects carry width and height, and every rect is also given scaled to
// 1080 lines ("*_1080p") for comparing with captures replayed at 1920x1080.
std::string UiDumpJson(const UiDump& dump);

}  // namespace torchlight::dev
