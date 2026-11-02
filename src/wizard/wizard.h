// The setup wizard's window: SDL3 and Dear ImGui (its SDL3 and SDL_Renderer backends) with the SDK's
// green palette, Noto Sans, the owner's cat as its icon. It always uses SDL's software renderer, the
// only one the SDK's SDL has on Linux and Windows (nothing of the GPU's is loaded before the runtime
// chooses one), so it paces itself. A script plays it with a simulated pad and takes the screenshots for the tests.

#pragma once

#include <string>

#include "imgui.h"

struct SDL_Window;

namespace torchlight::wizard {

struct Options {
  std::string title = "Torchlight Recomp";
  int width = 960, height = 600;  // in points: times the scale in pixels
  float scale = 0;                // 0: the display's
  std::string video;              // SDL's video driver ("offscreen" for the tests); empty: SDL's
  bool fullscreen = false;        // a display of 1280x800 or less (the Steam Deck) also is
  std::string script;             // a test's steps (kept in wizard.cpp)
  std::string shots;              // the folder the script's screenshots go to (PNG)
};

struct Fonts {
  ImFont* regular = nullptr;
  ImFont* bold = nullptr;
};

// What the window shows, drawn each frame.
class Pages {
 public:
  virtual ~Pages() = default;
  // Once, before the first frame.
  virtual void Attach(const Fonts& fonts, SDL_Window* window) = 0;
  // Draws this frame; true once the player is done.
  virtual bool Frame() = 0;
  // The page shown: the scripts wait for one.
  virtual std::string Name() const = 0;
  // More of what the page shows, as text, for a script's `detail` step.
  virtual std::string Detail() const { return {}; }
  // Work under way: the window does not close then.
  virtual bool Busy() const { return false; }
};

inline constexpr int kNoWindow = 2;
// Opens the window and runs until it is closed. 0 when it ran, kNoWindow when no window could be
// opened, 3 when a script failed.
int Run(const Options& options, Pages& pages);

}  // namespace torchlight::wizard
