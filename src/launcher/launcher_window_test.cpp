// Tests for the launcher's window (platform.h, LauncherWindow): it opens with SDL's software
// renderer and its own ImGui context (navigation by keyboard and gamepad, no imgui.ini), draws a
// frame that reaches the window, reports a request to close, and leaves no context and no SDL
// subsystem behind. Without a display (CI) it runs on SDL's offscreen video driver. Also the
// launcher's font (every character of tl_setup_strings.txt is in it), the install's progress window
// drawn with it, and the file browser's places of this platform.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>

#include "platform/platform.h"

namespace {

namespace platform = torchlight::platform;

int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

// The color of the window's pixel at (x, y) in ImGui's coordinates, read from the window's surface
// (the software renderer draws there).
bool ReadPixel(float x, float y, Uint8& r, Uint8& g, Uint8& b) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* window = windows && count == 1 ? windows[0] : nullptr;
  SDL_free(windows);
  SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
  if (!surface) return false;
  const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
  Uint8 a = 0;
  return SDL_ReadSurfacePixel(surface, int(x * scale.x), int(y * scale.y), &r, &g, &b, &a);
}

void TestWindow() {
  std::string error;
  auto window = platform::LauncherWindow::Open("launcher_window_test", error);
  Check(window != nullptr, "the window opens");
  if (!window) {
    std::fprintf(stderr, "  %s\n", error.c_str());
    return;
  }
  Check(ImGui::GetCurrentContext() != nullptr, "its ImGui context");
  const ImGuiIO& io = ImGui::GetIO();
  Check(io.IniFilename == nullptr && io.LogFilename == nullptr, "no files written");
  Check((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) &&
            (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad),
        "keyboard and gamepad navigation");
  Check(window->scale() > 0, "a scale");
  std::string second_error;
  Check(!platform::LauncherWindow::Open("second", second_error) && !second_error.empty(),
        "no second window over a current context");

  Check(!window->NewFrame(), "a frame, nobody asked to close");
  Check(io.DisplaySize.x > 0 && io.DisplaySize.y > 0, "the display size from the window");
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(60, 60),
                                                IM_COL32(255, 0, 0, 255));
  ImGui::Begin("Test");
  ImGui::Button("Play");
  ImGui::End();
  window->Present();
  Uint8 r = 0, g = 0, b = 0;
  Check(ReadPixel(35, 35, r, g, b) && r == 255 && g == 0 && b == 0, "the drawing reached it");
  Check(ReadPixel(io.DisplaySize.x - 5, io.DisplaySize.y - 5, r, g, b) && r == 24 && g == 20 &&
            b == 16,
        "the launcher's background around it");

  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
  Check(window->NewFrame(), "a request to close is reported");
  window->Present();
  window.reset();
  Check(ImGui::GetCurrentContext() == nullptr, "no ImGui context after the window");
  Check(SDL_WasInit(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) == 0, "no SDL subsystem left");
}

// The code points of UTF-8 text (no validation: the file is ours).
std::vector<unsigned int> CodePoints(const std::string& text) {
  std::vector<unsigned int> out;
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    const size_t length = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    unsigned int code = length == 1 ? c : c & (0x3F >> (length - 1));
    for (size_t k = 1; k < length && i + k < text.size(); ++k) {
      code = (code << 6) | (static_cast<unsigned char>(text[i + k]) & 0x3F);
    }
    out.push_back(code);
    i += length;
  }
  return out;
}

// Every character of the launcher's texts (their translations included) has a glyph in its font.
void TestFont() {
  std::string error;
  auto window = platform::LauncherWindow::Open("launcher_window_test", error);
  if (!window) return;
  const ImFont* font = ImGui::GetIO().Fonts->Fonts.Size ? ImGui::GetIO().Fonts->Fonts[0] : nullptr;
  Check(font != nullptr, "the launcher's font is loaded");
  std::ifstream file(TL_SETUP_STRINGS_FILE, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  Check(!text.empty(), "the texts' file reads");
  int missing = 0, characters = 0;
  for (const unsigned int c : CodePoints(text)) {
    if (!font || c < 0x20) continue;
    ++characters;
    if (!const_cast<ImFont*>(font)->IsGlyphInFont(ImWchar(c))) {
      if (missing++ < 5) std::fprintf(stderr, "  no glyph for U+%04X\n", c);
    }
  }
  Check(characters > 1000 && missing == 0, "every character of the texts is in the font");
}

// The progress window: the bar drawn to its fraction, a request to close reported.
void TestProgressWindow() {
  auto window = platform::ProgressWindow::Open("launcher_window_test");
  Check(window != nullptr, "the progress window opens");
  if (!window) return;
  Check(ImGui::GetCurrentContext() != nullptr, "drawn with ImGui");
  Check(window->Show(0.5, "Ñandú: Spieldateien werden geprüft ..."),
        "shown, nobody asked to close");
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Surface* surface = windows && count == 1 ? SDL_GetWindowSurface(windows[0]) : nullptr;
  SDL_free(windows);
  // The bar's color in the left two fifths, none in the right two.
  int left = 0, right = 0;
  for (int y = 0; surface && y < surface->h; ++y) {
    for (int x = 0; x < surface->w; ++x) {
      Uint8 r = 0, g = 0, b = 0, a = 0;
      SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, &a);
      if (r != 220 || g != 180 || b != 60) continue;
      if (x < surface->w * 2 / 5) ++left;
      if (x > surface->w * 3 / 5) ++right;
    }
  }
  Check(left > 0 && right == 0, "the bar filled to half");
  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  SDL_PushEvent(&quit);
  Check(!window->Show(0.5, "x"), "a request to close is reported");
  window.reset();
  Check(ImGui::GetCurrentContext() == nullptr, "no ImGui context after it");
}

void TestPlaces() {
  const auto places = platform::BrowsePlaces();
  Check(!places.empty(), "the browser has places");
  for (const auto& place : places) {
    std::error_code ec;
    Check(std::filesystem::is_directory(place, ec), "every place is a folder");
  }
}

}  // namespace

int main() {
  if (!platform::HasDisplay()) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    std::printf("no display: SDL's offscreen video driver\n");
  }
  TestWindow();
  TestFont();
  TestProgressWindow();
  TestPlaces();
  SDL_Quit();
  if (failures) return 1;
  std::printf("launcher window test: ok\n");
  return 0;
}
