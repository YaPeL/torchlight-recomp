// The wizard's parts on sample pages, played by a script, for the screenshots the tests and CI keep:
// wizard_shots --script FILE --shots DIR --browse_from FOLDER [--video DRIVER] [--width W --height H]
// [--scale S]. Nothing of the game. The pages' texts are samples, not what a player reads.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

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

  void Attach(const Fonts& fonts) override { ui_.Attach(fonts, "Torchlight Recomp"); }
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

}  // namespace

int main(int argc, char** argv) {
  Options options;
  options.width = 960;
  std::string browse_from;
  for (int i = 1; i + 1 < argc; i += 2) {
    const std::string key = argv[i], value = argv[i + 1];
    if (key == "--script") options.script = value;
    else if (key == "--shots") options.shots = value;
    else if (key == "--browse_from") browse_from = value;
    else if (key == "--video") options.video = value;
    else if (key == "--width") options.width = std::atoi(value.c_str());
    else if (key == "--height") options.height = std::atoi(value.c_str());
    else if (key == "--scale") options.scale = float(std::atof(value.c_str()));
    else {
      std::fprintf(stderr, "wizard_shots: unknown option %s\n", key.c_str());
      return 1;
    }
  }
  // Both required: it never lists a real folder or waits for a player by default.
  if (options.script.empty() || browse_from.empty()) {
    std::fprintf(stderr, "usage: wizard_shots --script FILE --shots DIR --browse_from FOLDER\n");
    return 1;
  }
  SamplePages pages(Path(browse_from));
  return Run(options, pages);
}
