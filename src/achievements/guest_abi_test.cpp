#include "guest_abi/achievements.h"
#include "guest_abi/game_ui_sheet.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <bit>
namespace abi = torchlight::guest_abi::achievements;
int main() {
  std::vector<uint8_t> memory(0x20000);  // fixtures above abi::kMinGuestObject
  auto put = [&](uint32_t address, uint32_t value) {
    for (int i=0;i<4;++i) memory[address+i]=uint8_t(value>>(24-8*i));
  };
  // Literal fixture offsets taken independently from guest instruction comments.
  const uint32_t player=0x10100, shared=0x11000, array=0x11100, context=0x11400, boss=0x14000;
  put(player+96,shared); put(shared+24,1); put(shared+20,array); put(array,context);
  put(context+5148,3); put(context+5124,1);
  put(player+700,std::bit_cast<uint32_t>(18000.f));
  put(player+1844,7); put(player+940,123456); memory[player+2469]=1;
  auto inputs=abi::Player(memory.data(),player);
  bool ok = inputs.difficulty==3 && inputs.played_seconds==18000 && inputs.hardcore==true &&
      inputs.deaths==7 && inputs.gold==123456 && abi::Eligible(memory.data(),player);
  put(boss+48+16,6); put(boss+48+20,7);
  const char* name="ORDRAK";
  for (int i=0;i<6;++i) memory[boss+48+2*i+1]=uint8_t(name[i]);
  put(boss+892,std::bit_cast<uint32_t>(0.75f));
  ok &= abi::UnitName(memory.data(),boss)=="ORDRAK" && abi::BossDefeated(memory.data(),boss);
  put(boss+892,std::bit_cast<uint32_t>(1.f)); ok &= !abi::BossDefeated(memory.data(),boss);
  put(shared+24,0); inputs=abi::Player(memory.data(),player);
  ok &= !inputs.difficulty && !abi::Eligible(memory.data(),player);
  // Game UI sheet: CGameUI +0x340 sheet, +0x344 loading, +0x358 ratings splash; children {+44,+48}.
  namespace sheet_abi = torchlight::guest_abi::game_ui_sheet;
  const uint32_t game_ui=16384, sheet=20000, children=20480, loading=21000, ratings=21100, hud=21200;
  put(game_ui+0x340,sheet); put(game_ui+0x344,loading); put(game_ui+0x358,ratings);
  put(sheet+44,children); put(sheet+48,children+4); put(children,hud);
  ok &= sheet_abi::IsChild(memory.data(),sheet,hud) && !sheet_abi::Covered(memory.data(),game_ui);
  put(children+4,loading); put(sheet+48,children+8);
  ok &= sheet_abi::Covered(memory.data(),game_ui);
  put(children+4,ratings); ok &= sheet_abi::Covered(memory.data(),game_ui);
  put(sheet+48,children+4); ok &= !sheet_abi::Covered(memory.data(),game_ui);
  put(sheet+48,children-4); ok &= !sheet_abi::IsChild(memory.data(),sheet,hud); // corrupt range
  ok &= !sheet_abi::IsChild(memory.data(),0,hud) && !sheet_abi::IsChild(memory.data(),sheet,0);
  if (!ok) std::fprintf(stderr,"FAIL: independent PPC layout fixture\n");
  // Pet bag: first pet through the holder; category fill as 0x4E5A30 / 0x4E5E10, with the category
  // as the last one (end = total slots +24) and as an inner one (end = next category's start).
  {
    std::vector<uint8_t> m(0x30000);
    auto at = [&](uint32_t address, uint32_t value) {
      for (int i=0;i<4;++i) m[address+i]=uint8_t(value>>(24-8*i));
    };
    const uint8_t* b = m.data();
    const uint32_t unit=0x10400, pets=0x10800, holder=0x10900, pet=0x10C00, inv=0x12000, ids=0x12400, starts=0x12500,
        items=0x12600, item0=0x13000;
    ok &= abi::FirstPet(b,unit)==0;
    at(unit+1452,pets); at(unit+1456,pets+4); at(pets,holder); at(holder,pet);
    ok &= abi::FirstPet(b,unit)==pet;
    at(pet+1000,inv);
    auto items_in = [&](std::initializer_list<uint32_t> slots) {
      uint32_t i=0;
      for (auto slot : slots) { at(items+4*i,item0+64*i); at(item0+64*i+12,slot); ++i; }
      at(inv+32,items); at(inv+36,i);
    };
    // Bag last: ids {7, 0}, starts {0, 10}, 16 slots -> capacity 6.
    at(inv+64,ids); at(ids,7); at(ids+4,0); at(inv+80,starts); at(inv+84,starts+8);
    at(starts,0); at(starts+4,10); at(inv+24,16);
    items_in({3,10,11,12,13,14});
    auto fill = abi::InventoryCategoryFill(b,inv,0);
    ok &= fill && fill->capacity==6 && fill->items==5;
    items_in({3,10,11,12,13,14,15});
    fill = abi::InventoryCategoryFill(b,inv,0);
    ok &= fill && fill->items==6 && fill->items==fill->capacity;
    // Bag first, not last: ids {0, 1}, starts {0, 12}, 14 slots -> capacity 12; slot 12 is category 1.
    at(ids,0); at(ids+4,1); at(starts+4,12); at(inv+24,14);
    items_in({0,1,2,3,4,5,6,7,8,9,10,12});
    fill = abi::InventoryCategoryFill(b,inv,0);
    ok &= fill && fill->capacity==12 && fill->items==11;
    items_in({0,1,2,3,4,5,6,7,8,9,10,11,12});
    fill = abi::InventoryCategoryFill(b,inv,0);
    ok &= fill && fill->items==12 && fill->capacity==12;
    fill = abi::InventoryCategoryFill(b,inv,1);
    ok &= fill && fill->capacity==2 && fill->items==1;
    fill = abi::InventoryCategoryFill(b,inv,5);  // not listed: the first category, as the game does
    ok &= fill && fill->capacity==12 && fill->items==12;
    ok &= !abi::InventoryCategoryFill(b,0,0);
    at(pet+632,41); ok &= abi::RejectedPetSubtype(b,pet);
    at(pet+632,42); ok &= abi::RejectedPetSubtype(b,pet);
    at(pet+632,40); ok &= !abi::RejectedPetSubtype(b,pet);
    // Unit types (0x821D7CE0 / 0x821D7D38 read from the host): the unit's own type, an ancestor from
    // the type tree's std::map entry for its type, an absent type, and a corrupt tree.
    const uint32_t data=0x20000, tree=0x20100, head=0x20200, n10=0x20300, n87=0x20400, n200=0x20500,
        anc=0x20600, scroll=0x20700;
    at(pet+96,data); at(data+16,tree); at(tree+12,head); at(pet+372,87);
    m[head+33]=1; at(head+4,n87);  // root
    for (auto n : {n10,n87,n200}) { at(n+0,head); at(n+8,head); m[n+33]=0; }
    at(n87+0,n10); at(n87+8,n200); at(n10+12,10); at(n87+12,87); at(n200+12,200);
    at(n87+16,anc); at(n87+20,anc+8); at(anc,5); at(anc+4,31);
    ok &= abi::UnitIsType(b,pet,87) && abi::UnitIsType(b,pet,31) && abi::UnitIsType(b,pet,5);
    ok &= !abi::UnitIsType(b,pet,129) && !abi::UnitIsType(b,0,87);
    at(scroll+96,data); at(scroll+372,150);  // no map entry for 150: only its own type
    ok &= abi::UnitIsType(b,scroll,150) && !abi::UnitIsType(b,scroll,31);
    at(scroll+372,200); at(n200+16,0); at(n200+20,0);  // entry with no ancestors
    ok &= abi::UnitIsType(b,scroll,200) && !abi::UnitIsType(b,scroll,31);
    at(tree+12,0x27); ok &= !abi::UnitIsType(b,pet,31);  // a freed link: not followed
    at(holder,0x38); ok &= abi::FirstPet(b,unit)==0;     // same for the pet's holder
  }
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
