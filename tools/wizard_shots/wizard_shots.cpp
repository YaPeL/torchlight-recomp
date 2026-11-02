// The wizard's parts on sample pages, played by a script, for the screenshots the tests and CI keep:
// wizard_shots --script FILE --shots DIR --browse_from FOLDER [--video DRIVER] [--width W --height H]
// [--scale S]. Nothing of the game: the sample pages' texts are not what a player reads, and the
// first start's real pages run over invented files.

#include <cstdio>
#include <cstdlib>
#include <SDL3/SDL.h>

#include <filesystem>
#include <functional>
#include <fstream>
#include <memory>
#include <vector>
#include <optional>
#include <string>

#include "game_menu/menu_strings.h"
#include "game_setup/game_files.h"
#include "game_setup/first_run_steps.h"
#include "game_setup/setup_pages.h"
#include "platform/storage_mode.h"
#include "wizard/browser_page.h"
#include "wizard/wizard.h"
#include "wizard/wizard_ui.h"

namespace fs = std::filesystem;
using namespace torchlight::wizard;

namespace {

fs::path Path(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

class SamplePages : public Pages {
 public:
  explicit SamplePages(const fs::path& browse_from) : browse_from_(browse_from) {}

  void Attach(const Fonts& fonts, SDL_Window*) override { ui_.Attach(fonts, "Torchlight Recomp"); }
  std::string Name() const override {
    static const char* const names[] = {"welcome", "browser", "chosen"};
    return names[ui_.page()];
  }
  std::string Detail() const override {
    if (ui_.page() == kChosen) return chosen_;
    const std::u8string folder = browser_.folder().generic_u8string();
    return std::string(folder.begin(), folder.end());
  }
  bool Frame() override {
    ui_.Frame(ui_.page() != kWelcome, [&] {
      switch (ui_.page()) {
        case kWelcome: Welcome(); break;
        case kBrowser: Browser(); break;
        case kChosen: Chosen(); break;
      }
    });
    return done_;
  }

 private:
  enum { kWelcome, kBrowser, kChosen };

  void Welcome() {
    ui_.Heading("Sample page", "Step 1 of 2");
    ImGui::BeginChild("body", ImVec2(0, ui_.BodyHeight()), ImGuiChildFlags_NavFlattened);
    ui_.Wrapped("The wizard's parts on a sample page: the heading, the text, a warning and the "
                "footer. Accents and other letters: ñandú, Größe, français, ¿qué?");
    ImGui::Spacing();
    ui_.Warning("warning", "A warning", "Its box takes the height of its text.");
    ImGui::Spacing();
    ui_.Muted("A muted line.");
    ui_.Error("An error line.");
    ImGui::EndChild();
    if (ui_.Footer("Next", true, false, true)) {
      browser_.Open(browse_from_);
      ui_.Go(kBrowser);
    }
  }
  void Browser() {
    if (auto chosen = browser_.Draw(ui_, "Choose a folder", "Step 2 of 2")) {
      const std::u8string text = chosen->generic_u8string();
      chosen_ = std::string(text.begin(), text.end());
      ui_.Go(kChosen);
    }
  }
  void Chosen() {
    ui_.Heading("The folder chosen", "Done");
    ImGui::BeginChild("body", ImVec2(0, ui_.BodyHeight()), ImGuiChildFlags_NavFlattened);
    ui_.Wrapped(chosen_);
    ImGui::EndChild();
    if (ui_.Footer("Close", true, true, true)) done_ = true;
  }

  Ui ui_;
  BrowserPage browser_{true};
  fs::path browse_from_;
  std::string chosen_;
  bool done_ = false;
};

// The first start's real pages, over invented game files with a table made here: --pages setup
// --sample_root DIR [--language CODE --strings FILE] [--no_game] [--install_delay MS]
// [--expect installed|none]. The exit code also says whether the install ended as expected.
struct SetupRun {
  fs::path sample_root;
  std::string language, strings;
  bool ask_game = true;
  int install_delay = 0;
  std::string expect;
  std::string storage;     // ask, parent_unwritable, portable_unwritable
  bool user_data = false;  // invented user folders holding files to copy
};

int RunSetup(const Options& options, const SetupRun& run) {
  namespace gs = torchlight::game_setup;
  const fs::path source = run.sample_root / "source", game = run.sample_root / "game";
  fs::remove_all(run.sample_root);
  fs::create_directories(source);
  std::ofstream(source / "default.xex") << "an invented executable";
  std::ofstream(source / "data.bin") << std::string(4096, 'x');
  std::ofstream(run.sample_root / "not-a-package.bin") << "not a package";
  const std::string xex_hash = gs::Sha256File(source / "default.xex");
  const std::string data_hash = gs::Sha256File(source / "data.bin");
  const std::vector<gs::GameFile> files = {
      {"default.xex", fs::file_size(source / "default.xex"), xex_hash},
      {"data.bin", fs::file_size(source / "data.bin"), data_hash}};

  gs::SetupConfig config;
  const fs::path portable = run.sample_root / "app" / torchlight::platform::kPortableFolderName;
  const fs::path user_game = run.sample_root / "user-folders" / "game";
  config.questions = {game, run.ask_game, true};
  config.files = files;
  config.own_browser_only = true;
  config.browse_from = run.sample_root;
  config.test_install_delay_ms = run.install_delay;
  if (!run.storage.empty()) {
    fs::create_directories(portable.parent_path());
    // Another portable folder, and files in the tests' user folders, to copy from.
    const fs::path other = run.sample_root / "other-portable";
    fs::create_directories(other / "config");
    std::ofstream(other / "config" / "settings.toml") << "achievements = \"pc\"\n";
    fs::create_directories(other / "data");
    std::ofstream(other / "data" / "a.sav") << "a save";
    gs::StorageQuestion question;
    question.portable_folder = portable;
    question.mode = run.storage == "portable_unwritable"
                        ? torchlight::platform::StorageMode::kPortableUnwritable
                        : torchlight::platform::StorageMode::kAsk;
    question.parent_writable = run.storage == "ask";
    if (run.user_data) {
      const fs::path user = run.sample_root / "user-folders";
      fs::create_directories(user / "data");
      std::ofstream(user / "data" / "from-user.sav") << "a save";
      question.user_sources = torchlight::platform::UserFolderSources(
          user / "config", user / "data", user / "cache", user / "data" / "game", user / "logs");
    }
    question.user_data = run.user_data;
    config.storage = question;
    config.after_storage = [=](bool portable_chosen) {
      return gs::OpenQuestions{portable_chosen ? portable / "game" : user_game, true, true};
    };
  }
  auto strings = std::make_shared<torchlight::game_menu::MenuStrings>();
  if (!run.language.empty()) {
    std::vector<std::string> warnings;
    std::string error;
    if (!strings->Load(run.strings, warnings, error)) {
      std::fprintf(stderr, "wizard_shots: %s\n", error.c_str());
      return 1;
    }
    config.translate = [strings, language = run.language](const std::string& english) {
      return strings->Translate(language, english);
    };
  }
  gs::SetupPages pages(std::move(config));
  if (const int status = Run(options, pages); status != 0) return status;
  if (run.expect == "copied-other" && !fs::exists(portable / "data" / "a.sav")) {
    std::fprintf(stderr, "wizard_shots: the other portable folder was not copied\n");
    return 4;
  }
  if (run.expect == "copied-user" && !fs::exists(portable / "data" / "from-user.sav")) {
    std::fprintf(stderr, "wizard_shots: the user's folders were not copied\n");
    return 4;
  }
  if (run.expect == "no-portable" && fs::exists(portable)) {
    std::fprintf(stderr, "wizard_shots: a cancelled install left the portable folder\n");
    return 4;
  }
  if (run.expect == "user-folders" && pages.portable() != false) {
    std::fprintf(stderr, "wizard_shots: the user's folders were not chosen\n");
    return 4;
  }
  if (run.expect == "installed" && (!pages.completed() || !gs::XexMatches(game, files))) {
    std::fprintf(stderr, "wizard_shots: the game was not installed\n");
    return 4;
  }
  if (run.expect == "none" && fs::exists(game)) {
    std::fprintf(stderr, "wizard_shots: a game folder was left behind\n");
    return 4;
  }
  return 0;
}

}  // namespace

namespace {

// As the game's start finds it, with the runtime's SDL up: the wizard must leave it so.
bool WatchCounts(void* count, SDL_Event*) {
  ++*static_cast<int*>(count);
  return true;
}

int RunWithSdlUp(const Options& options, const std::function<int()>& run) {
  if (!options.video.empty()) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, options.video.c_str());
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "wizard_shots: no SDL: %s\n", SDL_GetError());
    return 2;
  }
  int watched = 0;
  SDL_AddEventWatch(WatchCounts, &watched);
  SDL_SetHint(SDL_HINT_APP_NAME, "the runtime's name");
  const Uint32 user_event = SDL_RegisterEvents(1);
  SDL_Event before{};
  before.type = user_event;
  SDL_PushEvent(&before);
  const int status = run();
  if (status == 0) {
    if (!SDL_WasInit(SDL_INIT_VIDEO) || !SDL_GetCurrentVideoDriver()) {
      std::fprintf(stderr, "wizard_shots: the wizard took the video subsystem down\n");
      return 5;
    }
    const int seen = watched;
    SDL_Event after{};
    after.type = user_event + 0;
    SDL_PushEvent(&after);
    if (watched == seen) {
      std::fprintf(stderr, "wizard_shots: the wizard took the event watch away\n");
      return 5;
    }
    SDL_Event queued;
    int kept = 0;
    while (SDL_PollEvent(&queued)) kept += queued.type == user_event;
    if (kept != 2) {
      std::fprintf(stderr, "wizard_shots: the wizard ate the runtime's events (%d of 2)\n", kept);
      return 5;
    }
    if (std::string(SDL_GetHint(SDL_HINT_APP_NAME)) != "the runtime's name") {
      std::fprintf(stderr, "wizard_shots: the wizard lost a hint it found\n");
      return 5;
    }
    if (SDL_GetHint(SDL_HINT_RENDER_DRIVER) || SDL_GetHint(SDL_HINT_FILE_DIALOG_DRIVER)) {
      std::fprintf(stderr, "wizard_shots: the wizard left its hints set\n");
      return 5;
    }
  }
  SDL_RemoveEventWatch(WatchCounts, &watched);
  SDL_Quit();
  return status;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  options.width = 960;
  std::string browse_from, pages_kind;
  SetupRun setup;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--script") options.script = value;
    else if (key == "--shots") options.shots = value;
    else if (key == "--browse_from") browse_from = value;
    else if (key == "--pages") pages_kind = value;
    else if (key == "--sample_root") setup.sample_root = Path(value);
    else if (key == "--language") setup.language = value;
    else if (key == "--strings") setup.strings = value;
    else if (key == "--no_game") setup.ask_game = value != "1";
    else if (key == "--install_delay") setup.install_delay = std::atoi(value.c_str());
    else if (key == "--expect") setup.expect = value;
    else if (key == "--storage") setup.storage = value;
    else if (key == "--user_data") setup.user_data = value == "1";
    else if (key == "--video") options.video = value;
    else if (key == "--width") options.width = std::atoi(value.c_str());
    else if (key == "--height") options.height = std::atoi(value.c_str());
    else if (key == "--scale") options.scale = float(std::atof(value.c_str()));
    else {
      std::fprintf(stderr, "wizard_shots: unknown option %s\n", key.c_str());
      return 1;
    }
  }
  if (pages_kind == "setup") {
    if (options.script.empty() || setup.sample_root.empty()) {
      std::fprintf(stderr, "usage: wizard_shots --pages setup --script FILE --sample_root DIR\n");
      return 1;
    }
    return RunWithSdlUp(options, [&] { return RunSetup(options, setup); });
  }
  // Both required: it never lists a real folder or waits for a player by default.
  if (options.script.empty() || browse_from.empty()) {
    std::fprintf(stderr, "usage: wizard_shots --script FILE --shots DIR --browse_from FOLDER\n");
    return 1;
  }
  SamplePages pages(Path(browse_from));
  return RunWithSdlUp(options, [&] { return Run(options, pages); });
}
