// WindowDrawn: visible up to the active sheet, over a made-up window tree.

#include <cstdio>
#include <map>

#include "guest_abi/game_ui.h"
#include "hooks/gui_window_drawn.h"

namespace {
int g_failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++g_failures;
  }
}
}  // namespace

int main() {
  namespace ui = torchlight::guest_abi::game_ui;
  using torchlight::hooks::WindowDrawn;
  std::map<uint32_t, uint32_t> parent;
  std::map<uint32_t, uint8_t> visible;
  auto window = [&](uint32_t w, uint32_t p, bool v) {
    parent[w + ui::kWindowParent.offset] = p;
    visible[w + ui::kWindowVisible.offset] = v;
  };
  auto read8 = [&](uint32_t a) { return visible.count(a) ? visible[a] : uint8_t{0}; };
  auto read32 = [&](uint32_t a) { return parent.count(a) ? parent[a] : 0u; };
  const uint32_t sheet = 0x1000;
  window(sheet, 0, true);
  window(0x2000, sheet, true);    // an open panel
  window(0x3000, 0x2000, true);   // its arrow
  window(0x4000, sheet, false);   // a closed panel
  window(0x5000, 0x4000, true);   // its arrow, visible itself
  window(0x6000, 0, true);        // a panel detached from the tree
  window(0x7000, 0x6000, true);   // its arrow
  window(0x8000, 0x8000, true);   // a window that is its own parent

  Check(WindowDrawn(0x3000, sheet, read8, read32), "the open panel's arrow is drawn");
  Check(!WindowDrawn(0x5000, sheet, read8, read32), "a hidden ancestor: not drawn");
  Check(!WindowDrawn(0x7000, sheet, read8, read32), "not under the active sheet: not drawn");
  Check(!WindowDrawn(0x8000, sheet, read8, read32), "a cycle: not drawn");
  Check(!WindowDrawn(0, sheet, read8, read32), "no window");
  Check(!WindowDrawn(0x3000, 0, read8, read32), "no active sheet");
  if (g_failures) return 1;
  std::puts("gui_window_drawn_test: ok");
  return 0;
}
