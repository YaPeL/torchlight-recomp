#include "wizard/wizard.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "platform/platform_sdl.h"
#include "wizard/wizard_ui.h"

namespace torchlight::wizard {

// Built in (CMakeLists.txt): Noto Sans and data/icon/wizard-64.png.
extern const unsigned char kFontRegular[];
extern const size_t kFontRegularSize;
extern const unsigned char kFontBold[];
extern const size_t kFontBoldSize;
extern const unsigned char kWindowIcon[];
extern const size_t kWindowIconSize;

namespace {

// The software renderer has no vsync of its own to wait on.
constexpr Uint64 kFrameNanoseconds = SDL_NS_PER_SECOND / 60;

// The ReXGlue SDK's ImGui palette (its src/ui/imgui_drawer.cpp), the recomp's look, with Noto Sans
// for a text that reads at the wizard's size.
void ApplyTheme(ImGuiStyle& style, float scale) {
  ImGui::StyleColorsDark(&style);
  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = ImVec4(0.89f, 0.90f, 0.90f, 1.00f);
  c[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
  c[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.06f, 0.00f, 1.00f);
  c[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_Border] = ImVec4(0.00f, 0.35f, 0.00f, 1.00f);
  c[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  c[ImGuiCol_FrameBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.30f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.90f, 0.80f, 0.80f, 0.40f);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.90f, 0.65f, 0.65f, 0.45f);
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.40f, 0.11f, 0.59f);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 0.68f, 0.00f, 0.68f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 1.00f, 0.15f, 0.62f);
  c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 0.91f, 0.09f, 0.40f);
  c[ImGuiCol_PopupBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.99f);
  c[ImGuiCol_CheckMark] = ImVec4(0.74f, 0.90f, 0.72f, 0.50f);
  c[ImGuiCol_Button] = ImVec4(0.15f, 0.56f, 0.11f, 0.60f);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.19f, 0.72f, 0.09f, 1.00f);
  c[ImGuiCol_ButtonActive] = ImVec4(0.19f, 0.60f, 0.09f, 1.00f);
  c[ImGuiCol_Header] = ImVec4(0.00f, 0.40f, 0.00f, 0.71f);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.60f, 0.26f, 0.80f);
  c[ImGuiCol_HeaderActive] = ImVec4(0.00f, 0.75f, 0.00f, 0.80f);
  c[ImGuiCol_Separator] = ImVec4(0.00f, 0.35f, 0.00f, 1.00f);
  c[ImGuiCol_SeparatorHovered] = ImVec4(0.36f, 0.89f, 0.38f, 1.00f);
  c[ImGuiCol_SeparatorActive] = ImVec4(0.13f, 0.50f, 0.11f, 1.00f);
  c[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
  c[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 0.00f, 0.21f);
  // The wizard's own: the pad's focus, as visible as the SDK's hovered button.
  c[ImGuiCol_NavCursor] = ImVec4(0.36f, 0.89f, 0.38f, 1.00f);
  style.WindowRounding = 0;
  style.ScrollbarRounding = 0;
  style.FrameRounding = 4;
  style.ChildRounding = 4;
  style.WindowPadding = ImVec2(28, 22);
  style.FramePadding = ImVec2(14, 8);
  style.ItemSpacing = ImVec2(12, 10);
  style.ScrollbarSize = 16;
  // SDL's software renderer loses long, thin lines drawn from ImGui's line texture (the boxes'
  // borders at 1280 wide), and pieces of the plain ones: geometry with its own edge fade keeps them.
  style.AntiAliasedLinesUseTex = false;
  style.ScaleAllSizes(scale);
  style.FontSizeBase = kTextSize;
  style.FontScaleDpi = scale;
}

// A script: one step per line, `#` for a comment. wait FRAMES, press BUTTON (a b x y up down left
// right start back), key KEY (enter escape tab up down left right space backspace), shot NAME,
// page NAME [SECONDS] (waits for it, or fails), detail TEXT (waits up to 5 s for Pages::Detail to end
// in it), finished (the pages must end by themselves within 5 s), quit. A script fails unless it
// reaches `quit` or its pages end at `finished`.
struct Step {
  std::string op, arg;
  double seconds = 30;
};

bool ReadScript(const std::string& path, std::vector<Step>& steps) {
  std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())));
  if (!file) return false;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    std::istringstream words(line);
    Step step;
    words >> step.op;
    if (step.op == "detail") {
      std::getline(words >> std::ws, step.arg);
    } else {
      words >> step.arg;
    }
    if (double seconds; step.op == "page" && words >> seconds) step.seconds = seconds;
    steps.push_back(std::move(step));
  }
  return true;
}

int ButtonOf(const std::string& name) {
  static const std::pair<const char*, int> names[] = {
      {"a", SDL_GAMEPAD_BUTTON_SOUTH},        {"b", SDL_GAMEPAD_BUTTON_EAST},
      {"x", SDL_GAMEPAD_BUTTON_WEST},         {"y", SDL_GAMEPAD_BUTTON_NORTH},
      {"up", SDL_GAMEPAD_BUTTON_DPAD_UP},     {"down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
      {"left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
      {"start", SDL_GAMEPAD_BUTTON_START},    {"back", SDL_GAMEPAD_BUTTON_BACK}};
  for (const auto& [text, button] : names) {
    if (name == text) return button;
  }
  return -1;
}

ImGuiKey KeyOf(const std::string& name) {
  static const std::pair<const char*, ImGuiKey> names[] = {
      {"enter", ImGuiKey_Enter}, {"escape", ImGuiKey_Escape},    {"tab", ImGuiKey_Tab},
      {"up", ImGuiKey_UpArrow},  {"down", ImGuiKey_DownArrow},   {"left", ImGuiKey_LeftArrow},
      {"right", ImGuiKey_RightArrow}, {"space", ImGuiKey_Space}, {"backspace", ImGuiKey_Backspace}};
  for (const auto& [text, key] : names) {
    if (name == text) return key;
  }
  return ImGuiKey_None;
}

// A hint for this window only: on leaving, the value it had before, or none.
class ScopedHint {
 public:
  ScopedHint(const char* name, const char* value) : name_(name) {
    if (const char* before = SDL_GetHint(name)) before_ = before;
    SDL_SetHint(name, value);
  }
  ~ScopedHint() {
    if (before_) {
      SDL_SetHint(name_, before_->c_str());
    } else {
      SDL_ResetHint(name_);
    }
  }

 private:
  const char* name_;
  std::optional<std::string> before_;
};

bool SaveShot(SDL_Renderer* renderer, const std::string& path) {
  SDL_Surface* shot = SDL_RenderReadPixels(renderer, nullptr);
  if (!shot) return false;
  const bool saved = SDL_SavePNG(shot, path.c_str());
  SDL_DestroySurface(shot);
  return saved;
}

void SetIcon(SDL_Window* window) {
  SDL_IOStream* io = SDL_IOFromConstMem(kWindowIcon, kWindowIconSize);
  if (!io) return;
  if (SDL_Surface* icon = SDL_LoadPNG_IO(io, true)) {
    SDL_SetWindowIcon(window, icon);
    SDL_DestroySurface(icon);
  }
}

}  // namespace

int Run(const Options& options, Pages& pages) {
  // The runtime's SDL may be up already, with its event watches and hints: left as it was found, so
  // no SDL_Quit, which would take them all down whatever the count of who uses it.
  std::optional<ScopedHint> video_hint;
  if (!options.video.empty()) video_hint.emplace(SDL_HINT_VIDEO_DRIVER, options.video.c_str());
  const ScopedHint render_hint(SDL_HINT_RENDER_DRIVER, "software");
  const ScopedHint name_hint(SDL_HINT_APP_NAME, options.title.c_str());
  // Linux: the desktop portal's pickers only; where there is none (the Steam Deck's Game Mode), the
  // pages' own browser.
  const ScopedHint picker_hint(SDL_HINT_FILE_DIALOG_DRIVER, "portal");
  constexpr SDL_InitFlags kSubsystems = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
  if (!SDL_InitSubSystem(kSubsystems)) {
    std::fprintf(stderr, "wizard: no window: %s\n", SDL_GetError());
    return 2;
  }
  float scale = options.scale > 0 ? options.scale
                                  : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if (scale <= 0) scale = 1;
  bool fullscreen = options.fullscreen;
  if (options.script.empty()) {
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w <= 1280 &&
        usable.h <= 800) {
      fullscreen = true;
    }
  }
  SDL_Window* window = SDL_CreateWindow(
      options.title.c_str(), int(float(options.width) * scale), int(float(options.height) * scale),
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY |
          (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
  SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
  if (!renderer) {
    std::fprintf(stderr, "wizard: no window: %s\n", SDL_GetError());
    if (window) SDL_DestroyWindow(window);
    SDL_QuitSubSystem(kSubsystems);
    return 2;
  }
  SetIcon(window);
  platform::BringToFront();  // macOS: it would open behind what is in front (docs/macos-port.md)
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGuiContext* previous_context = ImGui::GetCurrentContext();
  ImGuiContext* context = ImGui::CreateContext();
  ImGui::SetCurrentContext(context);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // nothing written beside the executable
  io.LogFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
  ApplyTheme(ImGui::GetStyle(), scale);
  ImFontConfig config;
  config.FontDataOwnedByAtlas = false;  // the fonts are the executable's
  Fonts fonts;
  fonts.regular = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontRegular),
                                                 int(kFontRegularSize), kTextSize, &config);
  fonts.bold = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontBold),
                                              int(kFontBoldSize), kTextSize, &config);
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  // The script's simulated pad, a virtual gamepad SDL lists as any other. ImGui hears only this one:
  // a real pad on the machine would come first.
  std::string error;
  std::vector<Step> steps;
  SDL_Gamepad* gamepad = nullptr;
  SDL_Joystick* pad = nullptr;
  if (!options.script.empty()) {
    if (!ReadScript(options.script, steps)) error = "cannot read the script " + options.script;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "wizard test pad";
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    gamepad = id ? SDL_OpenGamepad(id) : nullptr;
    pad = gamepad ? SDL_GetGamepadJoystick(gamepad) : nullptr;
    if (pad) {
      ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, &gamepad, 1);
    } else if (error.empty()) {
      error = std::string("no simulated pad: ") + SDL_GetError();
    }
  }

  pages.Attach(fonts, window);
  size_t next = 0;
  int wait = 3, held = -1, hold_frames = 0;
  constexpr int kReleased = 1000;  // `held`: the button is up again, for a few frames
  Uint64 page_since = 0;
  bool quit = !error.empty();
  bool script_done = false;
  Uint64 frame_start = SDL_GetTicksNS();
  std::vector<SDL_Event> others;
  while (!quit) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      // The application's own (the runtime's): for its event loop, once this one ends.
      if (event.type >= SDL_EVENT_USER && event.type <= SDL_EVENT_LAST) {
        others.push_back(event);
        continue;
      }
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT ||
          (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
           event.window.windowID == SDL_GetWindowID(window))) {
        quit = !pages.Busy();
      }
    }
    if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
      SDL_Delay(10);
      continue;
    }
    std::string shot;
    // One script step when the last is done (a press: three frames held, three released).
    if (pad && held >= 0 && --hold_frames <= 0) {
      if (held < kReleased) {
        SDL_SetJoystickVirtualButton(pad, held, false);
        held = kReleased;
        hold_frames = 3;
      } else {
        held = -1;
      }
    } else if (held < 0 && --wait <= 0 && next < steps.size()) {
      const Step& step = steps[next];
      wait = 1;
      if (step.op == "wait") {
        wait = std::atoi(step.arg.c_str());
        if (wait < 1) error = "wait needs a number of frames: " + step.arg;
        next++;
      } else if (step.op == "press") {
        const int button = ButtonOf(step.arg);
        if (button < 0) {
          error = "no button " + step.arg;
        } else {
          SDL_SetJoystickVirtualButton(pad, button, true);
          held = button;
          hold_frames = 3;
        }
        next++;
      } else if (step.op == "key") {
        const ImGuiKey key = KeyOf(step.arg);
        if (key == ImGuiKey_None) {
          error = "no key " + step.arg;
        } else {
          io.AddKeyEvent(key, true);
          io.AddKeyEvent(key, false);
        }
        wait = 3;
        next++;
      } else if (step.op == "shot") {
        shot = step.arg;
        if (shot.empty()) error = "shot needs a name";
        next++;
      } else if (step.op == "page") {
        if (!page_since) page_since = SDL_GetTicks();
        if (pages.Name() == step.arg) {
          page_since = 0;
          wait = 4;  // drawn twice before the next step (ImGui settles its layout)
          next++;
        } else if (double(SDL_GetTicks() - page_since) > step.seconds * 1000) {
          error = "the page " + step.arg + " was not reached (still " + pages.Name() + ")";
        }
      } else if (step.op == "detail" && step.arg.empty()) {
        error = "detail needs a text";
      } else if (step.op == "detail") {
        if (!page_since) page_since = SDL_GetTicks();
        const std::string detail = pages.Detail();
        if (detail.ends_with(step.arg)) {
          page_since = 0;
          wait = 4;
          next++;
        } else if (SDL_GetTicks() - page_since > 5000) {
          error = "the detail is " + detail + ", not " + step.arg;
        }
      } else if (step.op == "finished") {
        if (!page_since) page_since = SDL_GetTicks();
        if (SDL_GetTicks() - page_since > 5000) error = "the pages did not end";
      } else if (step.op == "quit") {
        script_done = true;
        quit = true;
      } else {
        error = "no step " + step.op;
      }
    } else if (pad && held < 0 && next >= steps.size()) {
      error = "the script ended without quit";
    }
    if (!error.empty()) quit = true;

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    if (pages.Frame()) {
      quit = true;
      if (next < steps.size() && steps[next].op == "finished") script_done = true;
    }
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColorFloat(renderer, 0.0f, 0.06f, 0.0f, 1.0f);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    if (!shot.empty()) {
      const std::string path = options.shots + "/" + shot + ".png";
      if (options.shots.empty() || !SaveShot(renderer, path)) {
        error = "cannot write the screenshot " + path + ": " + SDL_GetError();
        quit = true;
      }
    }
    SDL_RenderPresent(renderer);
    const Uint64 spent = SDL_GetTicksNS() - frame_start;
    if (spent < kFrameNanoseconds) SDL_DelayPrecise(kFrameNanoseconds - spent);
    frame_start = SDL_GetTicksNS();
  }
  if (error.empty() && !steps.empty() && !script_done) {
    error = "the window closed before the script's end, at step " + std::to_string(next + 1);
  }
  if (!error.empty()) std::fprintf(stderr, "wizard: %s\n", error.c_str());
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext(context);
  ImGui::SetCurrentContext(previous_context);
  if (gamepad) SDL_CloseGamepad(gamepad);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  // Nothing of this window for the game's event loop, such as a key still going up.
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_USER - 1);
  for (SDL_Event& event : others) SDL_PushEvent(&event);
  SDL_QuitSubSystem(kSubsystems);
  return error.empty() ? 0 : 3;
}

}  // namespace torchlight::wizard
