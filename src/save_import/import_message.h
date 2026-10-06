// The texts of the import message boxes, from an ImportPlan: what is imported now, what at the
// next start, what cannot be, and why something confirmed before was not applied. Translated with
// `tr` (English text in, the game language's text out; data/ui/tl_import_strings.txt); long lists
// are summarized with counts (the detail is in import/import.log).

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "save_import/import_plan.h"

namespace torchlight::save_import {

using Translate = std::function<std::string(const std::string&)>;

struct ImportMessage {
  std::string title, text;
  std::vector<std::string> buttons;
  int active = 0;  // the button selected when the box opens
};

// The main box. Buttons: Yes / Not now / Do not ask again ("Not now" active); without anything
// that can be imported (no PC Pak.zip, an unreadable pak), only Not now / Do not ask again.
ImportMessage MainImportMessage(const ImportPlan& plan, const std::filesystem::path& import_folder,
                                const Translate& tr);

enum class MainAnswer { kYes, kNotNow, kDoNotAskAgain };
MainAnswer MainImportAnswer(const ImportPlan& plan, int button);

// Asked for each stash that would replace a recomp stash with items. Buttons: No (active) /
// Replace; button 1 means replace.
ImportMessage ReplaceStashMessage(const StashImport& stash, const Translate& tr);

// More lines than this in a list: a count instead.
inline constexpr size_t kMaxListedFiles = 6;

}  // namespace torchlight::save_import
