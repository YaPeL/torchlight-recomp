// The parts every page of the wizard has, and its way between pages (Back, B, Escape).

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "wizard/wizard.h"

namespace torchlight::wizard {

inline constexpr float kTextSize = 19.0f;  // points: times the display's scale in pixels

// The words of the parts below, English until the wizard follows the setup's language.
struct UiTexts {
  std::string back = "Back";
  std::string hint = "A: choose     B: back     D-pad or arrows: move";
  std::string hint_next = "A: choose     B: back     Start: next     D-pad or arrows: move";
};

class Ui {
 public:
  void Attach(const Fonts& fonts, std::string title) {
    fonts_ = fonts;
    title_ = std::move(title);
  }
  UiTexts texts;

  int page() const { return page_; }
  void Go(int page);
  void Back();
  // To `page` with no way back from it.
  void Reset(int page);
  bool can_go_back() const { return !history_.empty(); }
  // The page's first frames: its default item takes the focus then.
  bool entered() const { return entered_; }
  // Draws the frame's full-window page. `can_go_back` false: B and Escape do nothing.
  void Frame(bool can_go_back, const std::function<void()>& draw);

  void Heading(const std::string& title, const std::string& step);
  // The height left for a page's body, less the footer's.
  float BodyHeight() const;
  // Back on the left, the next step on the right when it has a label; true when it was chosen.
  bool Footer(const char* next_label, bool next_enabled, bool back_enabled = true,
              bool focus_next = false);
  void Wrapped(const std::string& text);
  void Muted(const std::string& text);
  void Error(const std::string& text);
  // A box in the warning's colour, its height its text's.
  void Warning(const char* id, const std::string& title, const std::string& text);

  // The page's default item: call FocusNext before it and Took after it. The focus is given after
  // the item (ImGui's SetFocusID), with the pad's cursor shown, since ImGui ignores a first A while
  // it is hidden; SetKeyboardFocusHere is not resolved inside a NavFlattened child.
  void FocusNext() { want_focus_ = true; }
  void Took();

 private:
  Fonts fonts_;
  std::string title_;
  int page_ = 0;
  std::vector<int> history_;
  bool entered_ = true;
  int entered_frames_ = 0;
  bool want_focus_ = false;
};

}  // namespace torchlight::wizard
