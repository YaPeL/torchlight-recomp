#include "achievements/service.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace torchlight::achievements {
Service::Service(std::string profile) { state_.profile = std::move(profile); }
void Service::Bind(Character character) { character_ = std::move(character); }
bool Service::Unlock(std::string_view id) {
  const auto* d = Find(id);
  if (!d) return false;
  const bool was_unlocked = state_.unlocked.contains(std::string(id));
  if (d->stat < state_.stats.size()) {
    state_.stats[d->stat] = std::max(state_.stats[d->stat], d->threshold);
    Changed(d->stat);
  }
  state_.unlocked.emplace(id);
  return !was_unlocked;
}
bool Service::Explicit(uint32_t event) {
  const auto* d = Event(event);
  return d && Unlock(d->id);
}
void Service::Evaluate(uint8_t stat) {
  for (const auto& d : kCatalog)
    if (d.stat == stat && state_.stats[stat] >= d.threshold) state_.unlocked.emplace(d.id);
}
void Service::Changed(uint8_t stat) {
  if (deferred_) changed_.insert(stat);
  else Evaluate(stat);
}
void Service::Flush() {
  for (const auto stat : changed_) Evaluate(stat);
  changed_.clear();
}
bool Service::Assign(uint8_t stat, int32_t value) {
  if (stat >= state_.stats.size() || value < 0) return false;
  if (state_.stats[stat] == value) return false;
  state_.stats[stat] = value;
  Changed(stat);
  return true;
}
bool Service::Maximum(uint8_t stat, int32_t value) {
  return stat < state_.stats.size() && value > state_.stats[stat] && Assign(stat, value);
}
bool Service::Add(uint8_t stat, int32_t delta) {
  if (stat >= state_.stats.size() || delta <= 0) return false;
  const int64_t value = int64_t(state_.stats[stat]) + delta;
  const auto next = int32_t(std::min(value, int64_t(std::numeric_limits<int32_t>::max())));
  const auto actual = next - state_.stats[stat];
  state_.pending_deltas[stat] = int32_t(std::min(int64_t(state_.pending_deltas[stat]) + actual,
                                               int64_t(std::numeric_limits<int32_t>::max())));
  return Assign(stat, next);
}
void Service::CharacterEvent(uint32_t event, int32_t delta, bool eligible) {
  if (!eligible || character_.identity.empty()) return;
  // PC 0x004D6690 jump table; NOT achievement-event enums.
  constexpr std::array<int8_t, 17> mapping{-1,13,-1,15,10,0,4,-1,-1,-1,-1,1,16,-1,6,9,8};
  if (event >= mapping.size()) return;
  if (mapping[event] >= 0) Add(uint8_t(mapping[event]), delta);
  if (event == 5 && character_.hardcore.value_or(false)) Add(18, delta);
  if (event == 1 && character_.gold && int64_t(*character_.gold) + delta >= 100000)
    Explicit(35);
}
void Service::Boss(std::string_view name) {
  constexpr std::array<std::string_view, 7> bosses{
      "BOSS1", "LICH", "ROOTGOLEMBOSS", "EMBERCOLOSSUS", "LAVATROLLBOSS", "MEDEA", "ALRIC_EVIL"};
  for (uint32_t i = 0; i < bosses.size(); ++i)
    if (name == bosses[i]) { Explicit(7 + i); return; }
  if (name != "ORDRAK" || character_.identity.empty()) return;
  // PC 0x004D8DE0. Unknown inputs suppress only their dependent awards.
  if (character_.deaths && *character_.deaths == 0) Explicit(34);
  if (character_.difficulty && *character_.difficulty >= 0 && *character_.difficulty <= 3) {
    const int difficulty = *character_.difficulty;
    Explicit(14);
    if (difficulty >= 2) Explicit(15);
    if (difficulty == 3) Explicit(16);
    if (character_.hardcore.value_or(false)) Explicit(17 + difficulty);
  }
  if (character_.played_seconds && std::isfinite(*character_.played_seconds) &&
      *character_.played_seconds >= 0) {
    const double hours = *character_.played_seconds / 3600.0;
    if (hours <= 8.0) Explicit(21);
    if (hours <= 5.0) Explicit(22);
  }
}
void Service::ClassWin(uint8_t stat) {
  if (stat < 21 || stat > 23) return;
  // PC 0x005EBC72..0x005EBD9F increments only when this class has no win yet.
  if (state_.stats[stat] == 0) Add(stat, 1);
  if (state_.stats[21] > 0 && state_.stats[22] > 0 && state_.stats[23] > 0) Explicit(23);
}
void Service::Reconcile(const State& remote) {
  if (remote.profile != state_.profile) return;
  for (size_t i = 0; i < state_.stats.size(); ++i)
    state_.stats[i] = std::max(state_.stats[i], remote.stats[i]);
  for (const auto& id : remote.unlocked) if (Find(id)) state_.unlocked.insert(id);
  // No unlock/stat producer replay. Account threshold evaluation is not a gameplay increment.
  // Like PC's receive path (0x5F75CE -> 0x5F72F0) it also evaluates the pending changes.
  for (uint8_t i = 0; i < state_.stats.size(); ++i) Evaluate(i);
  changed_.clear();
  if (state_.stats[21]>0 && state_.stats[22]>0 && state_.stats[23]>0) state_.unlocked.emplace("HAT_TRICK");
}
void Service::Acknowledge(const State& batch) {
  if (batch.profile != state_.profile) return;
  for (size_t i=0; i<state_.pending_deltas.size(); ++i)
    state_.pending_deltas[i] = std::max(0, state_.pending_deltas[i]-batch.pending_deltas[i]);
  state_.steam_inflight = false;
}
void Service::Reset() { const auto profile=state_.profile; state_={}; state_.profile=profile; character_={}; changed_.clear(); }
bool Service::Save(const std::filesystem::path& path, std::string& error) const {
  error.clear();
  if (state_.profile.empty() || state_.profile.size() > 256) { error = "invalid profile"; return false; }
  const auto temporary = path.string() + ".tmp";
  std::error_code temp_error;
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(temporary,temp_error))) {
    error="refusing symlink temporary state"; return false;
  }
  std::ofstream out(temporary, std::ios::trunc);
  if (!out) { error = "cannot open temporary achievement state"; return false; }
  out << "TLPCACH 2\n" << std::quoted(state_.profile) << '\n';
  for (int32_t value : state_.stats) out << value << '\n';
  for (int32_t value : state_.pending_deltas) out << value << '\n';
  out << state_.steam_inflight << '\n';
  out << state_.unlocked.size() << '\n';
  for (const auto& id : state_.unlocked) out << id << '\n';
  out.flush();
  if (!out) { error = "cannot write achievement state"; return false; }
  out.close();
  if (!out) { error = "cannot close achievement state"; return false; }
#if defined(__linux__)
  // Make the uncertainty marker durable before Steam can mutate its client cache.
  const int file=::open(temporary.c_str(),O_RDONLY|O_NOFOLLOW);
  if (file<0) { error="cannot open state for durable flush"; return false; }
  const bool synced=::fsync(file)==0;
  const bool closed=::close(file)==0;
  if (!synced || !closed) { error="cannot durably flush achievement state"; return false; }
#endif
  std::error_code ec;
  std::filesystem::rename(temporary, path, ec);
  if (ec) { error = ec.message(); return false; }
#if defined(__linux__)
  const auto parent=path.parent_path().empty()?std::filesystem::path("."):path.parent_path();
  const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);
  if (directory<0) { error="cannot open state directory for durable flush"; return false; }
  const bool directory_synced=::fsync(directory)==0;
  const bool directory_closed=::close(directory)==0;
  if (!directory_synced || !directory_closed) { error="cannot durably flush state directory"; return false; }
#endif
  return true;
}
bool Service::Load(const std::filesystem::path& path, std::string& error) {
  error.clear();
  std::ifstream in(path);
  State loaded;
  std::string magic;
  int version = 0;
  if (!(in >> magic >> version >> std::quoted(loaded.profile)) || magic != "TLPCACH" ||
      (version != 1 && version != 2) || loaded.profile != state_.profile) {
    error = "invalid achievement state or wrong profile"; return false;
  }
  for (auto& value : loaded.stats) if (!(in >> value) || value < 0) {
    error = "invalid stat"; return false;
  }
  if (version == 2) {
    for (auto& value : loaded.pending_deltas) if (!(in >> value) || value < 0) {
      error = "invalid pending delta"; return false;
    }
    int flight = 0;
    if (!(in >> flight) || (flight != 0 && flight != 1)) {
      error = "invalid Steam journal"; return false;
    }
    loaded.steam_inflight = flight == 1;
  }
  size_t count = 0;
  if (!(in >> count) || count > kCatalog.size()) { error = "invalid unlock count"; return false; }
  for (size_t i = 0; i < count; ++i) {
    std::string id;
    if (!(in >> id) || !Find(id) || !loaded.unlocked.insert(id).second) {
      error = "invalid achievement ID"; return false;
    }
  }
  in >> std::ws;
  if (!in.eof()) { error = "trailing achievement data"; return false; }
  state_ = std::move(loaded);
  character_ = {};
  changed_.clear();
  for (uint8_t i = 0; i < state_.stats.size(); ++i) Evaluate(i);
  if (state_.stats[21]>0 && state_.stats[22]>0 && state_.stats[23]>0) state_.unlocked.emplace("HAT_TRICK");
  return true;
}
} // namespace torchlight::achievements
