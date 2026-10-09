#include "launcher/launcher_view.h"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "game_setup/game_files.h"
#include "launcher/launcher_text.h"

namespace torchlight::launcher {

namespace {

namespace fs = std::filesystem;
using game_setup::Translate;

// Our own palette, nothing of the game's: black and phosphor green.
constexpr ImVec4 Rgb(int r, int g, int b, float a = 1) {
  return ImVec4(float(r) / 255, float(g) / 255, float(b) / 255, a);
}
constexpr ImVec4 kBackground = Rgb(0, 0, 0);
constexpr ImVec4 kPanel = Rgb(4, 14, 6);
constexpr ImVec4 kText = Rgb(200, 255, 210);
constexpr ImVec4 kTextDim = Rgb(0, 160, 40);
constexpr ImVec4 kAccent = Rgb(0, 255, 65);
constexpr ImVec4 kAccentHover = Rgb(110, 255, 140);
constexpr ImVec4 kAccentActive = Rgb(0, 200, 50);
constexpr ImVec4 kButton = Rgb(0, 40, 12);
constexpr ImVec4 kButtonHover = Rgb(0, 70, 22);
constexpr ImVec4 kButtonActive = Rgb(0, 100, 32);
constexpr ImVec4 kBorder = Rgb(0, 95, 28);
constexpr ImVec4 kError = Rgb(255, 96, 80);

// Sizes at 100% (times the display's scale).
constexpr float kColumnWidth = 720;
constexpr float kButtonHeight = 44;
constexpr float kRowHeight = 30;

// The launcher's colors and spacing for one frame.
class StyleScope {
 public:
  explicit StyleScope(float s) {
    const std::pair<ImGuiCol, ImVec4> colors[] = {
        {ImGuiCol_WindowBg, kBackground},     {ImGuiCol_ChildBg, kPanel},
        {ImGuiCol_Text, kText},               {ImGuiCol_TextDisabled, kTextDim},
        {ImGuiCol_Button, kButton},           {ImGuiCol_ButtonHovered, kButtonHover},
        {ImGuiCol_ButtonActive, kButtonActive}, {ImGuiCol_Header, kButtonActive},
        {ImGuiCol_HeaderHovered, kButtonHover}, {ImGuiCol_HeaderActive, kButtonActive},
        {ImGuiCol_FrameBg, kButton},          {ImGuiCol_PlotHistogram, kAccent},
        {ImGuiCol_Border, kBorder},           {ImGuiCol_Separator, kBorder},
        {ImGuiCol_NavCursor, kAccent},        {ImGuiCol_ScrollbarBg, kPanel},
    };
    for (const auto& [index, color] : colors) ImGui::PushStyleColor(index, color);
    color_count_ = int(std::size(colors));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32 * s, 24 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14 * s, 8 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10 * s, 10 * s));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6 * s);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    var_count_ = 8;
  }
  ~StyleScope() {
    ImGui::PopStyleVar(var_count_);
    ImGui::PopStyleColor(color_count_);
  }

 private:
  int color_count_ = 0, var_count_ = 0;
};

std::string Tr(const Translate& tr, std::string_view english,
               std::vector<std::pair<std::string, std::string>> values = {}) {
  return game_setup::Render({std::string(english), std::move(values)}, tr);
}

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

std::string_view Label(Button button) {
  switch (button) {
    case Button::kChoosePackage: return game_setup::kTextChoosePackage;
    case Button::kChooseFolder: return game_setup::kTextChooseFolder;
    case Button::kQuit: return game_setup::kTextQuit;
    case Button::kCancel: return kTextCancel;
    case Button::kBack: return kTextBack;
    case Button::kAchievementsXbox: return game_setup::kTextAchievementsXbox;
    case Button::kAchievementsPc: return game_setup::kTextAchievementsPc;
    case Button::kPlay: return kTextPlay;
    case Button::kReinstall: return kTextReinstall;
    case Button::kChangeAchievements: return kTextChangeAchievements;
  }
  return {};
}

void Heading(std::string_view text, float size) {
  ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * size);
  ImGui::TextUnformatted(text.data(), text.data() + text.size());
  ImGui::PopFont();
}

void Wrapped(const std::string& text, const ImVec4& color) {
  ImGui::PushStyleColor(ImGuiCol_Text, color);
  ImGui::TextWrapped("%s", text.c_str());
  ImGui::PopStyleColor();
}

// A page's buttons, full width, one under the other. When the page offers a choice the first one
// is the primary (the accent); Back and Quit stand apart at the end. Returns the one pressed.
std::optional<Button> DrawButtons(const std::vector<Button>& buttons, const Translate& tr,
                                  bool focus_first, float s) {
  std::optional<Button> pressed;
  for (size_t i = 0; i < buttons.size(); ++i) {
    const Button button = buttons[i];
    const bool primary = i == 0 && buttons.size() > 1;
    if (i > 0 && (button == Button::kBack || button == Button::kQuit)) {
      ImGui::Dummy(ImVec2(0, 8 * s));
    }
    if (primary) {
      ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
      ImGui::PushStyleColor(ImGuiCol_Text, kBackground);
    }
    if (i == 0 && focus_first) ImGui::SetKeyboardFocusHere();
    const std::string label = Tr(tr, Label(button)) + "##" + std::to_string(i);
    if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, kButtonHeight * s))) pressed = button;
    if (primary) ImGui::PopStyleColor(4);
  }
  return pressed;
}

bool BackPressed() {
  return ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
         ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
}

bool Has(const std::vector<Button>& buttons, Button button) {
  return std::find(buttons.begin(), buttons.end(), button) != buttons.end();
}

}  // namespace

std::string FormatSize(uint64_t bytes) {
  if (bytes < 1024) return std::to_string(bytes) + " B";
  static constexpr const char* kUnits[] = {"KB", "MB", "GB", "TB"};
  double value = double(bytes) / 1024;
  size_t unit = 0;
  while (value >= 1024 && unit + 1 < std::size(kUnits)) {
    value /= 1024;
    ++unit;
  }
  char text[32];
  std::snprintf(text, sizeof(text), "%.1f %s", value, kUnits[unit]);
  return text;
}

ViewResult LauncherView::Draw(const LauncherModel& model, FileBrowserModel* browser,
                              const Translate& tr, const std::string& game_dir, float scale) {
  ViewResult result;
  const float s = scale > 0 ? scale : 1;
  const Page page = model.page();
  const bool page_opened = page_ != page;
  page_ = page;
  ImGui::GetIO().ConfigNavCursorVisibleAlways = true;  // the pad's focus is always shown

  StyleScope style(s);
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::Begin("##launcher", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

  // The column, centered, under the project's name and a rule.
  const float available = ImGui::GetContentRegionAvail().x;
  const float width = std::min(available, kColumnWidth * s);
  const float left = ImGui::GetCursorPosX() + (available - width) / 2;
  ImGui::Dummy(ImVec2(0, 8 * s));
  ImGui::SetCursorPosX(left);
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  Heading(kProjectName, 2.2f);
  ImGui::PopStyleColor();
  {
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float x = ImGui::GetWindowPos().x + left;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(x, at.y), ImVec2(x + width, at.y),
                                        ImGui::GetColorU32(kBorder), 1.5f * s);
    ImGui::Dummy(ImVec2(0, 14 * s));
  }
  ImGui::SetCursorPosX(left);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
  ImGui::BeginChild("##column", ImVec2(width, 0), ImGuiChildFlags_NavFlattened);
  ImGui::PopStyleColor();

  const std::vector<Button> buttons = model.Buttons();
  const bool back = !page_opened && BackPressed();
  switch (page) {
    case Page::kInstall:
      Heading(Tr(tr, kTextInstallHeading), 1.4f);
      ImGui::TextWrapped("%s", Tr(tr, game_setup::kTextIntro, {{"where", game_dir}}).c_str());
      if (!model.error().empty()) {
        ImGui::Dummy(ImVec2(0, 4 * s));
        Wrapped(game_setup::Render(model.error(), tr), kError);
      }
      ImGui::Dummy(ImVec2(0, 12 * s));
      result.button = DrawButtons(buttons, tr, page_opened, s);
      break;

    case Page::kBrowse: {
      if (!browser) break;
      if (back && !browser->Up()) result.button = Button::kBack;
      const bool folder_opened = page_opened || browser->folder() != folder_;
      folder_ = browser->folder();
      Heading(Tr(tr, model.browse_source() == game_setup::Source::kPackage
                         ? game_setup::kTextPackagePicker
                         : game_setup::kTextFolderPicker),
              1.4f);
      if (!model.note().empty()) Wrapped(game_setup::Render(model.note(), tr), kTextDim);
      // The places, in a row that wraps.
      const float right = ImGui::GetContentRegionAvail().x;
      float used = 0;
      for (size_t i = 0; i < browser->places().size(); ++i) {
        const std::string label = Utf8(browser->places()[i]) + "##place" + std::to_string(i);
        const float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x +
                        2 * ImGui::GetStyle().FramePadding.x;
        if (i > 0 && used + ImGui::GetStyle().ItemSpacing.x + w <= right) {
          ImGui::SameLine();
          used += ImGui::GetStyle().ItemSpacing.x;
        } else {
          used = 0;
        }
        used += w;
        if (ImGui::Button(label.c_str())) browser->GoTo(i);
      }
      ImGui::TextDisabled("%s", Utf8(browser->folder()).c_str());
      // The rows, in a box that leaves room for the error and Back.
      const float line = ImGui::GetTextLineHeightWithSpacing();
      float footer = kButtonHeight * s + ImGui::GetStyle().ItemSpacing.y * 2;
      if (!browser->error().empty()) footer += line * 2;
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * s, 8 * s));
      ImGui::BeginChild("##rows", ImVec2(0, std::max(line * 4, ImGui::GetContentRegionAvail().y - footer)),
                        ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
      ImGui::PopStyleVar();
      const std::vector<FileBrowserModel::Row> rows = browser->rows();  // activating replaces them
      bool any_entry = false;
      for (size_t i = 0; i < rows.size(); ++i) {
        const FileBrowserModel::Row& row = rows[i];
        std::string label;
        switch (row.kind) {
          case FileBrowserModel::RowKind::kParent: label = ".. " + Tr(tr, kTextParentFolder); break;
          case FileBrowserModel::RowKind::kUseFolder: label = "> " + Tr(tr, kTextUseFolder); break;
          case FileBrowserModel::RowKind::kFolder: label = row.name + "/"; break;
          case FileBrowserModel::RowKind::kFile: label = row.name; break;
        }
        any_entry |= row.kind == FileBrowserModel::RowKind::kFolder ||
                     row.kind == FileBrowserModel::RowKind::kFile;
        label += "##row" + std::to_string(i);
        if (folder_opened && i == browser->selected()) ImGui::SetKeyboardFocusHere();
        const float row_right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        const float row_top = ImGui::GetCursorPosY();
        const bool activated = ImGui::Selectable(label.c_str(), i == browser->selected(), 0,
                                                 ImVec2(0, kRowHeight * s));
        if (ImGui::IsItemFocused() && i != browser->selected()) browser->Select(i);
        if (row.kind == FileBrowserModel::RowKind::kFile) {
          const std::string size = FormatSize(row.size);
          ImGui::SameLine(row_right - ImGui::CalcTextSize(size.c_str()).x);
          ImGui::SetCursorPosY(row_top + (kRowHeight * s - ImGui::GetTextLineHeight()) / 2);
          ImGui::TextDisabled("%s", size.c_str());
        }
        if (activated) {
          result.chosen = browser->Activate(i);
          break;
        }
      }
      if (!any_entry && !browser->folder().empty()) ImGui::TextDisabled("%s", Tr(tr, kTextEmptyFolder).c_str());
      ImGui::EndChild();
      if (!browser->error().empty()) Wrapped(game_setup::Render(browser->error(), tr), kError);
      if (auto pressed = DrawButtons(buttons, tr, false, s)) result.button = pressed;
      break;
    }

    case Page::kInstalling: {
      Heading(Tr(tr, kTextInstallingHeading), 1.4f);
      const std::string_view phase =
          model.phase() == game_setup::Phase::kChecking ? game_setup::kTextChecking
          : model.install_source() == game_setup::Source::kPackage ? game_setup::kTextExtracting
                                                                   : game_setup::kTextCopying;
      ImGui::TextUnformatted(Tr(tr, phase).c_str());
      char percent[16];
      std::snprintf(percent, sizeof(percent), "%d%%", int(model.fraction() * 100));
      ImGui::ProgressBar(float(model.fraction()), ImVec2(-FLT_MIN, 28 * s), percent);
      ImGui::Dummy(ImVec2(0, 12 * s));
      if (model.cancelling()) {
        ImGui::TextDisabled("%s", Tr(tr, kTextCancelling).c_str());
      } else {
        result.button = DrawButtons(buttons, tr, page_opened, s);
      }
      break;
    }

    case Page::kAchievements:
      Heading(Tr(tr, kTextAchievementsHeading), 1.4f);
      ImGui::TextWrapped("%s", Tr(tr, game_setup::kTextAchievementsChoice).c_str());
      ImGui::Dummy(ImVec2(0, 12 * s));
      result.button = DrawButtons(buttons, tr, page_opened, s);
      break;

    case Page::kReady: {
      Heading(Tr(tr, kTextReadyHeading), 1.4f);
      ImGui::TextWrapped("%s", Tr(tr, kTextReady, {{"where", game_dir}}).c_str());
      if (model.achievements()) {
        const std::string_view set = *model.achievements() == settings::AchievementSet::kPc
                                         ? game_setup::kTextAchievementsPc
                                         : game_setup::kTextAchievementsXbox;
        ImGui::TextDisabled("%s", Tr(tr, kTextAchievementSet, {{"set", Tr(tr, set)}}).c_str());
      }
      ImGui::Dummy(ImVec2(0, 12 * s));
      result.button = DrawButtons(buttons, tr, page_opened, s);
      break;
    }
  }
  if (back && page != Page::kBrowse && !result.button && Has(buttons, Button::kBack)) {
    result.button = Button::kBack;
  }

  ImGui::EndChild();
  ImGui::End();
  return result;
}

}  // namespace torchlight::launcher
