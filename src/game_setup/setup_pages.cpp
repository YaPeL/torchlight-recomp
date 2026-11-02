#include "game_setup/setup_pages.h"

#include <chrono>
#include <mutex>

#include "platform/user_folders.h"

namespace fs = std::filesystem;

namespace torchlight::game_setup {
namespace {

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

fs::path FromUtf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

}  // namespace

// The system's picker answers on its own thread, maybe after the player went on: the answer is kept
// in this, which outlives the pages, and one nobody waits for any more is ignored.
struct SetupPages::PickState {
  std::mutex mutex;
  bool pending = false, answered = false, failed = false, folder = false;
  std::string path;

  static void Answer(void* user, const char* const* files, int) {
    auto* holder = static_cast<std::shared_ptr<PickState>*>(user);
    {
      std::lock_guard lock((*holder)->mutex);
      (*holder)->answered = true;
      (*holder)->failed = !files;
      (*holder)->path = files && files[0] ? files[0] : "";
    }
    delete holder;
  }
};

SetupPages::SetupPages(SetupConfig config) : config_(std::move(config)) {}

SetupPages::~SetupPages() {
  if (worker_.joinable()) {
    cancel_ = true;
    worker_.join();
  }
}

std::string SetupPages::Tr(std::string_view english) const {
  return Render({std::string(english), {}}, config_.translate);
}

void SetupPages::Attach(const wizard::Fonts& fonts, SDL_Window* window) {
  window_ = window;
  ui_.Attach(fonts, Tr(kTextTitle));
  ui_.texts.back = Tr(kTextBack);
  ui_.texts.hint = Tr(kTextHint);
  ui_.texts.hint_next = Tr(kTextHintNext);
  browser_.texts.above = Tr(kTextFolderAbove);
  browser_.texts.this_computer = Tr(kTextThisComputer);
  browser_.texts.use_folder = Tr(kTextUseFolder);
  browser_.texts.folder_mark = Tr(kTextFolderMark) + "  ";
  if (config_.ask_game) {
    ui_.Reset(kGame);
  } else {
    AfterGame();
  }
}

void SetupPages::AfterGame() {
  ui_.Reset(config_.ask_achievements ? kAchievements : kReady);
}

bool SetupPages::Frame() {
  PollPick();
  ui_.Frame(ui_.page() != kWorking, [&] {
    switch (ui_.page()) {
      case kGame: GamePage(); break;
      case kBrowser: BrowserPage(); break;
      case kWorking: WorkingPage(); break;
      case kAchievements: AchievementsPage(); break;
      case kReady: ReadyPage(); break;
    }
  });
  return completed_ || quit_;
}

std::string SetupPages::Name() const {
  static const char* const names[] = {"game", "browser", "working", "achievements", "ready"};
  return names[ui_.page()];
}

std::string SetupPages::Detail() const {
  if (ui_.page() == kReady && achievements_) {
    return std::string(settings::AchievementSetName(*achievements_));
  }
  return error_;
}

bool SetupPages::Busy() const {
  if (worker_.joinable()) return true;  // cancelled from its page: an install is never left behind
  if (!pick_) return false;
  std::lock_guard lock(pick_->mutex);
  return pick_->pending && !pick_->answered;  // a picker's window answers into this
}

void SetupPages::GamePage() {
  const bool picking = Busy();
  ui_.Heading(Tr(kTextPageGame), "");
  ImGui::BeginChild("body", ImVec2(0, ui_.BodyHeight()), ImGuiChildFlags_NavFlattened);
  ui_.Wrapped(Render({std::string(kTextIntro), {{"where", Utf8(config_.game_dir)}}},
                     config_.translate));
  ImGui::Spacing();
  if (picking) ImGui::BeginDisabled();
  if (ui_.entered()) ui_.FocusNext();
  const bool package = ImGui::Button(Tr(kTextChoosePackage).c_str());
  ui_.Took();
  ImGui::SameLine();
  const bool folder = ImGui::Button(Tr(kTextChooseFolder).c_str());
  if (picking) ImGui::EndDisabled();
  ImGui::SameLine();
  // Also while a picker is open: one that never shows (no desktop portal) leaves this way out.
  const bool browse = ImGui::Button(Tr(kTextBrowsePad).c_str());
  ImGui::Spacing();
  if (picking) ImGui::BeginDisabled();
  if (ImGui::Button(Tr(kTextQuit).c_str())) quit_ = true;
  if (picking) ImGui::EndDisabled();
  ImGui::Spacing();
  if (!error_.empty()) ui_.Error(error_);
  ImGui::EndChild();
  ui_.Footer(nullptr, false, false);
  if (package) Pick(false);
  if (folder) Pick(true);
  if (browse) OpenBrowser();
}

void SetupPages::OpenBrowser() {
  if (pick_) {
    std::lock_guard lock(pick_->mutex);
    pick_->pending = false;  // a late answer is ignored
  }
  error_.clear();
  fs::path start = config_.browse_from;
  if (start.empty()) start = platform::HomeFolder();
  browser_.Open(start);
  ui_.Go(kBrowser);
}

void SetupPages::Pick(bool folder) {
  if (config_.own_browser_only) {
    OpenBrowser();
    return;
  }
  error_.clear();
  pick_ = std::make_shared<PickState>();
  pick_->pending = true;
  pick_->folder = folder;
  const std::string title = Tr(folder ? kTextFolderPicker : kTextPackagePicker);
  SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window_);
  SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
  SDL_ShowFileDialogWithProperties(folder ? SDL_FILEDIALOG_OPENFOLDER : SDL_FILEDIALOG_OPENFILE,
                                   PickState::Answer, new std::shared_ptr<PickState>(pick_),
                                   properties);
  SDL_DestroyProperties(properties);
}

void SetupPages::PollPick() {
  if (!pick_) return;
  bool failed, folder;
  std::string path;
  {
    std::lock_guard lock(pick_->mutex);
    if (!pick_->pending || !pick_->answered) return;
    pick_->pending = false;
    failed = pick_->failed;
    folder = pick_->folder;
    path = pick_->path;
  }
  if (failed) {
    OpenBrowser();  // no picker here (the Steam Deck's Game Mode)
  } else if (!path.empty()) {
    Chosen(FromUtf8(path), folder ? Source::kFolder : Source::kPackage);
  }
}

void SetupPages::Chosen(const fs::path& path, Source source) {
  error_.clear();
  if (source == Source::kPackage) {
    const auto facts = ReadPackageFacts(path);
    const Message problem = facts ? CheckPackage(*facts) : Message{std::string(kTextNotPackage), {}};
    if (!problem.empty()) {
      error_ = Render(problem, config_.translate);
      log_.push_back({false, "game setup: " + Utf8(path) + " rejected: " + Render(problem)});
      if (ui_.page() == kBrowser) ui_.Back();
      return;
    }
  }
  source_ = source;
  from_ = Utf8(path);
  cancel_ = false;
  finished_ = false;
  done_ = 0;
  total_ = 0;
  phase_ = 0;
  worker_ = std::thread([this, path, source] {
    result_ = Install(source, path, config_.game_dir, config_.files,
                      [this](Phase phase, uint64_t done, uint64_t total) {
                        phase_ = int(phase);
                        done_ = done;
                        total_ = total;
                        if (config_.test_install_delay_ms) {
                          std::this_thread::sleep_for(
                              std::chrono::milliseconds(config_.test_install_delay_ms));
                        }
                        return !cancel_.load();
                      });
    finished_ = true;
  });
  if (ui_.page() == kBrowser) ui_.Back();
  ui_.Go(kWorking);
}

void SetupPages::BrowserPage() {
  if (auto chosen = browser_.Draw(ui_, Tr(kTextBrowseTitle), "")) {
    std::error_code ec;
    const bool is_folder = fs::is_directory(*chosen, ec);
    Chosen(*chosen, is_folder ? Source::kFolder : Source::kPackage);
  }
}

void SetupPages::WorkingPage() {
  if (finished_) {
    worker_.join();
    if (Cancelled(result_.error)) {
      log_.push_back({false, "game setup: install from " + from_ + " cancelled"});
      ui_.Back();
    } else if (!result_.error.empty()) {
      error_ = Render(result_.error, config_.translate);
      log_.push_back({true, "game setup: install from " + from_ + " failed: " + Render(result_.error)});
      ui_.Back();
    } else {
      installed_ = true;
      log_.push_back({false, "game setup: installed the game files from " + from_ + " to " +
                                 Utf8(config_.game_dir)});
      if (!result_.moved_to.empty()) {
        log_.push_back({false, "game setup: the previous one was moved to " + result_.moved_to});
      }
      AfterGame();
    }
    return;
  }
  const bool checking = phase_.load() == int(Phase::kChecking);
  ui_.Heading(Tr(kTextPageWorking), "");
  ImGui::BeginChild("body", ImVec2(0, ui_.BodyHeight()), ImGuiChildFlags_NavFlattened);
  ui_.Wrapped(Tr(checking ? kTextChecking
                          : source_ == Source::kPackage ? kTextExtracting : kTextCopying));
  ImGui::Spacing();
  const uint64_t total = total_.load();
  ImGui::ProgressBar(total ? float(double(done_.load()) / double(total)) : 0.0f,
                     ImVec2(-FLT_MIN, 0));
  ImGui::Spacing();
  if (ui_.entered()) ui_.FocusNext();
  if (cancel_.load()) ImGui::BeginDisabled();
  if (ImGui::Button(Tr(kTextCancel).c_str())) cancel_ = true;
  ui_.Took();
  if (cancel_.load()) ImGui::EndDisabled();
  ImGui::EndChild();
  ui_.Footer(nullptr, false, false);
}

void SetupPages::AchievementsPage() {
  ui_.Heading(Tr(kTextPageAchievements), "");
  ImGui::BeginChild("body", ImVec2(0, ui_.BodyHeight()), ImGuiChildFlags_NavFlattened);
  ui_.Wrapped(Tr(kTextAchievementsChoice));
  ImGui::Spacing();
  if (ui_.entered()) ui_.FocusNext();
  const bool xbox = ImGui::Button(Tr(kTextAchievementsXbox).c_str());
  ui_.Took();
  ImGui::SameLine();
  const bool pc = ImGui::Button(Tr(kTextAchievementsPc).c_str());
  ImGui::EndChild();
  ui_.Footer(nullptr, false, ui_.can_go_back());
  if (xbox || pc) {
    achievements_ = pc ? settings::AchievementSet::kPc : settings::AchievementSet::kXbox;
    ui_.Go(kReady);
  }
}

void SetupPages::ReadyPage() {
  ui_.Heading(Tr(kTextPageReady), "");
  ImGui::BeginChild("body", ImVec2(0, ui_.BodyHeight()), ImGuiChildFlags_NavFlattened);
  ui_.Wrapped(Tr(kTextReady));
  ImGui::EndChild();
  if (ui_.Footer(Tr(kTextStart).c_str(), true, ui_.can_go_back(), true)) completed_ = true;
}

}  // namespace torchlight::game_setup
