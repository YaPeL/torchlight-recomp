// The character creation screen's class and pet selectors with mods (docs/mods.md, section 7l): the
// parts that need no guest.
//
// The Xbox screen has three class buttons on the pad grid's first row. With more classes (the
// mods' list, mods/class_list.h) the three buttons become a window onto the list: left on the first
// button or right on the last scrolls the list by one, wrapping, and A picks the class the button
// shows. A class with no Xbox button image shows an empty frame with its name written on it. With
// the game's three classes only, nothing changes.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace torchlight::game_menu::class_selector {

inline constexpr size_t kSlots = 3;

class Carousel {
 public:
  explicit Carousel(size_t classes) : classes_(classes) {}

  // More classes than buttons: the selector is on.
  bool Active() const { return classes_ > kSlots; }
  // No classes: the selector was turned off for its screen (a button missing), and stays off.
  bool Disabled() const { return classes_ == 0; }
  // The list index the button `slot` shows (the slot itself when disabled).
  size_t ClassAt(size_t slot) const { return classes_ ? (offset_ + slot) % classes_ : slot; }
  // A move of `columns` from button `slot` scrolls the list (and the focus stays) when it would
  // leave the row: right from the last button, left from the first.
  bool Scrolls(size_t slot, int columns) const {
    return Active() && ((columns > 0 && slot == kSlots - 1) || (columns < 0 && slot == 0));
  }
  void Scroll(int by) {
    if (!classes_) return;
    const long n = long(classes_);
    offset_ = size_t(((long(offset_) + by) % n + n) % n);
  }
  void Reset() { offset_ = 0; }
  size_t offset() const { return offset_; }

 private:
  size_t classes_;
  size_t offset_ = 0;
};

// The window holding a class button's label: a child of the button, named after it.
std::string LabelName(std::string_view button);

// The creation layout with a hidden text label in each button whose onClick is GUISELECT1 or
// GUISELECT2 (any case: the classes and the pets), for a choice without a button image; nothing when the layout has no such button or already
// has the labels. Every original byte is kept.
std::optional<std::string> AddClassLabels(std::string_view xml);

}  // namespace torchlight::game_menu::class_selector
