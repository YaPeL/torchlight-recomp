// Importing a mod the player picked (a .zip file or a folder) into the mods folder (docs/mods.md,
// section 8c): the source is extracted or copied into <data>/mods/<name>/, PC's folder layout,
// which the next start reads as any mod copied in by hand. Nothing is mounted from the zip.
//
// What the source may hold, decided from its entries' paths:
// - mod.dat or media/ at its root: one mod, named after the source (the zip's name without .zip,
//   or the folder's name);
// - otherwise, top-level folders holding mod.dat or media/: each of them is a mod, named after
//   its folder (one: a single mod; several: a pack). Other top-level files and folders are left
//   out (listed in ImportResult::skipped);
// - otherwise, when the root holds a single folder, the same one level down (a zip of a mods
//   folder);
// - otherwise: no mod found.
// A mods.dat in the source is never copied (the player's own list decides the priorities; the next
// start adds the new mods at the end of it).
//
// Each mod is written to <data>/mods/<name>.partial first and renamed to <name> without replacing
// anything (platform::RenameNoReplace): a mod of that name already there fails the import before
// anything is written, and any failure or cancel removes what this import wrote, so there is never
// half a mod (a pack is installed whole or not at all).
//
// Zip entries: absolute names, "..", drive letters and colons (with '\' read as a separator, as
// Windows tools write it) are refused, and so are symbolic links, encrypted entries and
// compression methods miniz cannot read. Names are UTF-8 when the entry's UTF-8 flag (general
// purpose bit 11) is set; without it the zip format says CP437, which agrees with ASCII only, so a
// name with other bytes is refused (kNonAsciiName) instead of guessing its code page. The free
// space of the mods folder's disk is checked against the files' uncompressed size before writing.
// No guest, OGRE or SDL: platform code only through platform::RenameNoReplace.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace torchlight::mods {

enum class ImportStatus {
  kInstalled,
  kCancelled,
  kNoModFound,
  kAlreadyInstalled,  // `mods` holds the names that exist
  kUnreadable,        // the source cannot be opened or read (not a zip, damaged, truncated)
  kUnsafeName,        // an entry or the mod's name could write outside its folder
  kUnsupportedEntry,  // a symbolic link, an encrypted entry or an unknown compression method
  kNonAsciiName,      // a zip entry name that is neither UTF-8 (flagged) nor ASCII
  kNotEnoughSpace,
  kCannotWrite,
};

struct ImportProgress {
  uint64_t files_done = 0, files_total = 0;
  uint64_t bytes_done = 0, bytes_total = 0;
};
// Called from the importing thread after each file and every chunk of a large one.
using ImportProgressCallback = std::function<void(const ImportProgress&)>;

struct ImportResult {
  ImportStatus status = ImportStatus::kNoModFound;
  // kInstalled: the mods installed, folder names; kAlreadyInstalled: the ones already there.
  std::vector<std::string> mods;
  // The entry, path or system error the status is about; empty when there is none.
  std::string detail;
  // Top-level entries of the source that are not part of any mod (left out).
  std::vector<std::string> skipped;
  // kNotEnoughSpace: what the files need and what the disk has free, in bytes.
  uint64_t bytes_needed = 0, bytes_free = 0;

  bool ok() const { return status == ImportStatus::kInstalled; }
  // One line in English for the log.
  std::string message() const;
};

// Imports `source` (a regular file is read as a zip, a directory is copied) into
// `data_dir`/mods/. `cancel`, when given, is polled between chunks; once true the import stops
// and leaves nothing behind (kCancelled).
ImportResult ImportMod(const std::filesystem::path& source, const std::filesystem::path& data_dir,
                       const ImportProgressCallback& progress = {},
                       const std::atomic<bool>* cancel = nullptr);

}  // namespace torchlight::mods
