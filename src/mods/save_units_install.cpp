#include "mods/save_units_install.h"

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
};
Assumed g_assumed;

void Run(const std::filesystem::path& user_data_root, const KnownUnits& known) {
  std::string error;
  const auto schema = save_import::Schema::Embedded(error);
  if (!schema) {
    REXLOG_ERROR("units: no save schema ({}); saves not checked", error);
    return;
  }
  if (!known.complete) REXLOG_WARN("units: saves not changed: {}", known.incomplete_why);
  char title[16];
  std::snprintf(title, sizeof(title), "%08X", game_setup::kTitleId);
  SaveUnitsReport report = ProtectSaves(user_data_root, title, *schema, known, std::chrono::system_clock::now(),
                                        [](const std::string& line) { REXLOG_INFO("{}", line); });
  if (!report.changed.empty() || report.failed) g_changes = std::move(report);
}

// The units of the index the game is expected to load with the mods' units: the cached one when
// it exists (exactly what the game will load), otherwise the base plus the GUIDs the mods'
// definitions set (a unit the game then cannot load shows in the comparison after the load).
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
  const auto guids = ModUnitGuids(files, &why);
  return MakeKnownUnits(base, guids ? *guids : std::vector<int64_t>{}, guids.has_value(), why);
}

}  // namespace

void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan) {
  g_changes.reset();
  g_assumed = {};
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
  REXLOG_WARN("units: {} units the saves' check counted on are not in the index the game loaded{}; "
              "saved items of them may stop a character from loading (saves not changed again)",
              missing.size(), loaded ? "" : " (its content is not known)");
  for (size_t i = 0; i < missing.size() && i < 20; ++i) REXLOG_WARN("units:   GUID {}", missing[i]);
  if (!g_changes) g_changes.emplace();
  g_changes->units_not_loaded = missing.size();
}

}  // namespace torchlight::mods
