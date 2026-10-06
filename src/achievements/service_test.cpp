#include "achievements/service.h"
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
using namespace torchlight::achievements;
namespace {
int failures = 0;
void Check(bool ok, const char* what) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
}
bool Has(const Service& s, std::string_view id) { return s.state().unlocked.contains(std::string(id)); }
Character Player(int difficulty = 0, bool hc = false, double seconds = 18000, int deaths = 0) {
  return {"player-A", difficulty, hc, seconds, deaths, 90000};
}
}
int main() {
  std::set<std::string_view> names;
  for (const auto& d : kCatalog) Check(names.insert(d.id).second, "unique original PC ID");
  Check(names.size() == 66 && !Find("max_fame") && !Event(36) && !Event(UINT32_MAX), "case sensitive, bounded catalog");
  // Every explicit event is independently deliverable; duplicate delivery cannot change state.
  for (uint32_t event = 0; event < 36; ++event) {
    Service s("test");
    Check(s.Explicit(event), "first explicit unlock");
    const auto before = s.state();
    Check(!s.Explicit(event) && s.state() == before, "duplicate explicit event idempotent");
    Check(Has(s, Event(event)->id), "event maps to PC ID");
  }
  for (int difficulty = 0; difficulty <= 3; ++difficulty) {
    for (bool hc : {false, true}) {
      Service s("test"); s.Bind(Player(difficulty, hc)); s.Boss("ORDRAK");
      Check(Has(s,"BEASTSLAYERI"), "all four difficulty branches award first tier");
      Check(Has(s,"BEASTSLAYERII") == (difficulty >= 2), "second tier hard/very hard");
      Check(Has(s,"BEASTSLAYERIII") == (difficulty == 3), "third tier very hard");
      for (uint32_t tier = 0; tier < 4; ++tier)
        Check(Has(s,Event(17+tier)->id) == (hc && int(tier)==difficulty), "exact hardcore tier");
      const auto before = s.state(); s.Boss("ORDRAK");
      Check(s.state() == before, "duplicate boss event idempotent");
    }
  }
  struct TimeCase { double seconds; bool speedy, king; };
  for (auto c : {TimeCase{18000,true,true}, {18000.01,true,false}, {28800,true,false},
                 {28800.01,false,false}, {-1,false,false},
                 {std::numeric_limits<double>::quiet_NaN(),false,false}}) {
    Service s("test"); s.Bind(Player(0,false,c.seconds)); s.Boss("ORDRAK");
    Check(Has(s,"SPEEDY")==c.speedy && Has(s,"SPEED_KING")==c.king, "inclusive speed boundaries");
  }
  for (int deaths : {0,1,500}) {
    Service s("test"); s.Bind(Player(0,false,18000,deaths)); s.Boss("ORDRAK");
    Check(Has(s,"PERFECT_VICTORY")==(deaths==0), "per-character deathless");
    Check(s.state().stats[0]==0, "deathless snapshot never increments lifetime deaths");
  }
  Service normal("test"); normal.Bind(Player()); normal.Boss("ALRIC_EVIL");
  Check(Has(normal,"KILL_ALRIC") && !Has(normal,"BEASTSLAYERI"), "Alric is not final boss");
  normal.Boss("FIRSTHENCHMEN"); Check(normal.state().unlocked.size()==1, "skip henchman");
  for (auto name : {"BOSS1","LICH","ROOTGOLEMBOSS","EMBERCOLOSSUS","LAVATROLLBOSS","MEDEA"})
    normal.Boss(name);
  Check(normal.state().unlocked.size()==7, "all seven normal boss names");
  normal.Bind({"player-B", 3, true, 29000, 1, 0}); normal.Boss("ORDRAK");
  Check(!Has(normal,"SPEEDY") && !Has(normal,"PERFECT_VICTORY"), "character switch no stale inputs");
  Check(Has(normal,"KILL_ALRIC"), "account unlock persists across character switches");
  Service unknown("test"); unknown.Bind({"unknown",{},{},{},{},{}}); unknown.Boss("ORDRAK");
  Check(unknown.state().unlocked.empty(), "unknown inputs fail closed");

  // All thirty counter achievements: below, equal, above; duplicates and force completion.
  size_t counters=0;
  for (const auto& d : kCatalog) if (d.stat < 28) {
    ++counters;
    for (int offset : {-1,0,1}) {
      Service s("test"); s.Assign(d.stat,d.threshold+offset);
      Check(Has(s,d.id)==(offset>=0), "counter threshold triplet");
      const auto before=s.state(); s.Assign(d.stat,d.threshold+offset);
      Check(s.state()==before, "duplicate stat assignment");
    }
    Service forced("test"); Check(forced.Unlock(d.id), "forced counter completion");
    Check(forced.state().stats[d.stat]==d.threshold, "forced completion raises stat");
  }
  Check(counters==30, "all counter achievements exercised");
  Service stats("test"); stats.Bind(Player(0,true));
  for (auto pair : {std::pair{1u,13u},{3u,15u},{4u,10u},{5u,0u},{6u,4u},{11u,1u},
                    {12u,16u},{14u,6u},{15u,9u},{16u,8u}}) {
    stats.CharacterEvent(pair.first, 3, true);
    Check(stats.state().stats[pair.second]==3, "PC character event dispatcher");
  }
  Check(stats.state().stats[18]==3, "hardcore deaths separate");
  auto before=stats.state(); stats.CharacterEvent(1,100,false);
  Check(stats.state()==before, "PC eligibility gate");
  stats.CharacterEvent(1,10000,true);
  Check(Has(stats,"PLAYER_GOLD_IN_POCKET") && stats.state().stats[13]==10003,
        "pocket and lifetime gold distinct");
  stats.Maximum(5,49); stats.Maximum(5,1);
  Check(Has(stats,"REACH_LVL_50") && !Has(stats,"PLAYER_LEVEL_65"), "depth max not level");
  stats.ClassWin(21); stats.ClassWin(22); Check(!Has(stats,"HAT_TRICK"), "two classes insufficient");
  stats.ClassWin(23); Check(Has(stats,"HAT_TRICK"), "all three class counters");
  stats.Add(4, std::numeric_limits<int32_t>::max()); stats.Add(4,1);
  Check(stats.state().stats[4]==std::numeric_limits<int32_t>::max(), "overflow saturates");

  const auto path=std::filesystem::temp_directory_path()/("torchlight-pc-achievements-test-"+
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".state");
  std::string error;
  Check(stats.Save(path,error), "save local state");
  Service loaded("test"); Check(loaded.Load(path,error) && loaded.state()==stats.state(), "save/load round trip");
  loaded.Bind({"player-C",0,false,29001,1,0});
  Check(loaded.state()==stats.state(), "load and character switch never re-add totals");
  const auto saved=loaded.state(); loaded.Reconcile(saved); loaded.Reconcile(saved);
  Check(loaded.state()==saved, "reconciliation never increments account stats");
  Service wrong("wrong"); Check(!wrong.Load(path,error) && wrong.state().unlocked.empty(), "profile isolation");
  { std::ofstream out(path); out << "broken state"; }
  Check(!loaded.Load(path,error) && loaded.state()==saved, "corrupt load transactional");
  stats.SteamFlight(true);
  Check(stats.Save(path,error), "save uncertain journal");
  Service flight("test"); Check(flight.Load(path,error) && flight.state().steam_inflight &&
      flight.state().pending_deltas==stats.state().pending_deltas, "pending and uncertainty survive restart");
  { std::ofstream out(path); out << "TLPCACH 1\n\"test\"\n";
    for(int i=0;i<28;++i) out << (i==6?12:0) << '\n'; out << "0\n"; }
  Service legacy("test"); Check(legacy.Load(path,error) && legacy.state().stats[6]==12 &&
      legacy.state().pending_deltas[6]==0, "v1 migrates as absolute minima without guessing deltas");
  const auto target=path.string()+".target";
  { std::ofstream out(target); out << "sentinel"; }
  std::filesystem::create_symlink(target,path.string()+".tmp");
  Check(!stats.Save(path,error), "temporary symlink refuses writes");
  { std::ifstream in(target); std::string value; in >> value; Check(value=="sentinel", "symlink target remains read-only"); }
  std::filesystem::remove(path.string()+".tmp"); std::filesystem::remove(target);
  std::filesystem::remove(path); loaded.Reset();
  Check(loaded.state().unlocked.empty() && loaded.state().stats[4]==0, "local reset independent of Steam");
  for (const auto& definition:kCatalog) {
    Service one("test"); one.Add(15,3); one.Unlock(definition.id);
    const auto expected=one.state();
    Check(one.Save(path,error), "catalog persistence writes every original ID");
    Service restored("test");
    Check(restored.Load(path,error) && restored.state()==expected, "every ID round-trips with exact account/pending state");
    restored.Bind({"another-character",3,true,29001,7,0});
    Check(restored.Load(path,error) && restored.state()==expected, "repeat load does not replay character totals");
    Check(!restored.Unlock(definition.id) && restored.state()==expected, "reload completion remains idempotent for every ID");
  }
  std::filesystem::remove(path);
  // PC timing: deferred counters unlock only at a flush; explicit completions stay immediate.
  {
    Service s("deferred"); s.DeferCounterEvaluation(true); s.Bind(Player());
    s.Maximum(5, 49);
    Check(s.state().stats[5] == 49 && !Has(s, "REACH_LVL_50"), "deferred: depth 49 recorded, not yet evaluated");
    s.Explicit(0);
    Check(Has(s, "TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL") && !Has(s, "REACH_LVL_50"), "deferred: explicit completion is immediate, counters still wait");
    s.Flush();
    Check(Has(s, "REACH_LVL_50"), "deferred: the next flush evaluates the counter");
    for (int i = 0; i < 24; ++i) s.CharacterEvent(6, 1, true);
    s.Add(17, 9999); s.Add(17, 1);
    Check(!Has(s, "SELL_ITEMS") && s.state().stats[17] == 10000, "deferred: several changes wait together");
    s.Flush(); s.Flush();
    Check(Has(s, "SELL_ITEMS") && !Has(s, "KILL_5000_MONSTERS"), "deferred: flush evaluates each changed counter against its own threshold");
    s.Unlock("DIE_500");
    Check(Has(s, "DIE_500") && s.state().stats[0] == 500, "deferred: forced completion is immediate and raises its stat");
    Service r("deferred"); r.DeferCounterEvaluation(true);
    State remote; remote.profile = "deferred"; remote.stats[15] = 25000;
    r.Add(16, 5000);
    r.Reconcile(remote);
    Check(Has(r, "TRAVEL_25000") && Has(r, "DRINK_POTIONS"), "deferred: a Steam read evaluates remote and pending values, as PC's receive path");
  }
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
