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
// anything (platform::RenameNoReplace): a mod of that name already there (in any case) fails the
// import before anything is written (kAlreadyInstalled), and any failure or cancel removes what
// this import wrote, so there is never half a mod (a pack is installed whole or not at all).
// With ImportOptions::replace (the player confirmed "Replace?"), a mod already there is replaced
// instead: once every new mod is complete in its .partial, each old folder is renamed to
// <its name>.replaced, the new one renamed into place, and only when all are in place are the old
// ones removed; a failure before that puts every old folder back, so the old mod stays intact.
// What an interrupted import leaves is cleared by RecoverInterruptedImports, at start-up and when an
// import starts. A failure while undoing a replace never loses the old mod: the new ones in place
// are first renamed aside (one rename each, so a mod's folder is never half removed), the old ones
// renamed back, and only then is anything deleted; an old one that cannot be put back is kept as
// <name>.replaced and reported (kCannotWrite). A pack whose import is killed mid-swap may come back
// mixed: the mods already swapped new, the others old (each one whole).
//
// Zip entries (and a folder's): absolute names, "..", drive letters, colons, the characters
// <>"|?*, a trailing dot or space, and Windows' device names (CON, NUL, AUX, PRN, COM1-9, LPT1-9,
// with an extension too), in any component, are refused on every platform (with '\' read as a
// separator, as Windows tools write it), and so are symbolic links, encrypted entries and
// compression methods miniz cannot read. Names are UTF-8 when the entry's UTF-8 flag (general
// purpose bit 11) is set; without it the zip format says CP437, which agrees with ASCII only, so a
// name with other bytes is refused (kNonAsciiName, the entry's name as detail) instead of guessing
// its code page: old Windows zip tools write names in the system's code page (CP1252, CP437...)
// without the flag; zipping it again with a tool that writes UTF-8 names imports it. A source
// file that is not a zip at all (no zip signature at its start: e.g. a .rar or .7z picked with
// "All files") is kNotAZip; a zip that cannot be read (truncated, damaged) is kUnreadable. The free
// space of the mods folder's disk is checked before writing (ImportSpaceNeeded plus
// ImportSpaceReserve; an import whose free space cannot be read is refused). A zip of more than
// kMaxImportEntries entries, or with a name longer than kMaxImportNameBytes, is refused before its
// entries are read (kUnreadable). Two mods of a pack, or two files of a mod, whose names differ only
// in case are refused (kUnsafeName): the guest's paths ignore case. Every .partial in the mods
// folder is removed when an import starts.
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
  kUnreadable,        // the source cannot be opened or read (a damaged or truncated zip)
  kUnsafeName,        // an entry or the mod's name could write outside its folder
  kUnsupportedEntry,  // a symbolic link, an encrypted entry or an unknown compression method
  kNonAsciiName,      // a zip entry name that is neither UTF-8 (flagged) nor ASCII
  kNotEnoughSpace,
  kCannotWrite,
  kNotAZip,           // the source file is not a zip (detail: its name)
};

struct ImportOptions {
  // Replace mods already installed under the same names (in any case) instead of failing with
  // kAlreadyInstalled; the player's confirmation, asked after a first import without it.
  bool replace = false;
  // Tests only: a failure made at a step when this returns true for it: "swap:<mod>" (before the
  // new mod is renamed into place), "undo:<mod>" (moving a new mod aside while undoing),
  // "restore:<old folder>" (putting an old mod back).
  std::function<bool(const std::string& step)> fault;
};

// What RecoverInterruptedImports did, for the log; `error` when something could not be cleared
// (then the rest is left for the next time).
struct ImportRecovery {
  std::vector<std::string> removed;   // .partial folders, and .replaced ones whose mod is there
  std::vector<std::string> restored;  // .replaced folders put back under their mod's name
  std::string error;
  bool ok() const { return error.empty(); }
};
// Clears what an interrupted import left in `mods_folder`: every .partial is removed; a
// <name>.replaced is removed when a mod of that name is there in any case (the replace had put
// the new one in place), and put back as <name> when none is (it was interrupted before). Called at
// start-up before the mods are scanned (game_menu/mods_install.cpp), so a mod is never missing
// from a run because a replace was interrupted, and when each import starts. Missing folder:
// nothing to do.
ImportRecovery RecoverInterruptedImports(const std::filesystem::path& mods_folder);

// The limits on a source: its entries, and an entry name's bytes.
inline constexpr size_t kMaxImportEntries = 200000;
inline constexpr size_t kMaxImportNameBytes = 1024;
// Listing polls the cancel every this many entries.
inline constexpr size_t kCancelPoll = 1024;
// The free space an import needs: each file rounded up to kImportBlockSize (an empty one or a
// folder takes a block; std::filesystem does not tell the disk's block size, 4 KiB is the common
// one), plus a reserve kept free for the saves and settings on the same disk: the larger of
// kImportReserveMin and 5 % of the disk.
inline constexpr uint64_t kImportBlockSize = 4096;
inline constexpr uint64_t kImportReserveMin = 256ull << 20;
uint64_t ImportSpaceNeeded(const std::vector<uint64_t>& file_sizes, uint64_t block_size);
uint64_t ImportSpaceReserve(uint64_t capacity);

struct ImportProgress {
  uint64_t files_done = 0, files_total = 0;
  uint64_t bytes_done = 0, bytes_total = 0;
};
// Called from the importing thread after each file and every chunk of a large one.
using ImportProgressCallback = std::function<void(const ImportProgress&)>;

struct ImportResult {
  ImportStatus status = ImportStatus::kNoModFound;
  // kInstalled: the mods installed, folder names; kAlreadyInstalled: the ones already there (as the
  // source names them).
  std::vector<std::string> mods;
  // kInstalled with ImportOptions::replace: the folders that were replaced (their old names).
  std::vector<std::string> replaced;
  // The entry, path or system error the status is about; empty when there is none.
  std::string detail;
  // Top-level entries of the source that are not part of any mod (left out).
  std::vector<std::string> skipped;
  // kNotEnoughSpace: what the import needs (the reserve included) and what the disk has free, in
  // bytes.
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
                       const std::atomic<bool>* cancel = nullptr, const ImportOptions& options = {});

}  // namespace torchlight::mods
