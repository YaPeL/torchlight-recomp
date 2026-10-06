// Tests for WidenBands on synthetic layouts: the band ends of a frame move to the edges of a
// centred 16:9 area, other edge-anchored images do not, and the result lays out as before at 16:9.

#include "game_menu/wide_layout.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using torchlight::game_menu::WidenBands;

namespace {

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

std::string Image(const std::string& name, const std::string& area, const std::string& extra = "") {
  return "<Window Type=\"set/StaticImage\" Name=\"" + name + "\" >"
         "<Property Name=\"Image\" Value=\"set:a image:" + name + "\" />"
         "<Property Name=\"UnifiedAreaRect\" Value=\"" + area + "\" />" + extra + "</Window>";
}
std::string Prop(const std::string& name, const std::string& value) {
  return "<Property Name=\"" + name + "\" Value=\"" + value + "\" />";
}
std::string Full(const std::string& children) {
  return "<Window Type=\"set/DefaultWindow\" Name=\"root\" >" +
         Prop("UnifiedAreaRect", "{{0,0},{0,0},{1,0},{1,0}}") + children + "</Window>";
}
std::string Layout(const std::string& body) { return "<GUILayout >" + body + "</GUILayout>"; }

// A frame as menus build it: corners of 215 px anchored to the screen edges, centre pieces of
// 1024 px (one centred by scale, one by alignment).
const std::string kFrame = Layout(Full(
    Image("top_left", "{{0,0},{0,0},{0,215},{0,87}}") +
    Image("top_right", "{{0,0},{0,0},{0,215},{0,87}}", Prop("HorizontalAlignment", "Right")) +
    Image("top_centre", "{{0.5,-512},{0,0},{0.5,512},{0,141}}") +
    Image("bottom_left", "{{0,0},{0,0},{0,215},{0,93}}", Prop("VerticalAlignment", "Bottom")) +
    Image("bottom_right", "{{0,0},{0,0},{0,215},{0,93}}",
          Prop("VerticalAlignment", "Bottom") + Prop("HorizontalAlignment", "Right")) +
    Image("bottom_centre", "{{0,0},{0,0},{0,1024},{0,93}}",
          Prop("VerticalAlignment", "Bottom") + Prop("HorizontalAlignment", "Centre"))));

}  // namespace

int main() {
  {
    auto out = WidenBands(kFrame);
    Check(bool(out), "a frame has band ends");
    if (out) {
      Check(out->windows.size() == 4, "the four corners move");
      Check(Has(out->xml, "{{0.5,-640},{0,0},{0.5,-425},{0,87}}"), "top left at the 16:9 left edge");
      Check(Has(out->xml, "{{0.5,425},{0,0},{0.5,640},{0,87}}"), "top right at the 16:9 right edge");
      Check(Has(out->xml, "{{0.5,-640},{0,0},{0.5,-425},{0,93}}"), "bottom left keeps its height");
      Check(!Has(out->xml, "\"Right\""), "right alignment becomes left (offsets from the centre)");
      Check(Has(out->xml, "\"Bottom\""), "vertical alignment kept");
      Check(Has(out->xml, "{{0.5,-512},{0,0},{0.5,512},{0,141}}"), "centre pieces untouched");
    }
  }
  {
    // A centre piece nested in a centred container (as loading screens do) still forms a band.
    const std::string layout = Layout(Full(
        Image("left", "{{0,0},{0,0},{0,215},{0,87}}") +
        "<Window Type=\"set/DefaultWindow\" Name=\"box\" >" +
        Prop("UnifiedAreaRect", "{{0,0},{0,0},{0,1024},{0,768}}") +
        Prop("HorizontalAlignment", "Centre") +
        Image("centre", "{{0.5,-512},{0,0},{0.5,512},{0,141}}") + "</Window>"));
    auto out = WidenBands(layout);
    Check(out && out->windows.size() == 1 && out->windows[0] == "left", "nested centre piece");
  }
  {
    // An edge-anchored panel with no centred piece at its edge (a menu background) stays.
    const std::string layout =
        Layout(Full(Image("panel", "{{0,0},{0,0},{0,303},{0,629}}") +
                    Image("hud", "{{0,0},{0,0},{0,160},{0,160}}", Prop("HorizontalAlignment", "Right"))));
    Check(!WidenBands(layout), "panels without a band stay");
  }
  {
    // A centred image that does not reach the corner leaves it alone (no overlap at 1280).
    const std::string layout = Layout(Full(Image("left", "{{0,0},{0,0},{0,100},{0,87}}") +
                                           Image("centre", "{{0.5,-200},{0,0},{0.5,200},{0,87}}")));
    Check(!WidenBands(layout), "no overlap, no band");
  }
  {
    // Not in a full-width parent: the corner is relative to something else.
    const std::string layout = Layout(
        "<Window Type=\"set/DefaultWindow\" Name=\"half\" >" +
        Prop("UnifiedAreaRect", "{{0,0},{0,0},{0.5,0},{1,0}}") +
        Image("left", "{{0,0},{0,0},{0,215},{0,87}}") +
        Image("centre", "{{0.5,-512},{0,0},{0.5,512},{0,141}}") + "</Window>");
    Check(!WidenBands(layout), "corner in a half-width parent stays");
  }
  Check(!WidenBands("<GUILayout ><Window Type=\"x\" Name=\"a\" >"), "unbalanced layout refused");
  if (failures) return EXIT_FAILURE;
  std::puts("wide_layout_test: ok");
  return EXIT_SUCCESS;
}
