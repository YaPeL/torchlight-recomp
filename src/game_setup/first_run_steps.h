// What first_run.cpp hands over to the way each platform asks: the wizard on desktop
// (first_run_wizard.cpp), the system's dialogs on Android (first_run_dialogs.cpp).

#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

#include "game_setup/first_run.h"
#include "game_setup/setup_text.h"
#include "platform/portable_copy.h"
#include "settings/host_settings.h"

namespace torchlight::game_setup {

// What is still open, which depends on where the files are kept.
struct OpenQuestions {
  std::filesystem::path game_dir;  // empty: --game_data_root gives it
  bool ask_game = false;
  bool ask_achievements = false;
};

// Where to keep the files, when that is the player's to answer (desktop only).
struct StorageQuestion {
  platform::StorageMode mode = platform::StorageMode::kAsk;
  std::filesystem::path portable_folder;
  bool parent_writable = false;  // the portable folder can be created where it goes
  bool user_data = false;        // the user's folders hold files that could be copied
  std::vector<platform::CopySource> user_sources;  // those folders
};

struct FirstStartAnswers {
  std::optional<settings::AchievementSet> achievement;
  std::optional<bool> portable;  // the storage answer: true portable, false the user's folders
};

// `questions` holds what is open with the storage as it is now; `after_storage` applies an answer and
// says what is open then. `answers` gets what was answered.
GameData AskFirstStart(const OpenQuestions& questions,
                       const std::optional<StorageQuestion>& storage,
                       const std::function<OpenQuestions(bool portable)>& after_storage,
                       const Translate& translate, FirstStartAnswers& answers,
                       std::vector<platform::StartupMessage>& log);

}  // namespace torchlight::game_setup
