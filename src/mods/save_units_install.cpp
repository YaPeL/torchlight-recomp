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
#include "mods/class_list.h"
#include "mods/mod_list.h"
#include "mods/save_units.h"
#include "mods/settled_saves.h"
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
// The saves settled at this start (settled_saves.h), forgotten when the check's assumptions prove
// wrong after the load.
std::filesystem::path g_settled_file;
using Skip = std::function<bool(const std::filesystem::path&)>;

std::string TitleFolder() {
  char title[16];
  std::snprintf(title, sizeof(title), "%08X", game_setup::kTitleId);
  return title;
}

void Run(const std::filesystem::path& user_data_root, const KnownUnits& known, const Skip& skip,
         const std::vector<std::u16string>& active_mods, SettledSaves& settled) {
  std::string error;
  const auto schema = save_import::Schema::Embedded(error);
  if (!schema) {
    REXLOG_ERROR("units: no save schema ({}); saves not checked", error);
    return;
  }
  if (!known.complete) REXLOG_WARN("units: saves not changed: {}", known.incomplete_why);
  SaveUnitsReport report = ProtectSaves(user_data_root, TitleFolder(), *schema, known, std::chrono::system_clock::now(),
                                        [](const std::string& line) { REXLOG_INFO("{}", line); }, skip,
                                        &active_mods);
  for (const auto& path : report.settled) settled.Settle(path);
  if (report.skipped) {
    REXLOG_INFO("units: {} save(s) unchanged since they were last checked, not read again", report.skipped);
  }
  if (!report.changed.empty() || report.failed || !report.other_mods.empty()) g_changes = std::move(report);
}

// The units of the index the game is expected to load with the mods' units: the cached one when
// it exists (exactly what the game will load), otherwise the base merged with the mods' units as
// the builder merges them (a unit the game then cannot load shows in the comparison after the
// load).
KnownUnits ExpectedUnits(const std::filesystem::path& cache_dir, const std::filesystem::path& pak,
                         const std::vector<ModUnitFile>& files, const std::optional<UnitIndex>& base) {
  if (const auto identity = PakIdentity(pak)) {
    std::string log, error;
    if (const auto path = FindCachedUnitIndex(cache_dir / "unitdata", UnitCacheKey(files, *identity), &log)) {
      std::ifstream in(*path, std::ios::binary);
      const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
      if (const auto cached = ParseUnitIndex(bytes, &error)) return KnownUnitsOfIndex(*cached);
    }
  }
  std::string why;
  return MakeExpectedUnits(base, ModUnitEntries(files, &why), why);
}

// The names a save's broken mod list may hold: the mods in the folder (folder and mod.dat NAME) and
// those mods.dat still lists (a mod since removed).
std::vector<std::u16string> RepairNames(const std::filesystem::path& data_dir) {
  const std::filesystem::path folder = data_dir / "mods";
  std::vector<std::u16string> candidates;
  auto add = [&](const std::string& name) {
    if (!name.empty()) candidates.push_back(Utf8ToUtf16(name));
  };
  for (const ModFolder& mod : ScanModsFolder(folder).mods) {
    add(mod.folder);
    add(mod.name);
  }
  std::ifstream in(folder / "mods.dat", std::ios::binary);
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
  if (const auto list = ParseModList(bytes, nullptr)) {
    for (const ModListEntry& entry : list->entries) {
      std::string directory = entry.directory;
      while (!directory.empty() && (directory.back() == '/' || directory.back() == '\\')) directory.pop_back();
      add(directory.substr(directory.find_last_of("/\\") + 1));
    }
  }
  return candidates;
}

// Saves written with the Xbox writer's mod list bug (docs/mods.md 7d), from before the repair while
// writing: repaired before anything reads them, with `candidates` (RepairNames).
void RepairModLists(const std::filesystem::path& user_data_root, const std::vector<std::u16string>& candidates,
                    const Skip& skip) {
  if (candidates.empty()) return;
  std::string error;
  const auto schema = save_import::Schema::Embedded(error);
  if (!schema) return;
  const ModListRepairReport report =
      RepairModListSaves(user_data_root, TitleFolder(), *schema, candidates, std::chrono::system_clock::now(),
                         [](const std::string& line) { REXLOG_INFO("{}", line); }, skip);
  if (!report.repaired.empty()) {
    REXLOG_WARN("mods: {} save(s) written by the Xbox mod list bug repaired (copy: {})", report.repaired.size(),
                report.backup.string());
  }
}

// The check InstallSaveUnits left for after the game's unit index build (CheckDeferredSaves).
struct Deferred {
  bool pending = false;
  std::filesystem::path data_dir, cache_dir, pak, user_data_root;
  std::vector<ModUnitFile> files;
  std::optional<ModPlan> plan;
};
Deferred g_deferred;
// g_deferred.pending, for SavingBlocked on any thread: saving stays off while the saves are not
// checked, so a session whose check never ran writes nothing unchecked.
std::atomic<bool> g_check_pending{false};

void CheckSaves(const std::filesystem::path& data_dir, const std::vector<ModUnitFile>& files,
                const std::filesystem::path& cache_dir, const std::filesystem::path& pak,
                const std::filesystem::path& user_data_root, const ModPlan* plan) {
  using Clock = std::chrono::steady_clock;
  const auto ms = [](Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
  };
  const auto t0 = Clock::now();
  std::string error;
  const auto base = ReadPakUnitIndex(pak, &error);
  if (!base) REXLOG_WARN("units: the game's unit index not read ({})", error);
  const auto t2 = Clock::now();
  KnownUnits known;
  if (files.empty()) {
    known = MakeKnownUnits(base, {}, true);
  } else {
    g_assumed.pending = true;
    g_assumed.user_data_root = user_data_root;
    g_assumed.known = ExpectedUnits(cache_dir, pak, files, base);
    if (base) g_assumed.base_guids = KnownUnitsOfIndex(*base).guids;
    REXLOG_INFO("units: mods bring units; saves checked against the {} units the game is expected to load",
                g_assumed.known.guids.size());
    known = g_assumed.known;
  }
  const auto t3 = Clock::now();
  // Saves settled at the last start under the same context and unchanged since are not read again;
  // those still settled, and those found settled now, are remembered for the next start.
  const std::vector<std::u16string> names = RepairNames(data_dir);
  // The names the game records for the active mods: a character last saved with others is told.
  const std::vector<std::u16string> active =
      plan ? ActiveModNames(*plan, ScanModsFolder(data_dir / "mods")) : std::vector<std::u16string>{};
  const std::string context = SettledSaves::Context(known, names, active);
  g_settled_file = user_data_root / "cache" / "settled-saves.txt";
  const SettledSaves last = SettledSaves::Load(g_settled_file, user_data_root, context);
  SettledSaves next(user_data_root, context);
  const Skip skip = [&](const std::filesystem::path& path) {
    if (!last.Unchanged(path)) return false;
    next.Settle(path);
    return true;
  };
  RepairModLists(user_data_root, names, skip);
  Run(user_data_root, known, skip, active, next);
  if (!next.Store(g_settled_file)) REXLOG_WARN("units: cannot write {}", g_settled_file.string());
  REXLOG_INFO("units: game's index {} ms, expected units {} ms, saves {} ms", ms(t0, t2), ms(t2, t3),
              ms(t3, Clock::now()));
}

}  // namespace

void InstallSaveUnits(const std::filesystem::path& data_dir, const std::vector<ModUnitFile>& files,
                      const std::filesystem::path& cache_dir, const std::filesystem::path& pak,
                      const std::filesystem::path& user_data_root, const ModPlan* plan, bool index_built_in_game) {
  g_changes.reset();
  g_assumed = {};
  g_saving_blocked = false;
  g_settled_file.clear();
  g_deferred = {};
  g_check_pending = false;
  if (user_data_root.empty()) return;
  if (!files.empty() && index_built_in_game) {
    g_deferred = {true, data_dir, cache_dir, pak, user_data_root, files,
                  plan ? std::optional<ModPlan>(*plan) : std::nullopt};
    g_check_pending = true;
    REXLOG_INFO("units: the saves are checked once the game has built the mods' unit index");
    return;
  }
  CheckSaves(data_dir, files, cache_dir, pak, user_data_root, plan);
}

void CheckDeferredSaves() {
  if (!g_deferred.pending) return;
  g_deferred.pending = false;
  const Deferred d = std::move(g_deferred);
  g_deferred = {};
  CheckSaves(d.data_dir, d.files, d.cache_dir, d.pak, d.user_data_root, d.plan ? &*d.plan : nullptr);
  g_check_pending = false;
}

bool SaveCheckPending() { return g_check_pending; }


void CompareWithLoadedUnits(const std::optional<std::unordered_set<int64_t>>& loaded) {
  if (!g_assumed.pending) return;
  g_assumed.pending = false;
  // Not known what the game holds: every mods' unit the check counted on may be missing.
  const std::vector<int64_t> missing = UnitsNotLoaded(g_assumed.known, loaded ? *loaded : g_assumed.base_guids);
  if (missing.empty()) return;
  // The saves were settled against units the game does not hold: check them all at the next start.
  std::error_code ec;
  if (!g_settled_file.empty()) std::filesystem::remove(g_settled_file, ec);
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

bool SavingBlocked() { return g_saving_blocked || g_check_pending; }

}  // namespace torchlight::mods
