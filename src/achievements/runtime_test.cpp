// Uses the actual app-owned runtime lifecycle; no ReXApp, guest, window or Steam API is run.
#include "achievements/runtime.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/achievement_manager.h>
#include <cstdlib>
#include <iostream>
namespace pc=torchlight::achievements;
namespace {
int failures=0;
void Check(bool value,const char* message) { if(!value) { ++failures; std::cerr<<message<<'\n'; } }
}
int main() {
  rex::InitLogging({},spdlog::level::err);  // {}: a const char* on SDK 0c7b01a, a path on bd833a2
  const std::filesystem::path root=TORCHLIGHT_ACHIEVEMENT_TEST_ROOT;
  std::filesystem::create_directories(root);
  const auto path=root/"lifecycle-test.state";
  std::filesystem::remove(path); std::filesystem::remove(path.string()+".tmp");
  Check(rex::cvar::SetFlagByName("pc_achievements","local"),"configure local only");
  Check(rex::cvar::SetFlagByName("pc_achievement_profile","lifecycle-test"),"configure isolated profile");
  pc::Install(root); Check(pc::NativeEnabled(),"actual runtime installed");
  pc::Apply([](pc::Service& s){s.Bind({"A",1,false,18000,0,100});s.CharacterEvent(3,1,true);});
  Check(!std::filesystem::exists(path),"one counter event is still uncheckpointed inside ten seconds");
  // Reproduce accepted window close WITHOUT calling Shutdown; formerly this lost the step.
  pc::PrepareForClose();
  pc::Service loaded("lifecycle-test"); std::string error;
  Check(loaded.Load(path,error) && loaded.state().stats[15]==1 && loaded.state().pending_deltas[15]==1,
        "pre-hard-exit close checkpoint saves last uncheckpointed counter");
  Check(pc::NativeEnabled(),"closing keeps native mode selected so hooks never fall back to Xbox");
  const auto before=loaded.state();
  pc::Apply([](pc::Service& s){s.Add(15,10);});pc::PrepareForClose();
  Check(loaded.Load(path,error) && loaded.state()==before,"repeat close and late notifications idempotent");
  pc::Shutdown();pc::Install(root);
  pc::Apply([](pc::Service& s){s.Bind({"B",3,true,29001,7,0});s.CharacterEvent(5,1,true);});
  pc::PrepareForClose();
  Check(loaded.Load(path,error) && loaded.state().stats[15]==1 && loaded.state().stats[0]==1 &&
        loaded.state().stats[18]==1,"runtime reload/character switching keep account stats without re-adding character totals");
  pc::Shutdown();pc::Install(root);
  pc::Apply([](pc::Service& s){s.Unlock("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL");});
  Check(loaded.Load(path,error) && loaded.state().unlocked.contains("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL"),
        "runtime unlock persisted immediately");
  const auto completed=loaded.state();pc::Shutdown();pc::Install(root);
  pc::Apply([](pc::Service& s){s.Unlock("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL");});
  pc::PrepareForClose();
  Check(loaded.Load(path,error) && loaded.state()==completed,"completion reload does not increment/replay progress");
  pc::Shutdown();std::filesystem::remove(path);std::filesystem::remove(path.string()+".tmp");

  // Unlock notifications: only new gameplay unlocks, once, in catalog order.
  const auto notify_path=root/"notification-test.state";
  std::filesystem::remove(notify_path);
  Check(rex::cvar::SetFlagByName("pc_achievement_profile","notification-test"),"configure notification profile");
  pc::Install(root);
  Check(pc::NotificationsShown() && !pc::NextNotification(),"local mode notifies; fresh profile has nothing queued");
  pc::Apply([](pc::Service& s){s.Unlock("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL");});
  Check(pc::NextNotification()==std::optional<std::string>("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL") &&
        !pc::NextNotification(),"new unlock announced once");
  pc::Apply([](pc::Service& s){s.Unlock("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL");});
  Check(!pc::NextNotification(),"repeated completion is not announced");
  pc::Apply([](pc::Service& s){s.Add(15,24999);});
  Check(!pc::NextNotification(),"counter below threshold announces nothing");
  pc::Apply([](pc::Service& s){s.Add(15,1);});
  Check(!pc::NextNotification(),"threshold crossing waits for a flush point (PC timing)");
  pc::FlushCounters();
  Check(pc::NextNotification()==std::optional<std::string>("TRAVEL_25000"),"flush point announces the crossing");
  pc::Apply([](pc::Service& s){s.Unlock("PERFECT_VICTORY");s.Unlock("BREAKABLES");});
  Check(pc::NextNotification()==std::optional<std::string>("BREAKABLES") &&
        pc::NextNotification()==std::optional<std::string>("PERFECT_VICTORY"),"one event, several unlocks: catalog order");
  pc::Apply([](pc::Service& s){s.Unlock("PET_MIMIC");});
  pc::Shutdown();
  Check(!pc::NextNotification(),"shutdown drops unannounced notifications");
  pc::Install(root);
  Check(!pc::NextNotification(),"loading saved unlocks announces nothing");
  pc::Apply([](pc::Service& s){s.Unlock("TRAVEL_25000");s.Unlock("PET_MIMIC");});
  Check(!pc::NextNotification(),"completions already in the saved state are not announced again");
  pc::Apply([](pc::Service& s){s.Add(4,5000);});
  pc::PrepareForClose();
  pc::Service flushed("notification-test");
  Check(flushed.Load(notify_path,error) && flushed.state().unlocked.contains("KILL_5000_MONSTERS"),
        "exit flush evaluates pending counters before the final checkpoint");
  pc::Apply([](pc::Service& s){s.Unlock("DIE_500");});
  Check(!pc::NextNotification(),"no announcement while closing");
  pc::Shutdown();std::filesystem::remove(notify_path);std::filesystem::remove(notify_path.string()+".tmp");
  pc::Install({}); Check(!pc::NativeEnabled(),"no state folder: native mode stays off");

  // Achievement set from the settings: "pc" turns the local mode on only when no mode was chosen.
  Check(rex::cvar::SetFlagByName("pc_achievements","off"),"reset mode");
  pc::ApplyAchievementSet(true);
  Check(rex::cvar::Query<std::string>("pc_achievements")=="local","pc set selects local");
  Check(rex::cvar::SetFlagByName("pc_achievements","steam-dry-run"),"command-line mode");
  pc::ApplyAchievementSet(true);
  Check(rex::cvar::Query<std::string>("pc_achievements")=="steam-dry-run","an explicit mode is kept");
  Check(rex::cvar::SetFlagByName("pc_achievements","off"),"reset mode");
  pc::ApplyAchievementSet(false);
  Check(rex::cvar::Query<std::string>("pc_achievements")=="off","xbox set leaves native mode off");

  // Xbox set: the SDK's unlock notifications reach our queue with the SDK's (official) texts.
  {
    rex::system::AchievementManager manager;
    rex::system::AchievementInfo award; award.id=3; award.label="Example award";
    manager.ReplaceAchievements({award});
    Check(!pc::NotificationsShown(),"nothing announced before a set is in use");
    pc::UseXboxAchievements(manager);
    Check(pc::NotificationsShown(),"Xbox set announces unlocks");
    manager.UnlockAchievement(3, rex::system::AchievementNotification::kShow);
    const auto id=pc::NextNotification();
    Check(id==std::optional<std::string>("XBOX:3") && pc::NotificationText(*id)=="Example award",
          "Xbox unlock queued with its official text");
    manager.UnlockAchievement(3, rex::system::AchievementNotification::kShow);
    Check(!pc::NextNotification(),"a repeated Xbox unlock is not announced again");
    Check(pc::NotificationText("TRAVEL_25000").empty(),"PC IDs use our own texts");
    pc::Shutdown();
    Check(!pc::NotificationsShown(),"shutdown stops listening");
    manager.UnlockAchievement(4, rex::system::AchievementNotification::kShow);
    Check(!pc::NextNotification(),"no announcement after shutdown");
  }
  rex::ShutdownLogging();
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
