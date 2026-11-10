// The player's mods folder (docs/mods.md, section 8): PC's layout, one folder per mod with its
// mod.dat, and the list mods.dat with each mod's priority (PC's mod manager 0x5CE440: CHECKFORNEW
// at the top, then per mod DIRECTORY, PRIORITY and COMPRESSED; a negative priority disables a mod).
// No guest, OGRE or platform types: the app registers the plan with the guest.

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mods/dat_text.h"

namespace torchlight::mods {

inline constexpr const char* kDescriptorFile = "mod.dat";
inline constexpr const char* kListFile = "mods.dat";

// A mod folder: a subfolder of the mods folder. Its mod.dat (block MOD; NAME, AUTHOR and
// DESCRIPTION) is optional, as on PC: the descriptor loader 0x5CD150 checks that the file exists
// (0x5C5E00 @0x5CD310) and, when it does not, skips reading it (@0x5CD32E) and goes on with the
// mod. Mods packs ship folders without one (the Ultimate Torchlight Mod-Pack). A mod.dat that
// exists but does not read is still skipped.
struct ModFolder {
  std::string folder;  // the subfolder's name
  std::string name, author, description;
  uint64_t descriptor_digest = 0;  // of mod.dat's bytes (FNV-1a 64)
};

struct ScanResult {
  std::vector<ModFolder> mods;      // folder-name order (case-insensitive)
  std::vector<std::string> skipped; // "<folder>: why" for subfolders that are not mods
};

// A folder an import is writing (mods/mod_import.h), renamed to the mod's name once complete: one
// with this suffix is what an interrupted import left, never a mod.
inline constexpr const char* kImportPartialSuffix = ".partial";

// The old folder of a mod an import is replacing (ImportOptions::replace), until the new one is in
// place: one with this suffix is never a mod either (the next import puts it back or removes it).
inline constexpr const char* kImportReplacedSuffix = ".replaced";
// A new mod moved aside while a failed replace is undone; ends in kImportPartialSuffix, so it is
// skipped and cleared as any .partial.
inline constexpr const char* kImportUndoPartialSuffix = ".undo.partial";

// The mods in `mods_folder` (missing folder: none). Folders ending in kImportPartialSuffix or
// kImportReplacedSuffix are skipped.
ScanResult ScanModsFolder(const std::filesystem::path& mods_folder);

struct ModListEntry {
  std::string directory;  // as written in mods.dat (a folder name, or a path ending in one)
  int32_t priority = 0;
  bool compressed = false;
};

struct ModList {
  bool check_for_new = true;
  std::vector<ModListEntry> entries;
};

// mods.dat's content: CHECKFORNEW from the top block, an entry per block holding DIRECTORY.
std::optional<ModList> ParseModList(const std::vector<uint8_t>& bytes, std::string* error);
std::vector<uint8_t> WriteModList(const ModList& list);

// One mod to register with the guest.
struct PlannedMod {
  std::string folder;
  int32_t priority = 0;  // from mods.dat; < 0: registered, then disabled
};

struct ModPlan {
  // Registration order: enabled mods by priority, then the disabled ones. The guest gives each
  // registered mod the next priority (kAddMod), so this order is the load order; the disabled
  // ones get their negative priority written afterwards.
  std::vector<PlannedMod> mods;
  // Activation order (indices into mods, enabled mods only): mods.dat's order, new folders last,
  // whatever the priorities. PC activates each mod (its OGRE resource group) as it reads its
  // entry (Torchlight.exe 0x5ce440 -> 0x5cd150 -> 0x5cce30); a list PC wrote is in priority order.
  std::vector<size_t> activation;
  ModList list;               // mods.dat to keep: unknown folders dropped, new ones added
  bool list_changed = false;  // write it back
};

// What to register, from the folders found and mods.dat (none: an empty list with CHECKFORNEW).
// With CHECKFORNEW, folders not in the list are added after the others, in folder-name order, as
// PC does; entries whose folder is gone are dropped.
ModPlan PlanMods(const ScanResult& scan, const std::optional<ModList>& list);

// Whether the game gets a mod manager at all: only with at least one mod (enabled or not); without,
// the guest keeps the original game's state (no manager).
inline bool ModManagerNeeded(const ModPlan& plan) { return !plan.mods.empty(); }

// The set of mods in effect: one line per planned mod (folder, priority, mod.dat digest), in plan
// order. Empty when there are none. The save safety net compares it between starts.
std::string ModSetFingerprint(const ModPlan& plan, const ScanResult& scan);

// The names the game records in a character save for the active mods, in its order (docs/mods.md
// 7d): every registered mod, in plan order, disabled ones too (sub_8239E718 lists each CMod with
// +192 set, which kAddMod sets for every mod; it does not look at the priority), each by its
// mod.dat NAME, or by its folder without one (as the host names it, mods_install.cpp). Empty
// without mods.
std::vector<std::u16string> ActiveModNames(const ModPlan& plan, const ScanResult& scan);

}  // namespace torchlight::mods
