// The PC achievement list's rows: order, unlocked state, counter progress and availability.
#include "achievements/list_model.h"
#include <cstdlib>
#include <iostream>
namespace pc=torchlight::achievements;
namespace {
int failures=0;
void Check(bool value,const char* message) { if(!value) { ++failures; std::cerr<<message<<'\n'; } }
}
int main() {
  pc::State state;
  state.stats[15]=30000;  // steps beyond TRAVEL_25000
  state.stats[9]=37;      // gambles
  state.unlocked.insert("TRAVEL_25000");
  state.unlocked.insert("TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL");
  const auto rows=pc::BuildList(state);
  Check(rows.size()==66 && rows.front().id=="TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL","66 rows in catalog order");
  int available=0;
  for (const auto& r:rows) {
    if (r.availability==pc::Availability::kAvailable) ++available;
    if (r.id=="TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL") Check(r.unlocked && !r.counter,"explicit achievement: no progress");
    if (r.id=="TRAVEL_25000") Check(r.unlocked && r.counter && r.value==25000 && r.threshold==25000,"progress capped at the threshold");
    if (r.id=="GAMBLER_50") Check(!r.unlocked && r.value==37 && r.threshold==50,"counter progress");
    if (r.id=="PET_POTIONS_50") Check(r.availability==pc::Availability::kAvailable,"pet potions: earnable (used from the pet's inventory)");
    if (r.id=="MODS_5") Check(r.availability==pc::Availability::kOutOfScope,"mods: out of scope");
    if (r.id=="PET_TRAINER" || r.id=="BEAST_OF_BURDEN") Check(r.availability==pc::Availability::kAvailable,"pet trainer and beast of burden: earnable");
    Check(!r.english.empty() && r.english!=r.id,"every row has its text");
  }
  Check(available==63,"63 earnable achievements");
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
