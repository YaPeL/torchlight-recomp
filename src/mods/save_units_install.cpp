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

void InstallSaveUnits(const std::filesystem::path& data_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan) {
  g_changes.reset();
  if (user_data_root.empty()) return;
  std::string error;
  const auto schema = save_import::Schema::Embedded(error);
  if (!schema) {
    REXLOG_ERROR("units: no save schema ({}); saves not checked", error);
    return;
  }

  // The units the game will know: the base index, and the mods' (the cached merged index when it
  // is valid, otherwise the GUIDs their definitions set).
  const auto base = ReadPakUnitIndex(pak, &error);
  if (!base) REXLOG_WARN("units: the game's unit index not read ({})", error);
  std::vector<int64_t> mod_guids;
  bool mods_complete = true;
  std::string mods_why;
  if (plan) {
    const auto files = ScanModUnitFiles(data_dir / "mods", *plan);
    const auto identity = PakIdentity(pak);
    std::optional<UnitIndex> cached;
    if (!files.empty() && identity) {
      std::string log;
      if (const auto path = FindCachedUnitIndex(data_dir / "cache" / "unitdata", UnitCacheKey(files, *identity), &log)) {
        std::ifstream in(*path, std::ios::binary);
        const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
        cached = ParseUnitIndex(bytes, &error);
      }
    }
    if (cached) {
      for (const auto& group : cached->groups) {
        for (const UnitEntry& e : group) mod_guids.push_back(e.guid);
      }
    } else if (const auto guids = ModUnitGuids(files, &mods_why)) {
      mod_guids = *guids;
    } else {
      mods_complete = false;
    }
  }
  const KnownUnits known = MakeKnownUnits(base, mod_guids, mods_complete, mods_why);
  if (!known.complete) REXLOG_WARN("units: saves not changed: {}", known.incomplete_why);

  char title[16];
  std::snprintf(title, sizeof(title), "%08X", game_setup::kTitleId);
  SaveUnitsReport report = ProtectSaves(user_data_root, title, *schema, known, std::chrono::system_clock::now(),
                                        [](const std::string& line) { REXLOG_INFO("{}", line); });
  if (!report.changed.empty() || report.failed) g_changes = std::move(report);
}

}  // namespace torchlight::mods
