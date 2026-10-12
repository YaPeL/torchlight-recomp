#include "mods/save_units_install.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_set>
#include <vector>

#include <rex/logging.h>

#include "game_setup/game_files.h"
#include "mods/unit_cache.h"
#include "save_import/schema.h"

namespace torchlight::mods {

namespace {
std::optional<SaveUnitsReport> g_changes;
}  // namespace

const std::optional<SaveUnitsReport>& SaveUnitsChanges() { return g_changes; }

namespace {

// What the check counted on, for the comparison once the game has loaded its index.
struct Assumed {
  bool pending = false;                   // mods' units: compare after the load
  KnownUnits known;
  std::unordered_set<int64_t> base_guids;  // the Xbox index's
  std::filesystem::path user_data_root;
};
Assumed g_assumed;
// Set on the game's thread by CompareWithLoadedUnits, read by the save hook (save_block_hooks.cpp).
std::atomic<bool> g_saving_blocked{false};

std::string TitleFolder() {
  char title[16];
  std::snprintf(title, sizeof(title), "%08X", game_setup::kTitleId);
  return title;
}

void Run(const std::filesystem::path& user_data_root, const KnownUnits& known) {
  std::string error;
  const auto schema = save_import::Schema::Embedded(error);
  if (!schema) {
    REXLOG_ERROR("units: no save schema ({}); saves not checked", error);
    return;
  }
  if (!known.complete) REXLOG_WARN("units: saves not changed: {}", known.incomplete_why);
  SaveUnitsReport report = ProtectSaves(user_data_root, TitleFolder(), *schema, known, std::chrono::system_clock::now(),
                                        [](const std::string& line) { REXLOG_INFO("{}", line); });
  if (!report.changed.empty() || report.failed) g_changes = std::move(report);
}

// The units of the index the game is expected to load with the mods' units: the cached one when
// it exists (exactly what the game will load), otherwise the base merged with the mods' units as
// the builder merges them (a unit the game then cannot load shows in the comparison after the
// load).
KnownUnits ExpectedUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                         const std::vector<ModUnitFile>& files, const std::optional<UnitIndex>& base) {
  if (const auto identity = PakIdentity(pak)) {
    std::string log, error;
    if (const auto path = FindCachedUnitIndex(data_dir / "cache" / "unitdata", UnitCacheKey(files, *identity), &log)) {
      std::ifstream in(*path, std::ios::binary);
      const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
      if (const auto cached = ParseUnitIndex(bytes, &error)) return KnownUnitsOfIndex(*cached);
    }
  }
  std::string why;
  return MakeExpectedUnits(base, ModUnitEntries(files, &why), why);
}

}  // namespace

void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan) {
  g_changes.reset();
  g_assumed = {};
  g_saving_blocked = false;
  if (user_data_root.empty()) return;
  std::string error;
  const auto base = ReadPakUnitIndex(pak, &error);
  if (!base) REXLOG_WARN("units: the game's unit index not read ({})", error);
  const auto files = plan ? ScanModUnitFiles(data_dir / "mods", *plan) : std::vector<ModUnitFile>{};
  if (files.empty()) {
    Run(user_data_root, MakeKnownUnits(base, {}, true));
    return;
  }
  g_assumed.pending = true;
  g_assumed.user_data_root = user_data_root;
  g_assumed.known = ExpectedUnits(data_dir, pak, files, base);
  if (base) g_assumed.base_guids = KnownUnitsOfIndex(*base).guids;
  REXLOG_INFO("units: mods bring units; saves checked against the {} units the game is expected to load",
              g_assumed.known.guids.size());
  Run(user_data_root, g_assumed.known);
}

void CompareWithLoadedUnits(const std::optional<std::unordered_set<int64_t>>& loaded) {
  if (!g_assumed.pending) return;
  g_assumed.pending = false;
  // Not known what the game holds: every mods' unit the check counted on may be missing.
  const std::vector<int64_t> missing = UnitsNotLoaded(g_assumed.known, loaded ? *loaded : g_assumed.base_guids);
  if (missing.empty()) return;
  REXLOG_WARN("units: {} units the saves' check counted on are not in the index the game loaded{}; the game "
              "drops saved items of them when it loads a character",
              missing.size(), loaded ? "" : " (its content is not known)");
  for (size_t i = 0; i < missing.size() && i < 20; ++i) REXLOG_WARN("units:   GUID {}", missing[i]);
  if (!g_changes) g_changes.emplace();
  g_changes->units_not_loaded = missing.size();
  // No character is loaded yet (the index loads first), so nothing has been dropped or saved.
  std::string error;
  const auto schema = save_import::Schema::Embedded(error);
  if (!schema) {
    // The saves cannot be read to tell which hold those units: keep them all from being written.
    REXLOG_ERROR("units: no save schema ({}); saving is off for this session", error);
    g_changes->saving_blocked = true;
  } else {
    ProtectFromUnitsNotLoaded(g_assumed.user_data_root, TitleFolder(), *schema, missing,
                              std::chrono::system_clock::now(),
                              [](const std::string& line) { REXLOG_INFO("{}", line); }, *g_changes);
  }
  if (g_changes->saving_blocked) {
    g_saving_blocked = true;
    REXLOG_ERROR("units: saves hold units the game did not load; NOTHING IS SAVED IN THIS SESSION (copy: {})",
                 g_changes->not_loaded_backup.empty() ? std::string("none") : g_changes->not_loaded_backup.string());
  }
}

bool SavingBlocked() { return g_saving_blocked; }

}  // namespace torchlight::mods
