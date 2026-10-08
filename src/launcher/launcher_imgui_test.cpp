// One ImGui in the process (docs/launcher.md, stage 1 step 0): the launcher's SDL3 backends
// (third_party/imgui_backends) work on the ImGui the SDK builds into rex::runtime, not on a copy of
// their own. Checks the version and the structures' layout against the compiled library, that the
// backends register in the runtime's current context, that contexts come and go cleanly (the
// launcher's is gone before the runtime's drawer creates its own), and draws one frame with SDL's
// software renderer on a surface (no window, so no display needed). With a display, the SDL3
// platform backend is started on a hidden window too.

#include <cstdio>
#include <cstring>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "platform/platform.h"

static_assert(IMGUI_VERSION_NUM == 19250,
              "third_party/imgui_backends is ImGui 1.92.5's: update it with the SDK's ImGui");

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
  if (!ok) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    ++failures;
  }
}

constexpr int kWidth = 320, kHeight = 200;

// The pixel at (x, y) of an RGBA32 surface.
const Uint8* At(const SDL_Surface* rgba, int x, int y) {
  return static_cast<const Uint8*>(rgba->pixels) + y * rgba->pitch + x * 4;
}

// A frame drawn by the SDL_Renderer backend on a software renderer: a red square through the
// background draw list and a line of text (which needs the font atlas's texture, created by the
// backend when the frame is rendered).
void DrawFrame() {
  SDL_Surface* target = SDL_CreateSurface(kWidth, kHeight, SDL_PIXELFORMAT_RGBA32);
  SDL_Renderer* renderer = target ? SDL_CreateSoftwareRenderer(target) : nullptr;
  if (!renderer) {
    std::fprintf(stderr, "FAIL: software renderer: %s\n", SDL_GetError());
    ++failures;
    if (target) SDL_DestroySurface(target);
    return;
  }
  Check(ImGui::GetCurrentContext() == nullptr, "no ImGui context before the launcher's");
  ImGuiContext* context = ImGui::CreateContext();
  Check(ImGui::GetCurrentContext() == context, "the launcher's context is the current one");
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.Fonts->AddFontDefault();
  Check(ImGui_ImplSDLRenderer3_Init(renderer), "SDL_Renderer backend started");
  Check(io.BackendRendererName && std::strcmp(io.BackendRendererName, "imgui_impl_sdlrenderer3") == 0,
        "the backend registered in the runtime's ImGui context");
  Check((io.BackendFlags & ImGuiBackendFlags_RendererHasTextures) != 0,
        "the backend updates textures (ImGui 1.92's font atlas)");

  io.DisplaySize = ImVec2(float(kWidth), float(kHeight));
  io.DeltaTime = 1.0f / 60.0f;
  ImGui_ImplSDLRenderer3_NewFrame();
  ImGui::NewFrame();
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(10, 10), ImVec2(60, 60),
                                                IM_COL32(255, 0, 0, 255));
  ImGui::GetForegroundDrawList()->AddText(ImVec2(100, 20), IM_COL32(255, 255, 255, 255),
                                          "Torchlight Recomp");
  ImGui::Render();
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);
  ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
  SDL_FlushRenderer(renderer);

  SDL_Surface* rgba = SDL_ConvertSurface(target, SDL_PIXELFORMAT_RGBA32);
  if (rgba) {
    const Uint8* inside = At(rgba, 35, 35);
    const Uint8* outside = At(rgba, 80, 80);
    Check(inside[0] > 247 && inside[1] < 8 && inside[2] < 8, "red square drawn");
    Check(outside[0] < 8 && outside[1] < 8 && outside[2] < 8, "black around it");
    int lit = 0;
    for (int y = 18; y < 40; ++y) {
      for (int x = 98; x < 260; ++x) lit += At(rgba, x, y)[0] > 128;
    }
    Check(lit > 20, "text drawn from the font atlas");
    SDL_DestroySurface(rgba);
  } else {
    Check(false, "read the frame back");
  }

  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui::DestroyContext(context);
  Check(ImGui::GetCurrentContext() == nullptr, "no ImGui context after the launcher's");
  SDL_DestroyRenderer(renderer);
  SDL_DestroySurface(target);
}

// The SDL3 platform backend (input, window size) on a hidden window, in a second context: the
// launcher's and then another one, as the runtime's drawer follows the launcher.
void StartPlatformBackend() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "FAIL: SDL video: %s\n", SDL_GetError());
    ++failures;
    return;
  }
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  if (!SDL_CreateWindowAndRenderer("launcher_imgui_test", kWidth, kHeight, SDL_WINDOW_HIDDEN,
                                   &window, &renderer)) {
    std::fprintf(stderr, "FAIL: hidden window: %s\n", SDL_GetError());
    ++failures;
    SDL_Quit();
    return;
  }
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  Check(ImGui_ImplSDL3_InitForSDLRenderer(window, renderer), "SDL3 platform backend started");
  const char* name = ImGui::GetIO().BackendPlatformName;
  // "imgui_impl_sdl3 (<SDL compiled>; <SDL linked>)".
  Check(name && std::strncmp(name, "imgui_impl_sdl3 ", 16) == 0, "platform backend registered");
  ImGui_ImplSDL3_NewFrame();
  Check(ImGui::GetIO().DisplaySize.x == float(kWidth), "display size from the window");
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(context);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

}  // namespace

int main() {
  Check(IMGUI_CHECKVERSION(), "ImGui's version and structures match the runtime's");
  Check(std::strcmp(ImGui::GetVersion(), IMGUI_VERSION) == 0, "the runtime's ImGui is 1.92.5");
  DrawFrame();
  if (torchlight::platform::HasDisplay()) {
    StartPlatformBackend();
  } else {
    std::printf("launcher imgui test: no display, platform backend not started\n");
  }
  if (failures) return 1;
  std::printf("launcher imgui test: ok (ImGui %s)\n", ImGui::GetVersion());
  return 0;
}
