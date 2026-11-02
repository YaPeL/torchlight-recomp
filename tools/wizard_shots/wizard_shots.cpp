// The wizard's parts on sample pages, played by a script, for the screenshots the tests and CI keep:
// wizard_shots --script FILE --shots DIR --browse_from FOLDER [--video DRIVER] [--width W --height H]
// [--scale S]. Nothing of the game: the sample pages' texts are not what a player reads, and the
// first start's real pages run over invented files.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#include <optional>
#include <string>

#include "game_menu/menu_strings.h"
#include "game_setup/game_files.h"
#include "game_setup/setup_pages.h"
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
  config.game_dir = game;
  config.files = files;
  config.ask_game = run.ask_game;
  config.ask_achievements = true;
  config.own_browser_only = true;
  config.browse_from = run.sample_root;
  config.test_install_delay_ms = run.install_delay;
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
    return RunSetup(options, setup);
  }
  // Both required: it never lists a real folder or waits for a player by default.
  if (options.script.empty() || browse_from.empty()) {
    std::fprintf(stderr, "usage: wizard_shots --script FILE --shots DIR --browse_from FOLDER\n");
    return 1;
  }
  SamplePages pages(Path(browse_from));
  return Run(options, pages);
}
