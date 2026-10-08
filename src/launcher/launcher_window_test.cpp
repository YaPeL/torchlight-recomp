// Tests for the launcher's window (platform.h, LauncherWindow): it opens with SDL's software
// renderer and its own ImGui context (navigation by keyboard and gamepad, no imgui.ini), draws a
// frame that reaches the window, reports a request to close, and leaves no context and no SDL
// subsystem behind. Without a display (CI) it runs on SDL's offscreen video driver. Also the file
// browser's places of this platform.

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

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
  TestPlaces();
  SDL_Quit();
  if (failures) return 1;
  std::printf("launcher window test: ok\n");
  return 0;
}
