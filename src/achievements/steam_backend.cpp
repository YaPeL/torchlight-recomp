#include "achievements/steam_backend.h"
#include <algorithm>
#include <limits>
#include <sstream>
namespace torchlight::achievements {
SteamBackend::SteamBackend(Service& s, SteamApi& api, SteamOptions options, Log log, Persist persist)
    : service_(s), api_(api), options_(options), log_(std::move(log)), persist_(std::move(persist)) {
  blocked_ = s.state().steam_inflight;
  if (blocked_) { status_="uncertain previous store"; log_(status_+"; writes blocked pending manual journal review"); }
}
SteamBackend::~SteamBackend() { if (initialized_) api_.Shutdown(); }
bool SteamBackend::IdentityValid() {
  const auto i=api_.Identity();
  const auto text="identity app="+std::to_string(i.app)+" account="+std::to_string(i.account)+
      " logged-in="+std::to_string(i.logged_in)+" owns-app="+std::to_string(i.owns_app);
  if (text!=last_identity_) { log_(text); last_identity_=text; }
  return i.app==kTorchlightAppId && i.account!=0 && i.logged_in && i.owns_app &&
      options_.expected_account==i.account &&
      service_.state().profile=="steam_"+std::to_string(i.account);
}
void SteamBackend::Failure(uint64_t now, std::string_view reason) {
  status_=reason; log_(status_); retry_at_=now+5000;
}
bool SteamBackend::Read() {
  State read; read.profile=service_.state().profile;
  for (const auto& d:kCatalog) {
    bool unlocked=false;
    if (!api_.Achievement(d.id,unlocked)) return false;
    if (unlocked) read.unlocked.emplace(d.id);
  }
  for (size_t i=0;i<read.stats.size();++i)
    if (!api_.Stat(kStatKeys[i],read.stats[i]) || read.stats[i]<0) return false;
  remote_=read;
  if (!blocked_) {
    // Delta journal records ONLY locally earned increments. Maxima, forced completions,
    // and class-first-win booleans use absolute minima. Reads/retries never append deltas.
    for (uint8_t i=0;i<read.stats.size();++i) {
      const auto delta = (i>=21 && i<=23) ? 0 : service_.state().pending_deltas[i];
      read.stats[i]=int32_t(std::min(int64_t(read.stats[i])+delta, int64_t(INT32_MAX)));
    }
  }
  service_.Reconcile(read);
  return persist_();
}
void SteamBackend::Tick(uint64_t now) {
  if (!initialized_) {
    if (now<retry_at_) return;
    if (!api_.Initialize()) { Failure(now,"Steam unavailable"); return; }
    initialized_=true;
  }
  if (!IdentityValid()) {
    // Never acknowledge an old batch using a new account's callbacks.
    ready_=false; requested_=false;
    if (waiting_) { waiting_=false; blocked_=true; }
    if (status_!="wrong Steam identity/context") Failure(now,"wrong Steam identity/context");
    return;
  }
  for (const auto& cb:api_.Poll()) {
    if (cb.app!=kTorchlightAppId || cb.account!=options_.expected_account) continue;
    if (cb.kind==SteamCallback::Kind::Received && requested_) {
      requested_=false;
      if (cb.success && Read()) { ready_=true; status_=blocked_?"uncertain previous store":"ready"; }
      else Failure(now,"Steam stats read failed");
    } else if (cb.kind==SteamCallback::Kind::Stored && waiting_) {
      waiting_=false;
      if (cb.success) {
        remote_=batch_;
        service_.Acknowledge(batch_);
        if (!persist_()) { blocked_=true; status_="store acknowledged but journal save failed"; log_(status_); }
        else status_="stored";
      } else {
        // Retry absolute targets in this process after an explicit failure. Retain
        // the durable marker: Steam's cache could still auto-flush after a crash/exit.
        if (!persist_() || cb.invalid_parameter) blocked_=true;
        Failure(now,cb.invalid_parameter?"Steam rejected stat schema; writes blocked":"Steam StoreStats failed");
      }
    }
  }
  if (waiting_) {
    if (now>=deadline_) {
      waiting_=false; blocked_=true; status_="Steam store outcome unknown; writes blocked"; log_(status_);
    }
    return;
  }
  if (requested_) {
    if (now>=deadline_) { requested_=false; Failure(now,"Steam stats request timed out"); }
    return;
  }
  if (!ready_) {
    if (now<retry_at_) return;
    if (!api_.RequestStats()) { Failure(now,"Steam stats request failed"); return; }
    requested_=true; deadline_=now+10000; status_="requesting"; return;
  }
  if (blocked_ || now<retry_at_) return;
  State desired=service_.state();
  for (size_t i=0;i<desired.stats.size();++i) {
    const auto delta=(i>=21 && i<=23)?0:desired.pending_deltas[i];
    desired.stats[i]=std::max(desired.stats[i], int32_t(std::min(
        int64_t(remote_.stats[i])+delta,int64_t(INT32_MAX))));
  }
  std::ostringstream plan;
  std::vector<std::string> achievements;
  std::vector<size_t> stats;
  for (const auto& id:desired.unlocked) if (!remote_.unlocked.contains(id)) {
    achievements.push_back(id); plan<<"WOULD SetAchievement("<<id<<")\n";
  }
  for (size_t i=0;i<desired.stats.size();++i) if (desired.stats[i]>remote_.stats[i]) {
    stats.push_back(i); plan<<"WOULD SetStat("<<kStatKeys[i]<<", "<<desired.stats[i]<<")\n";
  }
  if (achievements.empty() && stats.empty()) return;
  plan<<"WOULD StoreStats()";
  if (!options_.enable_writes || !api_.MutationsSupported()) {
    status_="dry-run";
    if (last_plan_!=plan.str()) { log_(plan.str()); last_plan_=plan.str(); }
    return;
  }
  if (!IdentityValid()) { blocked_=true; status_="identity changed before mutation; writes blocked"; log_(status_); return; }
  batch_=desired;
  service_.SteamFlight(true);
  // Save before Set*: Steam may automatically flush its client cache at process exit.
  if (!persist_()) { blocked_=true; status_="cannot persist Steam journal; writes blocked"; log_(status_); return; }
  bool accepted=true;
  for (const auto& id:achievements) accepted=api_.SetAchievement(id) && accepted;
  for (auto i:stats) accepted=api_.SetStat(kStatKeys[i],desired.stats[i]) && accepted;
  if (!accepted || !api_.Store()) {
    // Partial cache mutation or a failed Store return has uncertain eventual acceptance.
    blocked_=true; status_="Steam mutation batch uncertain; writes blocked"; log_(status_); return;
  }
  waiting_=true; deadline_=now+60000; status_="awaiting store callback";
}
} // namespace torchlight::achievements
