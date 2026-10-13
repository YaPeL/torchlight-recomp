// Tests for the creation screen's class selector model (class_selector_model.h): the carousel over
// the class list and the labels added to the layout.

#include "game_menu/class_selector_model.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace cs = torchlight::game_menu::class_selector;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

std::string Button(const std::string& name, const std::string& click) {
  return "<Window Type=\"w/ImageButton\" Name=\"" + name + "\" >"
         "<Property Name=\"onClick\" Value=\"" + click + "\" />"
         "<Property Name=\"NormalImage\" Value=\"set:a image:" + name + "\" />"
         "</Window>";
}

}  // namespace

int main() {
  // The game's three classes: nothing changes.
  {
    const cs::Carousel three(3);
    Check(!three.Active(), "three classes: the selector is off");
    for (size_t slot = 0; slot < 3; ++slot) Check(three.ClassAt(slot) == slot, "each button its class");
    Check(!three.Scrolls(2, +1) && !three.Scrolls(0, -1), "the game's own wrap at the row's ends");
  }

  // More classes: the buttons slide over the list, wrapping both ways.
  {
    cs::Carousel c(5);
    Check(c.Active(), "five classes: the selector is on");
    Check(c.ClassAt(0) == 0 && c.ClassAt(2) == 2, "the list starts with the game's three");
    Check(c.Scrolls(2, +1) && c.Scrolls(0, -1), "the row's ends scroll");
    Check(!c.Scrolls(1, +1) && !c.Scrolls(1, -1) && !c.Scrolls(0, +1) && !c.Scrolls(2, -1),
          "moves inside the row do not");
    c.Scroll(+1);
    Check(c.ClassAt(0) == 1 && c.ClassAt(2) == 3, "right at the end shows the next class");
    c.Scroll(+3);
    Check(c.ClassAt(0) == 4 && c.ClassAt(1) == 0 && c.ClassAt(2) == 1, "the list wraps forward");
    c.Scroll(-1);
    c.Scroll(-4);
    Check(c.offset() == 4, "and backward");
    c.Reset();
    Check(c.ClassAt(0) == 0, "reset to the start");
  }

  // Labels: one per GUISELECT1 button, inside it; the rest of the layout byte for byte.
  {
    const std::string in = "<GUILayout><Window Type=\"w\" Name=\"root\" >" + Button("Destroyer", "GUISELECT1") +
                           Button("Alchemist", "guiselect1") + Button("Dog", "GUISELECT2") +
                           Button("DoneButton", "GUINEWGAME") + "</Window></GUILayout>";
    const auto out = cs::AddClassLabels(in);
    Check(bool(out), "class buttons get labels");
    if (out) {
      const std::string label = "Name=\"" + cs::LabelName("Destroyer") + "\"";
      Check(out->find(label) != std::string::npos, "a label named after its button");
      Check(out->find(label) < out->find("Name=\"Alchemist\""), "inside its own button");
      Check(out->find(cs::LabelName("Alchemist")) != std::string::npos, "any case of GUISELECT1");
      Check(out->find(cs::LabelName("Dog")) == std::string::npos &&
                out->find(cs::LabelName("DoneButton")) == std::string::npos,
            "other buttons get none");
      Check(out->find("Name=\"Visible\" Value=\"False\"") != std::string::npos, "labels start hidden");
      std::string back = *out;
      for (const char* name : {"Destroyer", "Alchemist"}) {
        const size_t at = back.find("<Window Type=\"ceguiWidget/StaticText\" Name=\"" + cs::LabelName(name));
        back.erase(at, back.find("</Window>", at) + 9 - at);
      }
      Check(back == in, "every original byte is kept");
      Check(!cs::AddClassLabels(*out), "labels are not added twice");
    }
    Check(!cs::AddClassLabels("<GUILayout><Window Type=\"w\" Name=\"a\" /></GUILayout>"),
          "a layout without class buttons is left alone");
  }

  if (failures) {
    std::fprintf(stderr, "%d failure(s)\n", failures);
    return EXIT_FAILURE;
  }
  std::printf("class_selector_model_test: ok\n");
  return EXIT_SUCCESS;
}
