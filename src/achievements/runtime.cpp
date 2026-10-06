#include "achievements/runtime.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/achievement_manager.h>
#include "achievements/catalog.h"
#include "achievements/notifications.h"
#include "achievements/steam_backend.h"
#include <atomic>
#include <charconv>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>

REXCVAR_DEFINE_STRING(pc_achievements, "off", "Torchlight", "PC achievements: off, local, steam-dry-run, steam (writes separately gated)");
REXCVAR_DEFINE_STRING(pc_achievement_profile, "local", "Torchlight", "Local PC achievement profile (letters, digits, dash, underscore)");

REXCVAR_DEFINE_STRING(pc_steam_account, "", "Torchlight", "Expected Steam account ID; requires matching steam_ID local profile");
REXCVAR_DEFINE_STRING(pc_steam_writes, "off", "Torchlight", "Explicit Steam mutation gate: off or enabled; additionally requires compiled support");

REXCVAR_DEFINE_BOOL(pc_achievement_notifications_with_steam, false, "Torchlight", "Also show the game's own unlock notification with the real Steam backend (mode steam), which shows its own");

REXCVAR_DEFINE_STRING(pc_achievement_diagnostics, "off", "Torchlight", "Development achievement observations: off or trace");

namespace torchlight::achievements {
namespace {
std::mutex mutex;
std::unique_ptr<Service> service;
std::unique_ptr<SteamApi> steam_api;
std::unique_ptr<SteamBackend> steam;
std::filesystem::path path;
std::atomic<bool> enabled{false};
std::atomic<bool> closing{false};
std::atomic<bool> diagnostics{false};
std::atomic<bool> notify{false};
NotificationQueue notifications;
// Xbox set: announced through the same queue, with the SDK's texts.
std::atomic<bool> xbox_notify{false};
std::mutex labels_mutex;
std::map<std::string, std::string> labels;
rex::system::AchievementManager* xbox_manager = nullptr;
uint64_t xbox_listener = 0;
std::chrono::steady_clock::time_point last_save;
bool dirty = false;
void Persist() {
  if (!service || !dirty) return;
  std::string error;
  if (!service->Save(path,error)) REXLOG_ERROR("PC achievements: save failed: {}", error);
  else {
    dirty = false; last_save = std::chrono::steady_clock::now();
    if (diagnostics) REXLOG_INFO("PCACH {}",DescribeState("persist",service->state()));
  }
}
// PC flushes its stats on exit (0x40A1F5, app frame quit branch): evaluate the pending counters
// before the final checkpoint. The Xbox build quits by launching the dashboard, so the host's close
// and shutdown paths are this point. Mutex held. Nothing is announced: the game is going away.
void FlushOnExit() {
  if (!service) return;
  const auto before = service->state().unlocked;
  service->Flush();
  for (const auto& id : service->state().unlocked)
    if (!before.contains(id)) { REXLOG_INFO("PC achievement unlocked: {}",id); dirty = true; }
}
}
void Install(const std::filesystem::path& state_dir) {
  std::lock_guard lock(mutex);
  const auto mode = REXCVAR_GET(pc_achievements);
  if (mode == "off") return;
  if (mode != "local" && mode != "steam-dry-run" && mode != "steam") { REXLOG_ERROR("PC achievements: unsupported mode {}",mode); return; }
  const std::string profile = REXCVAR_GET(pc_achievement_profile);
  if (profile.empty() || profile.size()>64 || profile.find_first_not_of(
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) {
    REXLOG_ERROR("PC achievements: invalid local profile"); return;
  }
  // The player's data (the app passes the user data folder's achievements/); never below
  // game/reference data directories.
  if (state_dir.empty()) { REXLOG_ERROR("PC achievements: no state folder"); return; }
  std::error_code ec;
  path = std::filesystem::weakly_canonical(state_dir/(profile+".state"),ec);
  if (ec) { REXLOG_ERROR("PC achievements: cannot resolve state path"); return; }
  for (const auto& part:path) if (part == "reference") {
    REXLOG_ERROR("PC achievements: refusing to write below reference"); return;
  }
  std::filesystem::create_directories(path.parent_path(),ec);
  if (ec) { REXLOG_ERROR("PC achievements: cannot create state directory: {}",ec.message()); return; }
  service = std::make_unique<Service>(profile);
  service->DeferCounterEvaluation(true); // counters wait for PC's flush points (FlushCounters)
  std::string error;
  if (std::filesystem::exists(path,ec) && !service->Load(path,error)) {
    REXLOG_ERROR("PC achievements: refusing to overwrite invalid existing state: {}",error);
    service.reset(); return;
  }
  diagnostics = REXCVAR_GET(pc_achievement_diagnostics)=="trace";
  if (diagnostics) REXLOG_INFO("PCACH {}",DescribeState(
      std::filesystem::exists(path)?"reload":"fresh",service->state()));
  last_save = std::chrono::steady_clock::now();
  closing = false;
  notifications.Clear();
  notify = NotificationsEnabled(mode, REXCVAR_GET(pc_achievement_notifications_with_steam));
  enabled = true;
  REXLOG_INFO("PC achievements: profile {}, state {}",profile,path.string());
  if (mode != "local") {
    const std::string account_text=REXCVAR_GET(pc_steam_account);
    uint64_t account=0;
    const auto parsed=std::from_chars(account_text.data(),account_text.data()+account_text.size(),account);
    if (parsed.ec!=std::errc{} || parsed.ptr!=account_text.data()+account_text.size() || !account ||
        profile!="steam_"+std::to_string(account)) {
      REXLOG_ERROR("PC Steam disabled: expected account and steam_ID profile must match"); return;
    }
    const bool writes=mode=="steam" && REXCVAR_GET(pc_steam_writes)=="enabled";
    steam_api=CreateNativeSteamApi(SteamOptions{writes,account});
    steam=std::make_unique<SteamBackend>(*service,*steam_api,SteamOptions{writes,account},
        [](const auto& log){REXLOG_INFO("PC Steam: {}",log);},[]{
          std::string error;
          if (!service->Save(path,error)) { REXLOG_ERROR("PC Steam journal: {}",error); return false; }
          dirty=false; last_save=std::chrono::steady_clock::now(); return true;
        });
    REXLOG_INFO("PC Steam: writes requested {}, native mutation support {}",writes,steam_api->MutationsSupported());
  }
}
void PrepareForClose() {
  // ReXApp::OnClosing calls _Exit before OnShutdown. Stop accepting native events
  // and serialize a final local checkpoint BEFORE the SDK terminates guest threads.
  closing = true;
  std::lock_guard lock(mutex);
  FlushOnExit();
  Persist();
  if (diagnostics && service) REXLOG_INFO("PCACH {}",DescribeState("close-checkpoint",service->state()));
}
void Shutdown() {
  enabled = false; notify = false; notifications.Clear();
  if (xbox_manager) {
    xbox_manager->UnregisterCallback(xbox_listener);
    xbox_manager = nullptr; xbox_notify = false;
  }
  std::lock_guard lock(mutex);
  FlushOnExit();
  Persist(); steam.reset(); steam_api.reset(); service.reset(); }
void Pump() {
  std::lock_guard lock(mutex);
  if (!enabled || closing || !service) return;
  if (steam) steam->Tick(uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count()));
  if (dirty && std::chrono::steady_clock::now()-last_save>=std::chrono::seconds(10)) Persist();
}
bool NativeEnabled() { return enabled; }
bool Unlocked(std::string_view id) {
  std::lock_guard lock(mutex);
  return enabled && service && service->state().unlocked.contains(std::string(id));
}
void FlushCounters(const Observation& observation) {
  Apply([](Service& s) { s.Flush(); }, observation);
}
bool DiagnosticsEnabled() { return diagnostics; }
bool NotificationsShown() { return notify || xbox_notify; }
std::optional<std::string> NextNotification() { return notifications.Pop(); }
std::string NotificationText(const std::string& id) {
  std::lock_guard lock(labels_mutex);
  const auto it = labels.find(id);
  return it == labels.end() ? std::string() : it->second;
}
void ApplyAchievementSet(bool pc) {
  const std::string mode = REXCVAR_GET(pc_achievements);
  if (pc && mode == "off") {
    rex::cvar::SetFlagByName("pc_achievements", "local");
    REXLOG_INFO("achievements: PC set (local)");
  } else if (pc) {
    REXLOG_INFO("achievements: PC set, mode {} from the command line", mode);
  } else if (mode != "off") {
    REXLOG_INFO("achievements: --pc_achievements={} overrides the Xbox set", mode);
  } else {
    REXLOG_INFO("achievements: Xbox 360 set");
  }
}
std::optional<State> SnapshotState() {
  std::lock_guard lock(mutex);
  if (!enabled || !service) return std::nullopt;
  return service->state();
}
namespace {
std::mutex list_mutex;
std::function<void()> list_handler;
}
void SetAchievementListHandler(std::function<void()> handler) {
  std::lock_guard lock(list_mutex);
  list_handler = std::move(handler);
}
bool ShowAchievementList() {
  std::function<void()> handler;
  {
    std::lock_guard lock(list_mutex);
    handler = list_handler;
  }
  if (!handler) return false;
  handler();
  return true;
}
void UseXboxAchievements(rex::system::AchievementManager& manager) {
  if (xbox_manager) return;
  xbox_manager = &manager;
  // Guest threads (the XGI write); the queue and the label map are thread-safe.
  xbox_listener = manager.RegisterNotificationCallback([](const rex::system::AchievementEvent& event) {
    const std::string id = "XBOX:" + std::to_string(event.achievement.id);
    {
      std::lock_guard lock(labels_mutex);
      labels[id] = event.achievement.label;
    }
    REXLOG_INFO("Xbox achievement unlocked: {} {}", event.achievement.id, event.achievement.label);
    notifications.Push(id);
  });
  xbox_notify = true;
  REXLOG_INFO("achievements: Xbox unlocks announced by the game UI toast");
}
void Apply(const std::function<void(Service&)>& action, const Observation& observation) {
  std::lock_guard lock(mutex);
  if (!enabled || closing || !service) return;
  const auto before = service->state();
  action(*service);
  if (diagnostics && !observation.source.empty())
    REXLOG_INFO("PCACH {}",Describe(observation,before,service->state()));
  if (before == service->state()) return;
  dirty = true;
  // Catalog order, so several unlocks from one event are announced deterministically.
  for (const auto& definition : kCatalog) {
    const std::string id(definition.id);
    if (before.unlocked.contains(id) || !service->state().unlocked.contains(id)) continue;
    REXLOG_INFO("PC achievement unlocked: {}",id);
    if (notify) notifications.Push(id);
  }
  // Unlocks are persisted immediately; high-frequency counters checkpoint every 10 seconds.
  if (before.unlocked != service->state().unlocked ||
      std::chrono::steady_clock::now()-last_save >= std::chrono::seconds(10)) Persist();
}
} // namespace torchlight::achievements
