#include "mods/save_units_install.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
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

struct Deferred {
  bool pending = false;
  std::filesystem::path user_data_root;
};
Deferred g_deferred;

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

}  // namespace

void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan) {
  g_changes.reset();
  g_deferred = {};
  if (user_data_root.empty()) return;
  if (plan && !ScanModUnitFiles(data_dir / "mods", *plan).empty()) {
    g_deferred = {true, user_data_root};
    REXLOG_INFO("units: mods bring units; saves checked against the index the game loads");
    return;
  }
  std::string error;
  const auto base = ReadPakUnitIndex(pak, &error);
  if (!base) REXLOG_WARN("units: the game's unit index not read ({})", error);
  Run(user_data_root, MakeKnownUnits(base, {}, true));
}

bool SavesAwaitLoadedIndex() { return g_deferred.pending; }

void CheckSavesAgainstLoadedIndex(const std::optional<UnitIndex>& loaded) {
  if (!g_deferred.pending) return;
  g_deferred.pending = false;
  Run(g_deferred.user_data_root, loaded ? KnownUnitsOfIndex(*loaded) : MakeKnownUnits(std::nullopt, {}, true));
}

}  // namespace torchlight::mods
