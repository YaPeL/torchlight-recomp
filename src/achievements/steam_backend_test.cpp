#include "achievements/steam_backend.h"
#include <cstdlib>
#include <iostream>
#include <map>
using namespace torchlight::achievements;
namespace {
int failures=0;
void Check(bool value,const char* message) { if (!value) { ++failures; std::cerr<<message<<'\n'; } }
struct Fake final : SteamApi {
  SteamIdentity identity{kTorchlightAppId,42,true,true};
  bool available=true,request=true,read=true,sets=true,store=true,mutations=true;
  int init_calls=0,request_calls=0,shutdown_calls=0,stores=0;
  std::vector<std::string> achievements;
  std::vector<std::pair<std::string,int32_t>> stats;
  State remote{"steam_42",{},{},false,{}};
  std::vector<SteamCallback> callbacks;
  bool Initialize() override { ++init_calls; return available; }
  void Shutdown() override { ++shutdown_calls; }
  SteamIdentity Identity() override { return identity; }
  bool RequestStats() override { ++request_calls; return request; }
  std::vector<SteamCallback> Poll() override { auto result=callbacks; callbacks.clear(); return result; }
  bool Achievement(std::string_view id,bool& unlocked) override {
    unlocked=remote.unlocked.contains(std::string(id)); return read;
  }
  bool Stat(std::string_view id,int32_t& value) override {
    for (size_t i=0;i<remote.stats.size();++i) if (id==kStatKeys[i]) { value=remote.stats[i]; return read; }
    return false;
  }
  bool SetAchievement(std::string_view id) override { achievements.emplace_back(id); return sets; }
  bool SetStat(std::string_view id,int32_t value) override { stats.emplace_back(id,value); return sets; }
  bool Store() override { ++stores; return store; }
  bool MutationsSupported() const override { return mutations; }
  void Received(bool success=true) { callbacks.push_back({SteamCallback::Kind::Received,kTorchlightAppId,42,success,false}); }
  void Stored(bool success=true,bool invalid=false) { callbacks.push_back({SteamCallback::Kind::Stored,kTorchlightAppId,42,success,invalid}); }
};
struct Fixture {
  Service service{"steam_42"}; Fake api; std::vector<std::string> logs;
  bool persistence=true; int saves=0; State durable;
  SteamBackend backend;
  explicit Fixture(bool writes=false) : backend(service,api,{writes,42},
      [&](const auto& log){logs.push_back(log);},[&]{++saves; if(persistence) durable=service.state(); return persistence;}) {}
  void Ready() { backend.Tick(0); api.Received(); backend.Tick(1); }
};
}
int main() {
  {
    Fixture f; f.api.available=false; f.service.Explicit(2); f.backend.Tick(0); f.backend.Tick(4999);
    Check(f.api.init_calls==1 && f.api.stores==0,"unavailable retries without writes");
    f.api.available=true; f.backend.Tick(5000); f.api.Received(); f.backend.Tick(5001);
    Check(f.backend.status()=="dry-run" && f.service.state().unlocked.contains("PET_SEND_TO_TOWN"),"offline unlock retained");
    Check(f.logs.back()=="WOULD SetAchievement(PET_SEND_TO_TOWN)\nWOULD StoreStats()","deterministic dry-run");
    const auto logs=f.logs.size(); f.backend.Tick(6000); f.service.Explicit(2); f.backend.Tick(6001);
    Check(f.logs.size()==logs && f.api.achievements.empty(),"duplicate dry-run never mutates");
  }
  for (int wrong=0;wrong<5;++wrong) {
    Fixture f(true);
    if(wrong==0) f.api.identity.app=1;
    if(wrong==1) f.api.identity.account=43;
    if(wrong==2) f.api.identity.logged_in=false;
    if(wrong==3) f.api.identity.owns_app=false;
    if(wrong==4) f.api.identity.account=0;
    f.service.Explicit(2); f.backend.Tick(0);
    Check(f.api.request_calls==0 && f.api.stores==0,"identity gate before requests/writes");
  }
  {
    Service wrong("local"); Fake api; SteamBackend b(wrong,api,{true,42},[](const auto&){},[]{return true;});
    b.Tick(0); Check(api.request_calls==0,"local profile cannot export to Steam");
  }
  {
    Fixture f; f.api.request=false; f.backend.Tick(0); Check(f.backend.status()=="Steam stats request failed","failed request");
    f.api.request=true; f.backend.Tick(5000); f.api.Received(false); f.backend.Tick(5001);
    Check(f.backend.status()=="Steam stats read failed","failed stats callback");
    f.backend.Tick(10001); f.api.Received(); f.backend.Tick(10002); Check(f.backend.status()=="ready","read retry");
  }
  {
    Fixture f(true); f.api.remote.unlocked.insert("PET_SEND_TO_TOWN"); f.Ready(); f.service.Explicit(2); f.backend.Tick(2);
    Check(f.api.stores==0 && f.service.state().unlocked.contains("PET_SEND_TO_TOWN"),"already unlocked read without writes");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.service.Add(6,2); f.api.remote.stats[6]=10; f.Ready();
    Check(f.service.state().stats[6]==12 && f.service.state().pending_deltas[6]==2,"remote plus actual local delta, no event replay");
    Check(f.api.achievements.size()==1 && f.api.stats.size()==1 && f.api.stats[0].second==12 && f.api.stores==1,"set achievement/stat and store batch");
    Check(f.durable.steam_inflight && f.durable.pending_deltas[6]==2,"durable journal precedes writes");
    f.service.Add(6,3); f.api.Stored(); f.backend.Tick(2);
    Check(f.service.state().pending_deltas[6]==3 && f.api.stats.back().second==15,"ack preserves events during in-flight store");
    f.api.Stored(); f.backend.Tick(3); f.backend.Tick(4);
    Check(f.service.state().pending_deltas[6]==0 && !f.service.state().steam_inflight && f.api.stores==2,"successful callbacks acknowledge only their batch");
  }
  {
    Fixture f(true); f.service.Add(6,2); f.api.remote.stats[6]=10; f.Ready(); f.api.Stored(false); f.backend.Tick(2);
    Check(f.durable.steam_inflight,"failed store retains uncertainty marker across crash/auto-flush");
    f.backend.Tick(5002); Check(f.api.stats.size()==2 && f.api.stats[1].second==12,"failed callback retries absolute batch without double add");
    f.api.Stored(); f.backend.Tick(5003); Check(f.service.state().pending_deltas[6]==0,"retry success ack");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.api.store=false; f.Ready(); f.backend.Tick(100000);
    Check(f.api.stores==1 && f.service.state().steam_inflight,"false Store return uncertain, fail closed");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.Ready(); f.backend.Tick(60001); f.backend.Tick(90000);
    Check(f.api.stores==1 && f.service.state().steam_inflight,"unknown store timeout not replayed");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.Ready(); f.api.Stored(false,true); f.backend.Tick(2); f.backend.Tick(90000);
    Check(f.api.stores==1 && f.durable.steam_inflight,"invalid schema callback blocks retries across restart");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.persistence=false; f.Ready();
    Check(f.api.stores==0 && f.api.achievements.empty(),"failed reconciliation persistence prevents mutations");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.api.mutations=false; f.Ready();
    Check(f.api.achievements.empty() && f.backend.status()=="dry-run","compile/API capability gate");
  }
  {
    Service s("steam_42"); s.Add(6,2); s.SteamFlight(true); Fake api; api.remote.stats[6]=12;
    SteamBackend b(s,api,{true,42},[](const auto&){},[]{return true;}); b.Tick(0); api.Received(); b.Tick(1);
    Check(api.stores==0 && s.state().stats[6]==12 && s.state().pending_deltas[6]==2,"uncertain crash journal reads without replaying pending delta");
  }
  {
    Fixture f; f.service.Add(6,2); f.api.remote.stats[6]=10; f.backend.Tick(0);
    f.api.callbacks.push_back({SteamCallback::Kind::Received,1,42,true,false}); f.backend.Tick(1);
    Check(f.service.state().stats[6]==2,"foreign callback ignored"); f.api.Received(); f.backend.Tick(2);
    f.backend.Tick(3); Check(f.service.state().stats[6]==12 && f.service.state().pending_deltas[6]==2,"reconciliation does not increment journal");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.api.sets=false; f.Ready(); f.backend.Tick(90000);
    Check(f.api.stores==0 && f.service.state().steam_inflight,"partial Set failure remains uncertain and never stores");
  }
  {
    Fixture f; f.api.read=false; f.Ready();
    Check(f.service.state().unlocked.empty() && f.backend.status()=="Steam stats read failed","incomplete reads are transactional");
  }
  {
    Fixture f; f.backend.Tick(0); f.backend.Tick(10000);
    Check(f.backend.status()=="Steam stats request timed out","request timeout");
    f.backend.Tick(15000); Check(f.api.request_calls==2,"request timeout retry");
  }
  {
    Fixture f(true); f.service.ClassWin(21); f.api.remote.stats[21]=1; f.api.remote.stats[22]=1; f.api.remote.stats[23]=1; f.Ready();
    Check(f.service.state().stats[21]==1 && f.api.stats.empty() && f.service.state().unlocked.contains("HAT_TRICK"),"first class wins reconcile as booleans and evaluate HAT_TRICK without increments");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.Ready(); f.api.identity.account=43; f.api.Stored(); f.backend.Tick(2);
    Check(f.service.state().steam_inflight && f.api.stores==1,"account switch cannot acknowledge old account batch");
    f.api.identity.account=42; f.backend.Tick(5002); f.api.Received(); f.backend.Tick(5003);
    Check(f.api.stores==1 && f.service.state().steam_inflight,"account transition with in-flight batch remains blocked even after return");
  }
  {
    Fixture f(true); f.service.Explicit(2); f.Ready(); f.api.Stored(); f.backend.Tick(2);
    const auto state=f.service.state(); f.api.Stored(); f.backend.Tick(3);
    Check(f.service.state()==state && f.api.stores==1,"duplicate store callback ignored");
  }
  return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
