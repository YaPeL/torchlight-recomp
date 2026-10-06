#pragma once

#include "achievements/catalog.h"
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <optional>

namespace torchlight::achievements {

// Account progress survives character switches. Character inputs are snapshots, never added
// again during load/reconciliation. No platform IDs occur in this boundary.
struct State {
  std::string profile;
  std::array<int32_t, 28> stats{};
  std::array<int32_t, 28> pending_deltas{}; // actual local increments, not character snapshots
  bool steam_inflight = false; // durable uncertainty marker before any write batch
  std::set<std::string> unlocked;
  bool operator==(const State&) const = default;
};
struct Character {
  std::string identity;
  std::optional<int> difficulty;
  std::optional<bool> hardcore;
  std::optional<double> played_seconds;
  std::optional<int32_t> deaths;
  std::optional<int32_t> gold;
};

class Service {
 public:
  explicit Service(std::string profile);
  const State& state() const { return state_; }
  const Character& character() const { return character_; }
  void Bind(Character character);
  bool Unlock(std::string_view id); // PC force completion also raises a selected account stat.
  bool Explicit(uint32_t event);
  bool Add(uint8_t stat, int32_t delta);
  bool Maximum(uint8_t stat, int32_t value);
  bool Assign(uint8_t stat, int32_t value);
  void CharacterEvent(uint32_t event, int32_t delta, bool eligible);
  void Boss(std::string_view name);
  void ClassWin(uint8_t stat); // callers must establish the PC win predicate.
  // PC evaluates counter achievements only when its stats manager flushes (0x5F7980 -> pending
  // changes 0x5F72F0): at the start of a level load, entering game state 2, loading the main menu
  // and on exit. Deferred, counter changes wait for Flush(); explicit completions stay immediate.
  void DeferCounterEvaluation(bool deferred) { deferred_ = deferred; }
  void Flush();
  void Reconcile(const State& remote); // monotonic merge; never invokes gameplay/Add.
  void SteamFlight(bool active) { state_.steam_inflight = active; }
  void Acknowledge(const State& batch); // only after successful StoreStats callback
  void Reset(); // local only
  bool Save(const std::filesystem::path& path, std::string& error) const;
  bool Load(const std::filesystem::path& path, std::string& error);
 private:
  void Evaluate(uint8_t stat);
  void Changed(uint8_t stat);
  State state_;
  bool deferred_ = false;
  std::set<uint8_t> changed_; // counters awaiting evaluation (PC's pending changes, not persisted)
  Character character_;
};
} // namespace torchlight::achievements
