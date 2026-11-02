#include "wizard/browser_page.h"

namespace torchlight::wizard {

std::optional<std::filesystem::path> BrowserPage::Draw(Ui& ui, const std::string& title,
                                                       const std::string& step) {
  std::optional<std::filesystem::path> chosen;
  ui.Heading(title, step);
  if (browser_.at_roots()) {
    ui.Muted(texts.this_computer);
  } else {
    const std::u8string where = browser_.folder().u8string();
    ui.Muted(std::string(where.begin(), where.end()));
  }
  ImGui::BeginChild("body", ImVec2(0, ui.BodyHeight() - ImGui::GetFrameHeightWithSpacing()),
                    ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
  // What to do once the list is drawn: it changes under the loop otherwise.
  std::optional<BrowserEntry> pressed;
  bool up = false;
  const bool focus_first = refocus_frames_ > 0 || ui.entered();  // also coming Back to the page
  if (focus_first) --refocus_frames_;
  if (!browser_.at_roots()) {
    if (focus_first) ui.FocusNext();
    up = ImGui::Selectable(texts.above.c_str());
    ui.Took();
  }
  int index = 0;
  for (const BrowserEntry& entry : browser_.entries()) {
    ImGui::PushID(index);
    if (browser_.at_roots() && focus_first && index == 0) ui.FocusNext();
    const std::string line = (entry.folder ? texts.folder_mark : texts.file_mark) + entry.name;
    if (ImGui::Selectable(line.c_str())) pressed = entry;
    ui.Took();
    ImGui::PopID();
    ++index;
  }
  if (!browser_.error().empty()) ui.Error(browser_.error());
  ImGui::EndChild();
  if (!browser_.at_roots() && ImGui::Button(texts.use_folder.c_str())) chosen = browser_.folder();
  ui.Footer(nullptr, false);
  if (up) {
    browser_.Up();
    refocus_frames_ = kRefocusFrames;
  } else if (pressed && pressed->folder) {
    Open(pressed->path);
  } else if (pressed) {
    chosen = pressed->path;
  }
  return chosen;
}

}  // namespace torchlight::wizard
