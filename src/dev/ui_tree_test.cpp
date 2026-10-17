// The UI dump's pure parts (ui_tree.h): image properties, UTF-32 text and the JSON.

#include "dev/ui_tree.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>

namespace {

using namespace torchlight::dev;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

void TestImageProperty() {
  const auto i = ParseImageProperty("set:widgets image:a_button");
  Check(i && i->imageset == "widgets" && i->image == "a_button", "set and image");
  const auto spaced = ParseImageProperty("set:My Set image:Some Image ");
  Check(spaced && spaced->imageset == "My Set" && spaced->image == "Some Image", "names with spaces");
  Check(!ParseImageProperty(""), "empty: none");
  Check(!ParseImageProperty("image:x set:y"), "out of order: none");
  Check(!ParseImageProperty("set: image:x"), "no imageset: none");
}

void TestUtf8() {
  Check(Utf32ToUtf8({'O', 'k'}) == "Ok", "ASCII");
  Check(Utf32ToUtf8({0xE9}) == "\xC3\xA9", "Latin-1");
  Check(Utf32ToUtf8({0x20AC}) == "\xE2\x82\xAC", "three bytes");
  Check(Utf32ToUtf8({0x1F600}) == "\xF0\x9F\x98\x80", "four bytes");
  Check(Utf32ToUtf8({0xD800}) == "\xEF\xBF\xBD", "a surrogate: U+FFFD");
}

void TestJson() {
  UiDump d;
  d.name = "title";
  d.frame = 42;
  d.display_width = 1280;
  d.display_height = 720;
  d.menus = {{".?AVCMainMenu@@", "MainMenu", "Continue"}};
  UiWindow root;
  root.name = "MainMenu";
  root.type = "DefaultWindow";
  root.visible = root.shown = true;
  root.rect = {0, 0, 1280, 720};
  root.clipped = root.rect;
  UiWindow button;
  button.name = "Continue";
  button.type = "ceguiWidget/ImageButton";
  button.visible = true;
  button.shown = true;
  button.rect = {100, 200, 300, 240};
  button.clipped = button.rect;
  button.alpha = 0.5f;
  button.effective_alpha = 0.5f;
  button.text = "Say \"hi\"\n";
  button.images["NormalImage"] = {"widgets", "button"};
  button.properties["tabOrder"] = "1";
  root.children.push_back(button);
  d.windows.push_back(root);
  const std::string j = UiDumpJson(d);
  Check(Has(j, "\"format\": \"torchlight-ui-dump 1\""), "format");
  Check(Has(j, "\"display\": {\"width\": 1280, \"height\": 720}"), "display");
  Check(Has(j, "\"scale_to_1080p\": 1.5"), "scale");
  Check(Has(j, "{\"class\": \".?AVCMainMenu@@\", \"root\": \"MainMenu\", \"focus\": \"Continue\"}"), "menus");
  Check(Has(j, "\"rect\": {\"left\": 100, \"top\": 200, \"right\": 300, \"bottom\": 240, \"width\": 200, \"height\": 40}"),
        "rect in guest pixels");
  Check(Has(j, "\"rect_1080p\": {\"left\": 150, \"top\": 300, \"right\": 450, \"bottom\": 360, \"width\": 300, \"height\": 60}"),
        "rect at 1080p");
  Check(Has(j, "\"text\": \"Say \\\"hi\\\"\\n\""), "text escaped");
  Check(Has(j, "\"NormalImage\": {\"imageset\": \"widgets\", \"image\": \"button\"}"), "images");
  Check(Has(j, "\"alpha\": 0.5"), "alpha");
  Check(Has(j, "\"properties\": {\"tabOrder\": \"1\"}"), "properties");
  int depth = 0, max_depth = 0;
  bool in_string = false, escaped = false;
  for (char c : j) {
    if (in_string) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') in_string = false;
      continue;
    }
    if (c == '"') in_string = true;
    if (c == '{' || c == '[') max_depth = std::max(max_depth, ++depth);
    if (c == '}' || c == ']') --depth;
  }
  Check(depth == 0 && max_depth > 3, "balanced");
}

}  // namespace

int main() {
  TestImageProperty();
  TestUtf8();
  TestJson();
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("ui_tree_test: all passed\n");
  return EXIT_SUCCESS;
}
