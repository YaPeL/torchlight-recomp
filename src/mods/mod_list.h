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

// A mod folder: a subfolder of the mods folder with a readable mod.dat (block MOD; NAME, AUTHOR
// and DESCRIPTION, PC 0x5CD150).
struct ModFolder {
  std::string folder;  // the subfolder's name
  std::string name, author, description;
  uint64_t descriptor_digest = 0;  // of mod.dat's bytes (FNV-1a 64)
};

struct ScanResult {
  std::vector<ModFolder> mods;      // folder-name order (case-insensitive)
  std::vector<std::string> skipped; // "<folder>: why" for subfolders that are not mods
};

// The mods in `mods_folder` (missing folder: none).
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

}  // namespace torchlight::mods
