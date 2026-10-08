// The parts of the platform module that are plain SDL3, the same on every platform: the game
// window (SDL's) and its display, and the gamepad. Each platform file adds its native handles
// (platform_sdl.h) and everything else in platform.h.

#include "platform/platform.h"
#include "platform/platform_sdl.h"
#include "platform/text_wrap.h"

#include <algorithm>
#include <cctype>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace torchlight::platform {

const char* SystemName(const NativeWindow& window) {
  switch (window.system) {
    case NativeWindow::kX11: return "x11";
    case NativeWindow::kWayland: return "wayland";
    case NativeWindow::kWin32: return "win32";
    default: return "none";
  }
}

std::string VideoDriver() {
  const char* current = SDL_GetCurrentVideoDriver();
  return current ? current : "none";
}

bool FindGameWindow(NativeWindow& window, uint32_t& width, uint32_t& height, std::string& error) {
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  if (!windows) {
    error = "no SDL windows";
    return false;
  }
  SDL_Window* game = count == 1 ? windows[0] : nullptr;
  SDL_free(windows);
  if (!game) {
    error = "expected exactly one SDL window (the game window), found " + std::to_string(count);
    return false;
  }
  const bool handles = NativeGameWindow(game, window);
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(game, &w, &h);
  width = uint32_t(w);
  height = uint32_t(h);
  if (!handles) {
    error = std::string("the game window has no ") + SystemName(window) + " handles";
    return false;
  }
  return true;
}

DisplayInfo GameDisplay() {
  DisplayInfo info;
  int count = 0;
  SDL_Window** windows = SDL_GetWindows(&count);
  SDL_Window* game = windows && count == 1 ? windows[0] : nullptr;
  SDL_free(windows);
  const SDL_DisplayID display = game ? SDL_GetDisplayForWindow(game) : SDL_GetPrimaryDisplay();
  if (!display) return info;
  // SDL3 display modes are in points; pixel_density turns them into pixels.
  auto pixels = [](const SDL_DisplayMode& mode) {
    const float density = mode.pixel_density > 0 ? mode.pixel_density : 1.0f;
    return std::pair<uint32_t, uint32_t>(uint32_t(mode.w * density + 0.5f),
                                         uint32_t(mode.h * density + 0.5f));
  };
  if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display)) {
    std::tie(info.width, info.height) = pixels(*desktop);
    info.refresh_hz = uint32_t(desktop->refresh_rate + 0.5f);
  }
  int mode_count = 0;
  if (SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &mode_count)) {
    for (int i = 0; i < mode_count; ++i) info.modes.push_back(pixels(*modes[i]));
    SDL_free(modes);
  }
  return info;
}

void AllowBackgroundGamepad() { SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1"); }

GamepadState ReadGamepad() {
  // Opened once (SDL counts the opens, the runtime's input driver has its own) and again after a
  // disconnection.
  static SDL_Gamepad* gamepad = nullptr;
  if (gamepad && !SDL_GamepadConnected(gamepad)) {
    SDL_CloseGamepad(gamepad);
    gamepad = nullptr;
  }
  if (!gamepad) {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0) gamepad = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
  }
  GamepadState s;
  if (!gamepad) return s;
  s.connected = true;
  auto button = [](SDL_GamepadButton b) { return SDL_GetGamepadButton(gamepad, b); };
  s.a = button(SDL_GAMEPAD_BUTTON_SOUTH);
  s.b = button(SDL_GAMEPAD_BUTTON_EAST);
  s.x = button(SDL_GAMEPAD_BUTTON_WEST);
  s.y = button(SDL_GAMEPAD_BUTTON_NORTH);
  s.start = button(SDL_GAMEPAD_BUTTON_START);
  s.back = button(SDL_GAMEPAD_BUTTON_BACK);
  s.up = button(SDL_GAMEPAD_BUTTON_DPAD_UP);
  s.down = button(SDL_GAMEPAD_BUTTON_DPAD_DOWN);
  s.left = button(SDL_GAMEPAD_BUTTON_DPAD_LEFT);
  s.right = button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
  s.left_x = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
  s.left_y = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
  return s;
}

// ---- first start ------------------------------------------------------------------------------

int AskChoice(const std::string& title, const std::string& message,
              const std::vector<std::string>& buttons) {
  std::vector<SDL_MessageBoxButtonData> data;
  for (size_t i = 0; i < buttons.size(); ++i) {
    SDL_MessageBoxButtonData button{};
    button.buttonID = int(i);
    button.text = buttons[i].c_str();
    if (i == 0) button.flags |= SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    if (i + 1 == buttons.size()) button.flags |= SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    data.push_back(button);
  }
  // SDL's message boxes do not wrap (text_wrap.h).
  const std::string wrapped = WrapText(message);
  SDL_MessageBoxData box{};
  box.flags = SDL_MESSAGEBOX_INFORMATION | SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT;
  box.title = title.c_str();
  box.message = wrapped.c_str();
  box.numbuttons = int(data.size());
  box.buttons = data.data();
  int chosen = -1;
  if (!SDL_ShowMessageBox(&box, &chosen)) return -1;
  return chosen;
}

void ShowError(const std::string& title, const std::string& message) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), WrapText(message).c_str(), nullptr);
}

namespace {

// SDL's pickers answer through a callback, possibly on another thread; events are pumped on this
// one until it comes.
struct PickState {
  std::mutex mutex;
  std::atomic<bool> done{false};
  PickResult result = PickResult::kFailed;
  std::string path, error;
};

void PickCallback(void* userdata, const char* const* filelist, int) {
  auto* state = static_cast<PickState*>(userdata);
  {
    std::lock_guard lock(state->mutex);
    if (!filelist) {
      state->result = PickResult::kFailed;
      state->error = SDL_GetError();
    } else if (!filelist[0]) {
      state->result = PickResult::kCancelled;
    } else {
      state->result = PickResult::kChosen;
      state->path = filelist[0];
    }
  }
  state->done = true;
}

PickResult Pick(SDL_FileDialogType type, const std::string& title, std::string& path,
                std::string& error) {
  PickState state;
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
  SDL_ShowFileDialogWithProperties(type, PickCallback, &state, props);
  while (!state.done) {
    SDL_PumpEvents();
    SDL_Delay(10);
  }
  SDL_DestroyProperties(props);
  std::lock_guard lock(state.mutex);
  path = state.path;
  error = state.error;
  return state.result;
}

// UTF-8 text for SDL's debug font, which has ASCII only: Latin letters with accents lose them,
// anything else becomes '?'.
std::string Ascii(const std::string& utf8) {
  static const std::pair<const char*, const char*> kFold[] = {
      {"á", "a"}, {"à", "a"}, {"â", "a"}, {"ä", "a"}, {"é", "e"}, {"è", "e"}, {"ê", "e"},
      {"ë", "e"}, {"í", "i"}, {"î", "i"}, {"ï", "i"}, {"ó", "o"}, {"ô", "o"}, {"ö", "o"},
      {"ú", "u"}, {"ù", "u"}, {"û", "u"}, {"ü", "u"}, {"ñ", "n"}, {"ç", "c"}, {"ß", "ss"},
      {"Á", "A"}, {"É", "E"}, {"Í", "I"}, {"Ó", "O"}, {"Ú", "U"}, {"Ä", "A"}, {"Ö", "O"},
      {"Ü", "U"}, {"Ñ", "N"}, {"Ç", "C"}, {"À", "A"}, {"È", "E"}, {"«", "\""}, {"»", "\""},
      {"…", "..."}, {"¡", "!"}, {"¿", "?"}};
  std::string out;
  for (size_t i = 0; i < utf8.size();) {
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    if (c < 0x80) {
      out += char(c);
      ++i;
      continue;
    }
    const size_t length = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    const std::string_view letter(utf8.data() + i, std::min(length, utf8.size() - i));
    const char* folded = "?";
    for (const auto& [from, to] : kFold) {
      if (letter == from) folded = to;
    }
    out += folded;
    i += length;
  }
  return out;
}

class SdlProgressWindow : public ProgressWindow {
 public:
  SdlProgressWindow(SDL_Window* window, SDL_Renderer* renderer)
      : window_(window), renderer_(renderer) {}
  ~SdlProgressWindow() override {
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
  }
  bool Show(double fraction, const std::string& text) override {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        closed_ = true;
      }
    }
    int width = 0, height = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &width, &height);
    SDL_SetRenderDrawColor(renderer_, 24, 20, 16, 255);
    SDL_RenderClear(renderer_);
    const float margin = 20, bar_top = float(height) / 2, bar_height = 24;
    const SDL_FRect frame{margin, bar_top, float(width) - 2 * margin, bar_height};
    const SDL_FRect fill{margin + 2, bar_top + 2,
                         float(std::clamp(fraction, 0.0, 1.0)) * (frame.w - 4), bar_height - 4};
    SDL_SetRenderDrawColor(renderer_, 220, 180, 60, 255);
    SDL_RenderRect(renderer_, &frame);
    SDL_RenderFillRect(renderer_, &fill);
    SDL_SetRenderDrawColor(renderer_, 230, 225, 210, 255);
    SDL_RenderDebugText(renderer_, margin, bar_top - 24, Ascii(text).c_str());
    SDL_RenderPresent(renderer_);
    return !closed_;
  }

 private:
  SDL_Window* window_;
  SDL_Renderer* renderer_;
  bool closed_ = false;
};

}  // namespace

PickResult PickFile(const std::string& title, std::string& path, std::string& error) {
  return Pick(SDL_FILEDIALOG_OPENFILE, title, path, error);
}

PickResult PickFolder(const std::string& title, std::string& path, std::string& error) {
  return Pick(SDL_FILEDIALOG_OPENFOLDER, title, path, error);
}

std::vector<std::string> PreferredLanguages() {
  std::vector<std::string> languages;
  int count = 0;
  if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
    for (int i = 0; i < count; ++i) {
      if (!locales[i] || !locales[i]->language) continue;
      std::string code = locales[i]->language;
      if (locales[i]->country) code += std::string("-") + locales[i]->country;
      for (char& c : code) c = char(std::tolower(static_cast<unsigned char>(c)));
      languages.push_back(code);
      // The language alone too ("pt-br" also matches "pt").
      if (const size_t dash = code.find('-'); dash != std::string::npos) {
        languages.push_back(code.substr(0, dash));
      }
    }
    SDL_free(locales);
  }
  return languages;
}

std::unique_ptr<ProgressWindow> ProgressWindow::Open(const std::string& title) {
  SDL_Window* window = SDL_CreateWindow(title.c_str(), 560, 120, 0);
  if (!window) return nullptr;
  SDL_Renderer* renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
  if (!renderer) {
    SDL_DestroyWindow(window);
    return nullptr;
  }
  return std::make_unique<SdlProgressWindow>(window, renderer);
}

namespace {

class SdlLauncherWindow : public LauncherWindow {
 public:
  SdlLauncherWindow(SDL_Window* window, SDL_Renderer* renderer, SDL_InitFlags subsystems)
      : window_(window), renderer_(renderer), subsystems_(subsystems) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
    ApplyScale();
  }
  ~SdlLauncherWindow() override {
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer_);
    SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(subsystems_);
  }

  bool NewFrame() override {
    bool close = false;
    SDL_Event event;
    for (bool have = SDL_WaitEventTimeout(&event, kFrameMs); have; have = SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT ||
          (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
           event.window.windowID == SDL_GetWindowID(window_))) {
        close = true;
      } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        ApplyScale();
      }
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    return close;
  }

  void Present() override {
    ImGui::Render();
    const ImGuiIO& io = ImGui::GetIO();
    SDL_SetRenderScale(renderer_, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer_, 24, 20, 16, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
  }

  float scale() const override { return scale_; }

 private:
  static constexpr Sint32 kFrameMs = 16;

  // The style is rebuilt from the default at each scale, so that sizes are not scaled twice.
  void ApplyScale() {
    const float scale = SDL_GetWindowDisplayScale(window_);
    scale_ = scale > 0 ? scale : 1;
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);
    style.ScaleAllSizes(scale_);
    style.FontScaleDpi = scale_;
    ImGui::GetStyle() = style;
  }

  SDL_Window* window_;
  SDL_Renderer* renderer_;
  SDL_InitFlags subsystems_;
  float scale_ = 1;
};

}  // namespace

std::unique_ptr<LauncherWindow> LauncherWindow::Open(const std::string& title,
                                                     std::string& error) {
  if (ImGui::GetCurrentContext()) {
    error = "another ImGui context is current";
    return nullptr;
  }
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    error = std::string("SDL video: ") + SDL_GetError();
    return nullptr;
  }
  // Without gamepads the launcher still works with the keyboard and the mouse.
  SDL_InitFlags subsystems = SDL_INIT_VIDEO;
  if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) subsystems |= SDL_INIT_GAMEPAD;

  // 1280x720 at 100%, grown with the display's scale and kept inside its usable area.
  int width = 1280, height = 720;
  const SDL_DisplayID display = SDL_GetPrimaryDisplay();
  if (const float scale = SDL_GetDisplayContentScale(display); scale > 0) {
    width = int(float(width) * scale);
    height = int(float(height) * scale);
  }
  if (SDL_Rect usable; SDL_GetDisplayUsableBounds(display, &usable)) {
    const float fit = std::min({1.0f, 0.9f * float(usable.w) / float(width),
                                0.9f * float(usable.h) / float(height)});
    width = int(float(width) * fit);
    height = int(float(height) * fit);
  }
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (PreferFullscreenLauncher()) flags |= SDL_WINDOW_FULLSCREEN;
  SDL_Window* window = SDL_CreateWindow(title.c_str(), width, height, flags);
  if (!window) {
    error = std::string("window: ") + SDL_GetError();
    SDL_QuitSubSystem(subsystems);
    return nullptr;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, SDL_SOFTWARE_RENDERER);
  if (!renderer) {
    error = std::string("software renderer: ") + SDL_GetError();
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(subsystems);
    return nullptr;
  }
  return std::make_unique<SdlLauncherWindow>(window, renderer, subsystems);
}

}  // namespace torchlight::platform
