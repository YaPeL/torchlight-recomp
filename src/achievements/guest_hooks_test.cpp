// Exercises the actual app-owned overrides against deterministic guest-call doubles.
// This test reserves address space only; no game, Steam client, or original binary is run.
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>
#include "achievements/runtime.h"
#include "guest_abi/achievements.h"
#include <sys/mman.h>
#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace pc = torchlight::achievements;
namespace {
pc::Service service("hook-test"); bool enabled=true;
uint32_t calls=0, result=1, pumps=0;
int failures=0;
std::vector<std::string> observations;
void Check(bool ok, const char* text) { if (!ok) { ++failures; std::fprintf(stderr,"FAIL: %s\n",text); } }
bool Has(const char* id) { return service.state().unlocked.contains(id); }
}
namespace torchlight::achievements {
bool NativeEnabled() { return enabled; }
void Pump() { ++pumps; }
bool DiagnosticsEnabled() { return true; }
void Apply(const std::function<void(Service&)>& action, const Observation& observation) {
  const auto before=service.state(); action(service);
  if (!observation.source.empty()) observations.push_back(Describe(observation,before,service.state()));
}
void FlushCounters(const Observation& observation) { Apply([](Service& s) { s.Flush(); },observation); }
int list_opens=0; bool list_handler=false;
bool ShowAchievementList() { if (!list_handler) return false; ++list_opens; return true; }
bool Unlocked(std::string_view id) { return enabled && service.state().unlocked.contains(std::string(id)); }
}
namespace torchlight::dev { void OnGameLevelLoaded() {} }  // dev/guest_command.h, not linked here
extern "C" {
#define DOUBLE(address) REX_FUNC(__imp__sub_##address) { (void)base; ++calls; ctx.r3.u64=result; }
DOUBLE(821EF318) DOUBLE(823D9930) DOUBLE(82375500) DOUBLE(822D7298) DOUBLE(8287D8C0)
DOUBLE(821D7E78) DOUBLE(822D7520) DOUBLE(82361C78) DOUBLE(82361B00) DOUBLE(8221EF28)
DOUBLE(82217DA8) DOUBLE(8229FB58) DOUBLE(822238E0) DOUBLE(82294548) DOUBLE(822D8548)
DOUBLE(82217248) DOUBLE(823A1280) DOUBLE(822A0570) DOUBLE(822B9A60) DOUBLE(821DC980)
#define DECLARE(address) REX_EXTERN(sub_##address);
DECLARE(821EF318) DECLARE(823D9930) DECLARE(82375500) DECLARE(822D7298) DECLARE(8287D8C0)
DECLARE(821D7E78) DECLARE(822D7520) DECLARE(82361C78) DECLARE(82361B00) DECLARE(8221EF28)
DECLARE(82217DA8) DECLARE(8229FB58) DECLARE(822238E0) DECLARE(82294548) DECLARE(822D8548)
DECLARE(82217248) DECLARE(823A1280) DECLARE(822A0570) DECLARE(822B9A60) DECLARE(821DC980)
}
int main() {
  const size_t arena_size=size_t{1}<<32;
  auto* base=static_cast<uint8_t*>(mmap(nullptr,arena_size,PROT_READ|PROT_WRITE,
      MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
  if (base==MAP_FAILED) { std::perror("guest test mmap"); return EXIT_FAILURE; }
  auto put=[&](uint32_t a,uint32_t v) { for (int i=0;i<4;++i) base[a+i]=uint8_t(v>>(24-8*i)); };
  auto wide=[&](uint32_t address,const char* name) {
    const auto length=std::strlen(name); put(address+16,uint32_t(length)); put(address+20,7);
    for (size_t i=0;i<length;++i) base[address+2*i+1]=uint8_t(name[i]);
  };
  const uint32_t player=0x10000, shared=0x12000, array=0x13000, game=0x14000,
      manager=0x18000, unit=0x20000, ui=0x28000;
  put(0x8355A294,manager); put(manager+88,player);
  put(player+96,shared); put(shared+24,1); put(shared+20,array); put(array,game);
  put(game+5124,1); put(game+5148,3); put(player+700,std::bit_cast<uint32_t>(18000.f));
  put(player+1844,0); put(player+940,95000); put(player+240,100); base[player+2469]=1;
  wide(unit+48,"ORDRAK"); put(unit+892,0);
  PPCContext ctx{}; ctx.r3.u64=player; ctx.r4.u64=unit;
  sub_822D7298(ctx,base);
  Check(calls==1 && ctx.r3.u32==result && Has("HARDCORE_GOD") && Has("SPEED_KING") &&
      Has("PERFECT_VICTORY"),"ORDRAK actual bridge, original and return preserved");
  auto before=service.state(); ctx.r3.u64=player; ctx.r4.u64=unit; sub_822D7298(ctx,base);
  Check(service.state()==before,"duplicate final boss bridge");
  const auto calls_before=calls; ctx.r3.u64=7; sub_82375500(ctx,base);
  Check(calls==calls_before && ctx.r3.u32==0 && Has("KILL_BRINK"),"mapper no Xbox submission");
  sub_8287D8C0(ctx,base); Check(calls==calls_before && ctx.r3.u32==0,"Xbox UI suppressed");
  enabled=false; ctx.r3.u64=8; sub_82375500(ctx,base); sub_8287D8C0(ctx,base);
  Check(calls==calls_before+2 && !Has("KILL_LICH"),"off mode delegates both originals"); enabled=true;
  const uint32_t achievement=0x30000, text=0x31000;
  std::memcpy(base+text,"PET_MIMIC",10); put(achievement+12,text);
  put(achievement+28,9); put(achievement+32,16);
  ctx.r3.u64=achievement; sub_823D9930(ctx,base);
  Check(Has("PET_MIMIC") && ctx.r3.u32==result,"retained completion observed");
  Check(observations.back().find("\"source\":\"retained-completion\"")!=std::string::npos &&
      observations.back().find("\"id\":\"PET_MIMIC\"")!=std::string::npos,
      "completion observation captures ID before the original clobbers guest registers");
  ctx.r3.u64=player; ctx.r4.u64=1; ctx.r5.u64=5000; sub_822D7520(ctx,base);
  Check(service.state().stats[13]==5000 && Has("PLAYER_GOLD_IN_POCKET"),"gold delta and current snapshot");
  ctx.r3.u64=player; ctx.r4.u64=5; ctx.r5.u64=1; sub_822D7520(ctx,base);
  Check(service.state().stats[0]==1 && service.state().stats[18]==1,"both death counters");
  put(game+5124,6); before=service.state();
  ctx.r3.u64=player; ctx.r4.u64=1; ctx.r5.u64=5000; sub_822D7520(ctx,base);
  Check(service.state()==before,"non-gameplay context blocked"); put(game+5124,1);
  result=0; sub_82361C78(ctx,base); Check(service.state().stats[7]==1,"enchant failure counts");
  result=1; sub_82361C78(ctx,base); Check(service.state().stats[7]==1,"success not failure");
  put(ui+300,4); put(ui+168,game); put(game+56,player); base[player+2468]=0;
  ctx.r3.u64=ui; ctx.r4.u64=0x40000; sub_82361B00(ctx,base);
  Check(service.state().stats[11]==1 && service.state().stats[12]==100,"accepted retirement captured before quit");
  base[player+2468]=1; ctx.r3.u64=ui; ctx.r4.u64=0x40000; sub_82361B00(ctx,base);
  Check(service.state().stats[11]==1,"already retired not replayed");
  for (auto caller : {0x823CF6A8u,0x823CF710u,0x823CF778u}) {
    ctx.lr=caller; sub_8221EF28(ctx,base); ctx.lr=caller; sub_8221EF28(ctx,base);
  }
  Check(Has("HAT_TRICK") && service.state().stats[21]==1 && service.state().stats[22]==1 &&
      service.state().stats[23]==1,"class win qualification and duplicate Alric path idempotent");
  for (auto c : {std::pair{0x82356904u,27u}, {0x82355E90u,25u}, {0x82355EACu,26u}}) {
    ctx.lr=c.first; sub_8221EF28(ctx,base); Check(service.state().stats[c.second]==1,"name-qualified counter");
  }
  ctx.lr=0x8229FE60; sub_8221EF28(ctx,base);
  Check(service.state().stats[19]==1,"qualified Troll champion");
  // The attacker's type test from the host (guest_abi UnitIsType, type 28): the player's unit data
  // (+96, here the shared context object) holds an empty type tree at +16, so its own type counts.
  {
    const uint32_t player_tree=0x12800, player_head=0x12900;
    put(shared+16,player_tree); put(player_tree+12,player_head); put(player_head+4,player_head);
    base[player_head+33]=1; put(player+372,28);
  }
  put(player+372,30); ctx.lr=0x8229CAD4; ctx.r29.u64=player; ctx.f30.f64=10000.75;
  sub_821D7E78(ctx,base);
  Check(!Has("MAX_DMG_DONE"),"an attacker that is not the player does not count");
  put(player+372,28); ctx.lr=0x8229CAD4; ctx.r29.u64=player; ctx.f30.f64=10000.75;
  sub_821D7E78(ctx,base);
  Check(Has("MAX_DMG_DONE") && service.state().stats[3]==10000 && ctx.r29.u32==player &&
      ctx.f30.f64==10000.75,"applied damage max and guest registers preserved");
  ctx.lr=0x8229CAD4; ctx.f30.f64=1; sub_821D7E78(ctx,base);
  Check(service.state().stats[3]==10000,"smaller damage cannot lower max");
  before=service.state(); result=0; ctx.lr=0x82356904; sub_8221EF28(ctx,base);
  result=1; ctx.lr=123; sub_8221EF28(ctx,base);
  Check(service.state()==before,"nonmatch and unrelated compare ignored");
  ctx.lr=0x822D74C8; ctx.r3.u64=13; sub_82375500(ctx,base); before=service.state();
  std::memcpy(base+text,"KILL_ALRIC",11); put(achievement+28,10);
  ctx.lr=0x823CF458; ctx.r3.u64=achievement; sub_823D9930(ctx,base);
  Check(service.state()==before,"alternate Alric retained completion cannot mutate an already completed award");
  ctx.lr=0x823CF460; ctx.r3.u64=13; sub_82375500(ctx,base);
  Check(service.state()==before && observations.back().find("\"duplicate\":1")!=std::string::npos,
      "boss and alternate Alric delivery produce one PC completion without pending mutations");
  const auto fish_before=service.state().stats[6];
  for(int i=0;i<2;++i) { ctx.r3.u64=player; ctx.r4.u64=14; ctx.r5.u64=1; sub_822D7520(ctx,base); }
  Check(service.state().stats[6]==fish_before+2 && service.state().pending_deltas[6]==2,
      "separate legitimate equal fishing notifications remain additive");
  before=service.state(); ctx.r3.u64=26; sub_82375500(ctx,base);
  Check(service.state()==before,"unresolved mod predicate cannot unlock from guest mapper");
  // Deepest floor: game +56 level, level +296 zero-based depth, after the game's level loads.
  const uint32_t level=0x30000;
  auto load=[&](uint32_t from,int32_t depth) {
    put(game+56,level); put(level+296,uint32_t(depth));
    const auto prior=calls; ctx.lr=from; ctx.r3.u64=game; sub_82217DA8(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==result,"level load original always runs, result kept");
  };
  load(0x82215058,48);
  Check(service.state().stats[5]==48 && !Has("REACH_LVL_50"),"floor transition records depth below threshold");
  load(0x82215058,49);
  Check(service.state().stats[5]==49 && Has("REACH_LVL_50") && !Has("REACH_LVL_100"),"depth 49 (floor 50) unlocks REACH_LVL_50");
  load(0x82212F60,3);
  Check(service.state().stats[5]==49,"shallower level cannot lower the deepest floor");
  load(0x82250FA0,98);
  Check(service.state().stats[5]==49,"editor level load is not a game depth source");
  load(0x8221301C,98);
  Check(service.state().stats[5]==98 && !Has("REACH_LVL_100"),"game update load records depth 98");
  put(game+5124,0); load(0x82215058,99);
  Check(service.state().stats[5]==98 && !Has("REACH_LVL_100"),"ineligible context cannot raise the stat");
  put(game+5124,1); put(game+56,0); ctx.lr=0x82215058; ctx.r3.u64=game; sub_82217DA8(ctx,base);
  Check(service.state().stats[5]==98,"no current level, no update");
  load(0x82215058,99);
  Check(service.state().stats[5]==99 && Has("REACH_LVL_100"),"depth 99 (floor 100) unlocks REACH_LVL_100");
  Check(observations.back().find("\"source\":\"level-depth\"")!=std::string::npos &&
      observations.back().find("\"caller\":\"0x82215058\"")!=std::string::npos,"level-depth observation keeps the load's caller");
  // Items sold: add-gold returning to the merchant sale counts one item; other gold does not.
  auto gold=[&](uint32_t from) {
    const auto prior=calls; ctx.lr=from; ctx.r3.u64=player; ctx.r4.u64=250; sub_8229FB58(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==result,"add-gold original always runs, result kept");
  };
  const auto sold=service.state().stats[17];
  gold(0x8236B854); gold(0x8236B854);
  Check(service.state().stats[17]==sold+2 && service.state().pending_deltas[17]==2,"each merchant sale adds one item");
  Check(observations.back().find("\"source\":\"item-sold\"")!=std::string::npos &&
      observations.back().find("\"caller\":\"0x8236b854\"")!=std::string::npos,"item-sold observation keeps the sale's caller");
  before=service.state();
  for (uint32_t from : {0x8228E76Cu,0x82362514u,0x8236B644u,0x823CE424u}) gold(from);
  Check(service.state()==before,"pet town sale, enchanting, gambling and quest gold are not item sales");
  put(game+5124,0); gold(0x8236B854);
  Check(service.state()==before,"ineligible context cannot count a sale");
  put(game+5124,1);
  service.Assign(17,9999); gold(0x8236B854);
  Check(service.state().stats[17]==10000 && Has("SELL_ITEMS"),"10,000th sale unlocks SELL_ITEMS");
  enabled=false; before=service.state(); gold(0x8236B854);
  Check(service.state()==before,"native off leaves gold alone");
  enabled=true;
  // Exploding enemies: the effect call of the death's explosion block counts one.
  auto effect=[&](uint32_t from) {
    const auto prior=calls; ctx.lr=from; ctx.r3.u64=unit; sub_822238E0(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==result,"effect original always runs, result kept");
  };
  const auto exploded=service.state().stats[24];
  effect(0x8229D88C);
  Check(service.state().stats[24]==exploded+1,"explosion block counts one exploded enemy");
  Check(observations.back().find("\"source\":\"enemy-exploded\"")!=std::string::npos,"enemy-exploded observation");
  before=service.state(); effect(0x8229E198); effect(0x82227424);
  Check(service.state()==before,"other effect calls are not explosions");
  put(game+5124,0); effect(0x8229D88C); Check(service.state()==before,"ineligible context cannot count");
  put(game+5124,1);
  service.Assign(24,24); effect(0x8229D88C);
  Check(service.state().stats[24]==25 && Has("CRITICAL_STRIKE_ON_DEATH"),"25th explosion unlocks CRITICAL_STRIKE_ON_DEATH");
  enabled=false; before=service.state(); effect(0x8229D88C);
  Check(service.state()==before,"native off leaves effects alone");
  enabled=true;
  // Potions used on a pet: event 12 from the potion effect, target in r28 cast to an owned CCharacter.
  const uint32_t pet=0x34000;
  auto potion=[&](uint32_t from,uint32_t target) {
    const auto prior=calls; ctx.lr=from; ctx.r3.u64=player; ctx.r4.u64=12; ctx.r5.u64=1;
    ctx.r28.u64=target; ctx.r1.u64=0x70000; sub_82294548(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==result && ctx.r28.u32==target && ctx.r1.u32==0x70000,
        "event original always runs; result and caller registers kept");
  };
  // MSVC RTTI for the host cast (guest_abi CastToCharacter): a vtable whose locator's hierarchy lists
  // a CCharacter base (character classes) or only a CBaseUnit base (other units).
  auto rtti=[&](uint32_t object,uint32_t at,bool character) {
    const uint32_t vtable=at+4, locator=at+0x40, hierarchy=at+0x80, bases=at+0xC0, unit_base=at+0x100,
        character_base=at+0x140;
    put(object,vtable); put(at,locator); put(locator,0); put(locator+4,0); put(locator+16,hierarchy);
    put(hierarchy+8,character?2:1); put(hierarchy+12,bases);
    put(bases,unit_base); put(unit_base,0x834C27FC); put(unit_base+8,0); put(unit_base+12,0xFFFFFFFF);
    put(bases+4,character_base); put(character_base,0x834C2814); put(character_base+8,0);
    put(character_base+12,0xFFFFFFFF);
  };
  rtti(pet,0x5A000,true); rtti(player,0x5A400,true); rtti(unit,0x5A800,false);
  put(pet+1444,player);
  const auto potions=service.state().stats[20];
  potion(0x822B9BB8,pet);
  Check(service.state().stats[20]==potions+1,"potion on an owned pet counts one");
  Check(observations.back().find("\"source\":\"pet-potion\"")!=std::string::npos,"pet-potion observation");
  before=service.state();
  potion(0x822B9BB8,unit);
  Check(service.state()==before && observations.back().find("character=0x00000000")!=std::string::npos,
        "target that is not a CCharacter does not count; diagnostic reports the cast");
  put(player+1444,0); potion(0x822B9BB8,player);
  Check(service.state()==before && observations.back().find("owner=0x00000000")!=std::string::npos,
        "the player drinking (no owner) does not count; diagnostic reports the owner");
  potion(0x822B9BB8,0); Check(service.state()==before,"no target, no count");
  const auto diag_count=observations.size(); potion(0x82294058,pet);
  Check(observations.size()==diag_count && service.state()==before,"other character events are not pet potions");
  put(game+5124,0); potion(0x822B9BB8,pet); Check(service.state()==before,"ineligible context cannot count");
  put(game+5124,1);
  service.Assign(20,49); potion(0x822B9BB8,pet);
  Check(service.state().stats[20]==50 && Has("PET_POTIONS_50"),"50th pet potion unlocks PET_POTIONS_50");
  enabled=false; before=service.state(); potion(0x822B9BB8,pet);
  Check(service.state()==before,"native off leaves events alone");
  enabled=true;
  // Levers: CTriggerUnit activation end, PC's condition: SPAWNCLASS empty (string at +612, length
  // +628) and MAXSTATES 2 (+500 == 1), checked before the call.
  const uint32_t lever=0x38000;
  auto activate=[&](uint32_t from,uint32_t length,uint32_t needed) {
    put(lever+628,length); put(lever+500,needed); put(lever+604,1); // constructor sets +604 to 1
    const auto prior=calls; ctx.lr=from; ctx.r3.u64=lever; ctx.r4.u64=7; sub_822D8548(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==result,"activation original always runs, result kept");
  };
  const auto levers=service.state().stats[14];
  activate(0x822D8540,0,1);
  Check(service.state().stats[14]==levers+1,"trigger without SPAWNCLASS and MAXSTATES 2 counts one");
  Check(observations.back().find("\"source\":\"lever-pulled\"")!=std::string::npos &&
      observations.back().find("\"caller\":\"0x822d8540\"")!=std::string::npos,"lever-pulled observation");
  before=service.state();
  activate(0x822D8540,3,1); Check(service.state()==before,"trigger with a SPAWNCLASS (chest) does not count");
  activate(0x822D8540,0,0); activate(0x822D8540,0,2); Check(service.state()==before,"other MAXSTATES do not count");
  activate(0x82244224,0,1); Check(service.state()==before,"the other caller of the activation end does not count");
  put(game+5124,0); activate(0x822D8540,0,1); Check(service.state()==before,"ineligible context cannot count");
  put(game+5124,1);
  service.Assign(14,99); activate(0x822D8540,0,1);
  Check(service.state().stats[14]==100 && Has("PULLED_LEVERS_100"),"100th trigger unlocks PULLED_LEVERS_100");
  enabled=false; before=service.state(); activate(0x822D8540,0,1);
  Check(service.state()==before,"native off leaves activations alone");
  enabled=true;
  enabled=false; before=service.state(); put(level+296,500); load(0x82215058,500);
  Check(service.state()==before,"native off leaves level loads alone");
  enabled=true;
  const auto prior_calls=calls; sub_821EF318(ctx,base);
  Check(pumps==1 && calls==prior_calls+1 && ctx.r3.u32==result,"guest tick preserves original and pumps native callbacks");
  enabled=false; sub_821EF318(ctx,base); Check(pumps==1,"native off does not pump Steam");
  // MAX_FAME: PC's check at the rank-up (rank == FAMEGATE maximum); the guest's event 6 is ignored.
  enabled=true;
  {
    const uint32_t holder=0x3C000, graph=0x3D000, curves=0x3E000, long_curve=0x3F000, short_curve=0x3F400;
    put(0x835594CC,holder); put(holder+200,graph); put(graph+12,curves); put(graph+16,2);
    put(curves,short_curve); put(curves+4,long_curve);
    put(short_curve+116,0x50000); put(short_curve+120,0x50000+10*8);
    put(long_curve+116,0x51000); put(long_curve+120,0x51000+55*8);
    Check(torchlight::guest_abi::achievements::MaxFameRank(base)==55,"maximum over the graph's curves");
    auto rank_up=[&](uint32_t rank) {
      put(player+952,rank); const auto prior=calls; ctx.r3.u64=player; sub_822A0570(ctx,base);
      Check(calls==prior+1,"rank-up original always runs");
    };
    rank_up(33); Check(!Has("MAX_FAME"),"rank 33 (last title) is not PC's maximum");
    ctx.lr=0x822A0898; ctx.r3.u64=6; sub_82375500(ctx,base);
    Check(!Has("MAX_FAME"),"the guest's event 6 is not accepted in native mode");
    rank_up(54); Check(!Has("MAX_FAME"),"one below the maximum");
    rank_up(55); Check(Has("MAX_FAME"),"rank == FAMEGATE maximum unlocks MAX_FAME");
    put(holder+200,0); Check(torchlight::guest_abi::achievements::MaxFameRank(base)==0,"no graph: no maximum");
    enabled=false; const auto prior=calls; ctx.r3.u64=player; sub_822A0570(ctx,base);
    Check(calls==prior+1,"Xbox set: the rank-up runs untouched");
  }
  // PET_TRAINER: the pet inventory's spell use (return 0x8236BC3C) with PC's conditions.
  enabled=true;
  {
    const uint32_t scroll=0x60000, pet=0x61000, potion=0x62000, data=0x67000, tree=0x67100,
        head=0x67200;
    // Unit types from the host (guest_abi UnitIsType): an empty type map, so only each unit's own
    // type (+372) matches.
    put(data+16,tree); put(tree+12,head); put(head+4,head); base[head+33]=1;
    for (auto [unit,type] : {std::pair{scroll,129u},{pet,87u},{potion,33u}}) {
      put(unit+96,data); put(unit+372,type);
    }
    auto use=[&](uint32_t from,uint32_t item,uint32_t user) {
      const auto prior=calls; ctx.lr=from; ctx.r3.u64=item; ctx.r4.u64=user; ctx.r5.u64=user;
      sub_822B9A60(ctx,base);
      Check(calls==prior+1,"item use original always runs");
    };
    use(0x8236BEB0,scroll,pet); Check(!Has("PET_TRAINER"),"other item uses do not teach the pet");
    use(0x8236BC3C,potion,pet); Check(!Has("PET_TRAINER"),"not a spell");
    use(0x8236BC3C,scroll,player); Check(!Has("PET_TRAINER"),"the user is not a pet");
    put(pet+632,41); use(0x8236BC3C,scroll,pet); Check(!Has("PET_TRAINER"),"subtype 41 rejected");
    put(pet+632,42); use(0x8236BC3C,scroll,pet); Check(!Has("PET_TRAINER"),"subtype 42 rejected");
    put(pet+632,0);
    enabled=false; use(0x8236BC3C,scroll,pet); Check(!Has("PET_TRAINER"),"Xbox set: untouched");
    enabled=true; use(0x8236BC3C,scroll,pet); Check(Has("PET_TRAINER"),"teaching the pet a spell unlocks PET_TRAINER");
  }
  // BEAST_OF_BURDEN: the character update checks the first pet's bag (category 0) is full.
  {
    const uint32_t pets=0x63000, holder=0x63100, pet=0x64000, inv=0x65000, ids=0x65400,
        starts=0x65500, items=0x65600, item0=0x66000;
    auto update=[&](uint32_t unit) {
      const auto prior=calls; ctx.r3.u64=unit; sub_821DC980(ctx,base);
      Check(calls==prior+1,"character update original always runs");
    };
    update(player); Check(!Has("BEAST_OF_BURDEN"),"no pet");
    put(player+1452,pets); put(player+1456,pets+4); put(pets,holder); put(holder,pet);
    put(pet+1000,inv); put(inv+64,ids); put(ids,0); put(ids+4,1);
    put(inv+80,starts); put(inv+84,starts+8); put(starts,0); put(starts+4,3); put(inv+24,5);
    put(inv+32,items);
    for (uint32_t i=0;i<3;++i) { put(items+4*i,item0+64*i); put(item0+64*i+12,i); }
    put(inv+36,2); update(player); Check(!Has("BEAST_OF_BURDEN"),"one item short of a full bag");
    put(inv+36,3); put(game+5124,6); update(player);
    Check(!Has("BEAST_OF_BURDEN"),"not while the game is not playing");
    put(game+5124,1); enabled=false; update(player); Check(!Has("BEAST_OF_BURDEN"),"Xbox set: untouched");
    enabled=true; update(player); Check(Has("BEAST_OF_BURDEN"),"a full bag unlocks BEAST_OF_BURDEN");
    const auto count=observations.size(); update(player);
    Check(observations.size()==count,"after the unlock the update reads nothing more");
  }
  // PC timing: counters are evaluated only at PC's flush points; explicit completions are immediate.
  service.Reset(); service.DeferCounterEvaluation(true); enabled=true;
  load(0x82215058,49);
  Check(service.state().stats[5]==49 && !Has("REACH_LVL_50"),"arriving at floor 50 records depth 49 without unlocking");
  ctx.lr=0x822D74C8; ctx.r3.u64=13; sub_82375500(ctx,base);
  Check(Has("KILL_ALRIC") && !Has("REACH_LVL_50"),"explicit completion stays immediate while counters wait");
  load(0x82215058,50);
  Check(Has("REACH_LVL_50") && service.state().stats[5]==50,"the next level load's flush unlocks REACH_LVL_50");
  Check(std::any_of(observations.begin(),observations.end(),[](const std::string& o){
      return o.find("\"source\":\"counter-flush\"")!=std::string::npos &&
             o.find("REACH_LVL_50")!=std::string::npos; }),"counter-flush observation reports the unlock");
  service.Assign(17,10000);
  auto state_two=[&](uint32_t from) {
    const auto prior=calls; ctx.lr=from; ctx.r3.u64=0x50000; sub_823A1280(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==result,"state-2 call original always runs");
  };
  state_two(0x82212AB0); Check(!Has("SELL_ITEMS"),"other callers of that function are not flush points");
  state_two(0x82212AAC); Check(Has("SELL_ITEMS"),"entering game state 2 flushes");
  service.Assign(14,100);
  { const auto prior=calls; ctx.lr=0x82213160; ctx.r3.u64=game; ctx.r4.u64=1; sub_82217248(ctx,base);
    Check(calls==prior+1 && Has("PULLED_LEVERS_100"),"main menu level load flushes and runs the original"); }
  enabled=false; service.Assign(4,5000); load(0x82215058,1); state_two(0x82212AAC);
  Check(!Has("KILL_5000_MONSTERS"),"native off: no flush");
  enabled=true;
  // The "Achievements" button opens our list when the app set a handler, in either mode.
  { enabled=false; pc::list_handler=true; const auto prior=calls; ctx.r3.u64=7; sub_8287D8C0(ctx,base);
    Check(pc::list_opens==1 && calls==prior && ctx.r3.u32==0,"button opens our list, the SDK stub is not called");
    pc::list_handler=false; sub_8287D8C0(ctx,base);
    Check(calls==prior+1,"without a list the Xbox set keeps the original call");
    enabled=true; ctx.r3.u64=7; sub_8287D8C0(ctx,base);
    Check(calls==prior+1 && ctx.r3.u32==0,"PC set without a list: no Xbox UI");
  }
  munmap(base,arena_size);
  return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
