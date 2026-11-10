#include "mods/mod_import.h"

#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <system_error>

#include "mods/mod_list.h"
#include "platform/user_folders.h"

namespace torchlight::mods {

namespace {

namespace fs = std::filesystem;

constexpr size_t kChunk = 1 << 20;

// One file or folder of the source, by its path's components (UTF-8).
struct Entry {
  std::vector<std::string> parts;
  bool directory = false;
  uint64_t size = 0;
  uint32_t zip_index = 0;  // zip source
  fs::path file;           // folder source
};

struct Failure {
  ImportStatus status;
  std::string detail;
};

// A mod found in the source: the folder name it gets and the components its files are below.
struct FoundMod {
  std::string name;
  std::vector<std::string> prefix;
};

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

bool EqualNoCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return Lower(x) == Lower(y);
         });
}

bool EndsWithNoCase(std::string_view text, std::string_view suffix) {
  return text.size() > suffix.size() && EqualNoCase(text.substr(text.size() - suffix.size()), suffix);
}

bool IsControl(char c) { return static_cast<unsigned char>(c) < 0x20; }

bool IsAscii(std::string_view text) {
  return std::all_of(text.begin(), text.end(),
                     [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

// Well-formed UTF-8 (no overlong forms, surrogates or values past U+10FFFF).
bool IsUtf8(std::string_view text) {
  for (size_t i = 0; i < text.size();) {
    const unsigned char lead = static_cast<unsigned char>(text[i]);
    size_t length = 1;
    uint32_t code = lead;
    if (lead >= 0x80) {
      if (lead < 0xC2 || lead > 0xF4) return false;
      length = lead < 0xE0 ? 2 : lead < 0xF0 ? 3 : 4;
      if (i + length > text.size()) return false;
      code = lead & (0xFF >> (length + 1));
      for (size_t k = 1; k < length; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) return false;
        code = (code << 6) | (next & 0x3F);
      }
      const uint32_t smallest[] = {0, 0, 0x80, 0x800, 0x10000};
      if (code < smallest[length] || (code >= 0xD800 && code < 0xE000) || code > 0x10FFFF) {
        return false;
      }
    }
    i += length;
  }
  return true;
}

std::string Utf8Of(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

fs::path PathOf(std::string_view utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }

// A name that is one plain path component on every platform the game runs on: no separators,
// colons (drive letters, NTFS streams), control characters or the other characters Windows refuses
// (<>"|?*), not "." or "..", no trailing dot or space (Windows drops them, so two names would
// meet), and not a device name Windows keeps in every folder (CON, NUL, AUX, PRN, COM1-9, LPT1-9,
// in any case, with an extension too: "Mod/media/COM1" would open a serial port, "NUL" would drop
// the data).
bool IsPlainComponent(std::string_view name) {
  if (name.empty() || name == "." || name == "..") return false;
  if (name.back() == '.' || name.back() == ' ') return false;
  if (std::any_of(name.begin(), name.end(), [](char c) {
        return c == '/' || c == '\\' || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' ||
               c == '?' || c == '*' || IsControl(c);
      })) {
    return false;
  }
  std::string_view base = name.substr(0, name.find('.'));
  while (!base.empty() && base.back() == ' ') base.remove_suffix(1);
  for (const char* device : {"CON", "NUL", "AUX", "PRN"}) {
    if (EqualNoCase(base, device)) return false;
  }
  if (base.size() == 4 && (EqualNoCase(base.substr(0, 3), "COM") || EqualNoCase(base.substr(0, 3), "LPT")) &&
      base[3] >= '1' && base[3] <= '9') {
    return false;
  }
  return true;
}

// A zip entry's name as components, '\' read as a separator; nothing when it is absolute, has a
// drive letter, or a component that is not plain (IsPlainComponent: "..", a colon, a device
// name...), since it could write outside the mod's folder or not where it says. Empty and "."
// components are dropped.
std::optional<std::vector<std::string>> SplitEntryName(std::string name) {
  std::replace(name.begin(), name.end(), '\\', '/');
  if (name.empty() || name.front() == '/') return std::nullopt;
  std::vector<std::string> parts;
  size_t start = 0;
  while (start <= name.size()) {
    size_t end = name.find('/', start);
    if (end == std::string::npos) end = name.size();
    const std::string part = name.substr(start, end - start);
    start = end + 1;
    if (part.empty() || part == ".") continue;
    if (!IsPlainComponent(part)) return std::nullopt;
    parts.push_back(part);
  }
  if (parts.empty()) return std::nullopt;
  return parts;
}

// The folders a mod can be at (the root, a top-level folder, one below a single top-level
// folder), built in one pass over the entries: for each, the names right below it in first-seen
// order with whether each is a folder, and whether it is a mod (a mod.dat file or a media folder
// right in it). Keyed by the folder's components joined with '/' (none holds one).
struct Folder {
  std::vector<std::pair<std::string, bool>> children;
  std::unordered_map<std::string, size_t> index;  // child name -> children[i]
  bool mod = false;
};
using Folders = std::unordered_map<std::string, Folder>;

std::string KeyOf(const std::vector<std::string>& parts, size_t count) {
  std::string key;
  for (size_t i = 0; i < count; ++i) key += (i ? "/" : "") + parts[i];
  return key;
}

Folders FoldersOf(const std::vector<Entry>& entries) {
  Folders folders;
  for (const Entry& entry : entries) {
    for (size_t depth = 0; depth < 3 && depth < entry.parts.size(); ++depth) {
      Folder& folder = folders[KeyOf(entry.parts, depth)];
      const std::string& name = entry.parts[depth];
      const bool last = entry.parts.size() == depth + 1;
      const bool is_folder = entry.directory || !last;
      const auto [it, added] = folder.index.try_emplace(name, folder.children.size());
      if (added) folder.children.emplace_back(name, is_folder);
      else folder.children[it->second].second = folder.children[it->second].second || is_folder;
      if (last && !entry.directory && EqualNoCase(name, kDescriptorFile)) folder.mod = true;
      if (EqualNoCase(name, "media") && is_folder) folder.mod = true;
    }
  }
  return folders;
}

const Folder* FolderAt(const Folders& folders, const std::vector<std::string>& prefix) {
  const auto it = folders.find(KeyOf(prefix, prefix.size()));
  return it == folders.end() ? nullptr : &it->second;
}

// The mod folders right below `prefix`; the other children go to `skipped`.
std::vector<FoundMod> ModFoldersAt(const Folders& folders, const std::vector<std::string>& prefix,
                                   std::vector<std::string>& skipped) {
  std::vector<FoundMod> mods;
  std::vector<std::string> others;
  const Folder* at = FolderAt(folders, prefix);
  if (!at) return mods;
  for (const auto& [name, folder] : at->children) {
    std::vector<std::string> child = prefix;
    child.push_back(name);
    const Folder* inside = folder ? FolderAt(folders, child) : nullptr;
    if (inside && inside->mod) mods.push_back({name, child});
    else others.push_back(name);
  }
  if (!mods.empty()) skipped.insert(skipped.end(), others.begin(), others.end());
  return mods;
}

std::vector<FoundMod> FindMods(const std::vector<Entry>& entries, const std::string& root_name,
                               std::vector<std::string>& skipped) {
  const Folders folders = FoldersOf(entries);
  const Folder* root = FolderAt(folders, {});
  if (!root) return {};
  if (root->mod) return {{root_name, {}}};
  auto mods = ModFoldersAt(folders, {}, skipped);
  if (!mods.empty()) return mods;
  if (root->children.size() == 1 && root->children.front().second) {
    return ModFoldersAt(folders, {root->children.front().first}, skipped);
  }
  return {};
}

// ---- the zip --------------------------------------------------------------------------------

// A zip starts with a local file header (PK\3\4), or an end of central directory when it is
// empty (PK\5\6); anything else is not a zip (a .rar, a .7z, a text file).
bool LooksLikeZip(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  char magic[4] = {};
  in.read(magic, sizeof magic);
  return in.gcount() == 4 && (std::memcmp(magic, "PK\3\4", 4) == 0 || std::memcmp(magic, "PK\5\6", 4) == 0);
}

size_t ReadAt(void* opaque, mz_uint64 offset, void* buffer, size_t size) {
  auto& file = *static_cast<std::ifstream*>(opaque);
  file.clear();
  file.seekg(static_cast<std::streamoff>(offset));
  file.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
  return static_cast<size_t>(file.gcount());
}

class Zip {
 public:
  ~Zip() {
    if (open_) mz_zip_reader_end(&zip_);
  }
  bool Open(const fs::path& path, std::string& error) {
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    file_.open(path, std::ios::binary);
    if (ec || !file_) {
      error = ec ? ec.message() : "cannot open the file";
      return false;
    }
    mz_zip_zero_struct(&zip_);
    zip_.m_pRead = ReadAt;
    zip_.m_pIO_opaque = &file_;
    if (!mz_zip_reader_init(&zip_, size, 0)) {
      error = mz_zip_get_error_string(mz_zip_get_last_error(&zip_));
      return false;
    }
    open_ = true;
    return true;
  }
  mz_zip_archive* get() { return &zip_; }

 private:
  std::ifstream file_;
  mz_zip_archive zip_{};
  bool open_ = false;
};

bool Cancelled(const std::atomic<bool>* cancel) { return cancel && cancel->load(); }

// The zip's entries, every one checked; a failure on the first that cannot be taken. `cancel` is
// polled every kCancelPoll entries.
std::optional<Failure> ListZip(Zip& zip, std::vector<Entry>& entries, const std::atomic<bool>* cancel) {
  mz_zip_archive* archive = zip.get();
  const mz_uint count = mz_zip_reader_get_num_files(archive);
  if (count > kMaxImportEntries) {
    return Failure{ImportStatus::kUnreadable, std::to_string(count) + " entries, more than " +
                                                  std::to_string(kMaxImportEntries)};
  }
  for (mz_uint i = 0; i < count; ++i) {
    if (i % kCancelPoll == 0 && Cancelled(cancel)) return Failure{ImportStatus::kCancelled, ""};
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(archive, i, &stat)) {
      return Failure{ImportStatus::kUnreadable,
                     mz_zip_get_error_string(mz_zip_get_last_error(archive))};
    }
    // The whole name (the stat's copy stops at 512 bytes).
    const mz_uint length = mz_zip_reader_get_filename(archive, i, nullptr, 0);
    if (length > kMaxImportNameBytes + 1) {
      return Failure{ImportStatus::kUnreadable, "an entry name longer than " +
                                                    std::to_string(kMaxImportNameBytes) + " bytes"};
    }
    std::string name(length, '\0');
    mz_zip_reader_get_filename(archive, i, name.data(), static_cast<mz_uint>(name.size()));
    name.resize(name.find('\0') == std::string::npos ? name.size() : name.find('\0'));
    const bool utf8 = (stat.m_bit_flag & (1u << 11)) != 0;
    if (utf8 ? !IsUtf8(name) : !IsAscii(name)) return Failure{ImportStatus::kNonAsciiName, name};
    auto parts = SplitEntryName(name);
    if (!parts) return Failure{ImportStatus::kUnsafeName, name};
    // A Unix mode in the high half of the external attributes: S_IFLNK is a symbolic link.
    if (((stat.m_external_attr >> 16) & 0xF000) == 0xA000) {
      return Failure{ImportStatus::kUnsupportedEntry, name + " (symbolic link)"};
    }
    if (stat.m_is_encrypted) return Failure{ImportStatus::kUnsupportedEntry, name + " (encrypted)"};
    if (!stat.m_is_directory && !stat.m_is_supported) {
      return Failure{ImportStatus::kUnsupportedEntry, name + " (compression method)"};
    }
    Entry entry;
    entry.parts = std::move(*parts);
    entry.directory = stat.m_is_directory;
    entry.size = stat.m_is_directory ? 0 : stat.m_uncomp_size;
    entry.zip_index = i;
    entries.push_back(std::move(entry));
  }
  return std::nullopt;
}

// ---- the folder ---------------------------------------------------------------------------

std::optional<Failure> ListFolder(const fs::path& folder, std::vector<Entry>& entries,
                                  const std::atomic<bool>* cancel) {
  std::error_code ec;
  fs::recursive_directory_iterator it(folder, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    if (entries.size() % kCancelPoll == 0 && Cancelled(cancel)) return Failure{ImportStatus::kCancelled, ""};
    if (entries.size() >= kMaxImportEntries) {
      return Failure{ImportStatus::kUnreadable, "more than " + std::to_string(kMaxImportEntries) + " entries"};
    }
    const fs::directory_entry& item = *it;
    const fs::path relative = item.path().lexically_relative(folder);
    if (item.is_symlink(ec)) {
      return Failure{ImportStatus::kUnsupportedEntry, Utf8Of(relative) + " (symbolic link)"};
    }
    Entry entry;
    for (const fs::path& part : relative) {
      entry.parts.push_back(Utf8Of(part));
      if (!IsPlainComponent(entry.parts.back())) return Failure{ImportStatus::kUnsafeName, Utf8Of(relative)};
    }
    entry.file = item.path();
    if (item.is_directory(ec)) {
      entry.directory = true;
    } else if (item.is_regular_file(ec)) {
      entry.size = item.file_size(ec);
    } else {
      return Failure{ImportStatus::kUnsupportedEntry, Utf8Of(relative) + " (not a regular file)"};
    }
    if (ec) break;
    entries.push_back(std::move(entry));
  }
  if (ec) return Failure{ImportStatus::kUnreadable, Utf8Of(folder) + ": " + ec.message()};
  return std::nullopt;
}

// ---- writing --------------------------------------------------------------------------------

struct Writer {
  const ImportProgressCallback& progress;
  const std::atomic<bool>* cancel;
  ImportProgress done;
  bool cancelled = false;

  bool Stop() {
    if (cancel && cancel->load()) cancelled = true;
    return cancelled;
  }
  void Report() {
    if (progress) progress(done);
  }
};

// What the zip extraction writes into, through miniz's callback.
struct ZipSink {
  std::ofstream* out;
  Writer* writer;
  uint64_t expected, written = 0;
  bool write_failed = false;
};

size_t WriteChunk(void* opaque, mz_uint64, const void* data, size_t size) {
  auto& sink = *static_cast<ZipSink*>(opaque);
  if (sink.writer->Stop()) return 0;
  // Never more than the entry says it holds: the space was checked against that.
  if (sink.written + size > sink.expected) return 0;
  sink.out->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  if (!*sink.out) {
    sink.write_failed = true;
    return 0;
  }
  sink.written += size;
  sink.writer->done.bytes_done += size;
  sink.writer->Report();
  return size;
}

std::optional<Failure> WriteFile(const Entry& entry, Zip* zip, const fs::path& target,
                                 Writer& writer) {
  std::error_code ec;
  fs::create_directories(target.parent_path(), ec);
  std::ofstream out(target, std::ios::binary | std::ios::trunc);
  if (ec || !out) return Failure{ImportStatus::kCannotWrite, Utf8Of(target)};
  if (zip) {
    ZipSink sink{&out, &writer, entry.size};
    const bool extracted =
        mz_zip_reader_extract_to_callback(zip->get(), entry.zip_index, WriteChunk, &sink, 0);
    if (writer.cancelled) return Failure{ImportStatus::kCancelled, ""};
    if (sink.write_failed) return Failure{ImportStatus::kCannotWrite, Utf8Of(target)};
    if (!extracted || sink.written != entry.size) {
      std::string name;
      for (const std::string& part : entry.parts) name += (name.empty() ? "" : "/") + part;
      return Failure{ImportStatus::kUnreadable,
                     name + ": " + mz_zip_get_error_string(mz_zip_get_last_error(zip->get()))};
    }
  } else {
    std::ifstream in(entry.file, std::ios::binary);
    if (!in) return Failure{ImportStatus::kUnreadable, Utf8Of(entry.file)};
    std::vector<char> buffer(kChunk);
    while (in) {
      if (writer.Stop()) return Failure{ImportStatus::kCancelled, ""};
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      const std::streamsize got = in.gcount();
      if (got <= 0) break;
      out.write(buffer.data(), got);
      if (!out) return Failure{ImportStatus::kCannotWrite, Utf8Of(target)};
      writer.done.bytes_done += static_cast<uint64_t>(got);
      writer.Report();
    }
    if (in.bad()) return Failure{ImportStatus::kUnreadable, Utf8Of(entry.file)};
  }
  out.close();
  if (!out) return Failure{ImportStatus::kCannotWrite, Utf8Of(target)};
  ++writer.done.files_done;
  writer.Report();
  return std::nullopt;
}

// The entries of each mod to write and where below its folder, in one pass (the mods' prefixes
// all have one length); a mods.dat at a mod's top is left out.
std::vector<std::vector<std::pair<const Entry*, fs::path>>> FilesOf(const std::vector<Entry>& entries,
                                                                    const std::vector<FoundMod>& mods) {
  std::vector<std::vector<std::pair<const Entry*, fs::path>>> files(mods.size());
  if (mods.empty()) return files;
  const size_t depth = mods.front().prefix.size();
  std::unordered_map<std::string, size_t> by_prefix;
  for (size_t m = 0; m < mods.size(); ++m) by_prefix[KeyOf(mods[m].prefix, depth)] = m;
  for (const Entry& entry : entries) {
    if (entry.parts.size() <= depth) continue;
    const auto it = by_prefix.find(KeyOf(entry.parts, depth));
    if (it == by_prefix.end()) continue;
    if (entry.parts.size() == depth + 1 && !entry.directory && EqualNoCase(entry.parts.back(), kListFile)) continue;
    fs::path relative;
    for (size_t i = depth; i < entry.parts.size(); ++i) relative /= PathOf(entry.parts[i]);
    files[it->second].emplace_back(&entry, relative);
  }
  return files;
}

// `text` with ASCII letters lowered: the guest's paths ignore their case.
std::string Folded(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), Lower);
  return text;
}

std::string Megabytes(uint64_t bytes) {
  return std::to_string((bytes + (1 << 20) - 1) >> 20) + " MB";
}

}  // namespace

uint64_t ImportSpaceNeeded(const std::vector<uint64_t>& file_sizes, uint64_t block_size) {
  // Saturating: a zip64 entry may declare an absurd size, which must not wrap to a small total.
  constexpr uint64_t kMax = std::numeric_limits<uint64_t>::max();
  uint64_t total = 0;
  for (const uint64_t size : file_sizes) {
    const uint64_t blocks = size == 0 ? 1 : size / block_size + (size % block_size != 0);
    const uint64_t bytes = blocks > kMax / block_size ? kMax : blocks * block_size;
    total = bytes > kMax - total ? kMax : total + bytes;
  }
  return total;
}

uint64_t ImportSpaceReserve(uint64_t capacity) { return std::max<uint64_t>(kImportReserveMin, capacity / 20); }

std::string ImportResult::message() const {
  std::string names;
  for (const std::string& mod : mods) names += (names.empty() ? "" : ", ") + mod;
  const std::string about = detail.empty() ? "" : ": " + detail;
  switch (status) {
    case ImportStatus::kInstalled: {
      std::string replaced;
      for (const std::string& mod : this->replaced) replaced += (replaced.empty() ? "" : ", ") + mod;
      return "installed " + names + (replaced.empty() ? "" : " (replacing " + replaced + ")");
    }
    case ImportStatus::kCancelled: return "cancelled; nothing installed";
    case ImportStatus::kNoModFound: return "no mod found (no mod.dat or media folder)";
    case ImportStatus::kAlreadyInstalled: return "already installed: " + names;
    case ImportStatus::kUnreadable: return "cannot read the source" + about;
    case ImportStatus::kNotAZip: return "not a zip file" + about;
    case ImportStatus::kUnsafeName: return "unsafe name" + about;
    case ImportStatus::kUnsupportedEntry: return "unsupported entry" + about;
    case ImportStatus::kNonAsciiName: return "entry name not in UTF-8 or ASCII" + about;
    case ImportStatus::kNotEnoughSpace: return "not enough free space" + about;
    case ImportStatus::kCannotWrite: return "cannot write" + about;
  }
  return "";
}

ImportRecovery RecoverInterruptedImports(const std::filesystem::path& mods_folder) {
  ImportRecovery out;
  std::error_code ec;
  if (!fs::is_directory(mods_folder, ec)) return out;  // no mods folder: nothing was interrupted
  std::vector<fs::path> partials, replaced;
  std::unordered_set<std::string> present;  // the mods there, folded (the guest ignores case)
  {
    fs::directory_iterator it(mods_folder, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      const std::string name = Utf8Of(it->path().filename());
      if (EndsWithNoCase(name, kImportPartialSuffix)) partials.push_back(it->path());
      else if (EndsWithNoCase(name, kImportReplacedSuffix)) replaced.push_back(it->path());
      else present.insert(Folded(name));
    }
  }
  if (ec) {
    out.error = Utf8Of(mods_folder) + ": " + ec.message();
    return out;
  }
  // Each step goes on after an error (the errors collected): a locked .partial must not keep the old
  // mods from being put back. The .replaced first: they may be the only copy of a mod.
  const auto note = [&](const std::string& error) { out.error += (out.error.empty() ? "" : "; ") + error; };
  // A .replaced whose mod is there (in any case: never two copies of one mod) is removed, the
  // replace having put the new one in place; one whose mod is missing is put back.
  for (const fs::path& path : replaced) {
    const std::string name = Utf8Of(path.filename());
    const std::string original = name.substr(0, name.size() - std::strlen(kImportReplacedSuffix));
    if (present.contains(Folded(original))) {
      fs::remove_all(path, ec);
      if (ec) {
        note("cannot remove " + Utf8Of(path) + ": " + ec.message());
        continue;
      }
      out.removed.push_back(name);
    } else {
      std::string error;
      if (!platform::RenameNoReplace(path, mods_folder / PathOf(original), error)) {
        note("cannot put " + Utf8Of(path) + " back: " + error);
        continue;
      }
      present.insert(Folded(original));
      out.restored.push_back(original);
    }
  }
  // A leftover .partial that cannot be removed would mix its files into the next import's.
  for (const fs::path& path : partials) {
    fs::remove_all(path, ec);
    if (ec) {
      note("cannot remove " + Utf8Of(path) + ": " + ec.message());
      continue;
    }
    out.removed.push_back(Utf8Of(path.filename()));
  }
  return out;
}

ImportResult ImportMod(const std::filesystem::path& source, const std::filesystem::path& data_dir,
                       const ImportProgressCallback& progress, const std::atomic<bool>* cancel,
                       const ImportOptions& options) {
  ImportResult result;
  bool old_lost = false;  // a replace's undo could not put an old mod back
  auto fail = [&](Failure failure) {
    result.status = failure.status;
    result.detail = std::move(failure.detail);
    result.old_mods_kept = options.replace && !old_lost;
    return result;
  };
  std::error_code ec;
  fs::path from = source;
  if (!from.has_filename()) from = from.parent_path();  // "…/MyMod/" -> "…/MyMod"

  // The entries, all checked before anything is written.
  std::vector<Entry> entries;
  std::unique_ptr<Zip> zip;
  std::string root_name;
  if (fs::is_directory(from, ec)) {
    root_name = Utf8Of(from.filename());
    if (auto failure = ListFolder(from, entries, cancel)) return fail(*failure);
  } else {
    root_name = Utf8Of(from.stem());
    if (!LooksLikeZip(from)) return fail({ImportStatus::kNotAZip, Utf8Of(from.filename())});
    zip = std::make_unique<Zip>();
    std::string error;
    if (!zip->Open(from, error)) {
      return fail({ImportStatus::kUnreadable, Utf8Of(from) + ": " + error});
    }
    if (auto failure = ListZip(*zip, entries, cancel)) return fail(*failure);
  }

  const std::vector<FoundMod> mods = FindMods(entries, root_name, result.skipped);
  if (mods.empty()) return fail({ImportStatus::kNoModFound, ""});
  std::unordered_map<std::string, std::string> folded_mods;  // folded name -> name
  for (const FoundMod& mod : mods) {
    // The import's own folders' suffixes are not names a mod can take.
    const bool own_suffix = EndsWithNoCase(mod.name, kImportPartialSuffix) ||
                            EndsWithNoCase(mod.name, kImportReplacedSuffix);
    if (!IsPlainComponent(mod.name) || EqualNoCase(mod.name, kListFile) || own_suffix) {
      return fail({ImportStatus::kUnsafeName, mod.name});
    }
    // Two mods of a pack named alike but for case would be one folder to the guest.
    const auto [it, added] = folded_mods.try_emplace(Folded(mod.name), mod.name);
    if (!added) return fail({ImportStatus::kUnsafeName, it->second + ", " + mod.name});
  }

  std::vector<std::vector<std::pair<const Entry*, fs::path>>> files = FilesOf(entries, mods);
  // Two files of a mod at paths alike but for case: which one the guest reads would depend on the
  // file system.
  for (size_t m = 0; m < mods.size(); ++m) {
    std::unordered_set<std::string> seen;
    for (const auto& [entry, relative] : files[m]) {
      if (entry->directory) continue;
      if (!seen.insert(Folded(Utf8Of(relative))).second) {
        return fail({ImportStatus::kUnsafeName, mods[m].name + "/" + Utf8Of(relative)});
      }
    }
  }

  // Names already in the mods folder, in any case (the guest's paths ignore it). A .partial there
  // is only ever an import's that was interrupted (never loaded as a mod): removed.
  const fs::path mods_folder = data_dir / "mods";
  fs::create_directories(mods_folder, ec);
  if (ec) return fail({ImportStatus::kCannotWrite, Utf8Of(mods_folder) + ": " + ec.message()});
  // What an interrupted import left (its .partial folders, the old folders of a replace): cleared
  // first, as at start-up.
  if (const ImportRecovery recovery = RecoverInterruptedImports(mods_folder); !recovery.ok()) {
    return fail({ImportStatus::kCannotWrite, recovery.error});
  }
  // The mods already there, by the source's name, with every folder that matches it in any case
  // (a case-sensitive file system can hold several: all are the same mod to the guest).
  std::vector<std::vector<std::string>> existing(mods.size());
  {
    fs::directory_iterator it(mods_folder, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      const std::string name = Utf8Of(it->path().filename());
      if (const auto found = folded_mods.find(Folded(name)); found != folded_mods.end()) {
        for (size_t m = 0; m < mods.size(); ++m) {
          if (mods[m].name != found->second) continue;
          if (existing[m].empty()) result.mods.push_back(found->second);
          existing[m].push_back(name);
        }
      }
    }
  }
  if (ec) return fail({ImportStatus::kCannotWrite, Utf8Of(mods_folder) + ": " + ec.message()});
  if (!result.mods.empty() && !options.replace) return fail({ImportStatus::kAlreadyInstalled, ""});
  result.mods.clear();

  // The space: each file rounded up to the disk's blocks, each folder a block, and a reserve left
  // for the saves and settings on the same disk.
  ImportProgress totals;
  std::vector<uint64_t> sizes;
  for (const auto& mod_files : files) {
    for (const auto& [entry, relative] : mod_files) {
      if (entry->directory) {
        sizes.push_back(0);
        continue;
      }
      ++totals.files_total;
      totals.bytes_total = entry->size > std::numeric_limits<uint64_t>::max() - totals.bytes_total
                               ? std::numeric_limits<uint64_t>::max()
                               : totals.bytes_total + entry->size;
      sizes.push_back(entry->size);
    }
  }
  const fs::space_info space = fs::space(mods_folder, ec);
  if (ec) {
    return fail({ImportStatus::kCannotWrite, "cannot read the free space of " + Utf8Of(mods_folder) + ": " +
                                                 ec.message()});
  }
  const uint64_t needed = ImportSpaceNeeded(sizes, kImportBlockSize) + ImportSpaceReserve(space.capacity);
  if (space.available < needed) {
    result.bytes_needed = needed;
    result.bytes_free = space.available;
    return fail({ImportStatus::kNotEnoughSpace,
                 "needs " + Megabytes(needed) + " with the reserve, " + Megabytes(space.available) + " free"});
  }

  // Each mod into its .partial, then all renamed (the old ones aside first); on any failure what
  // this import wrote goes and the old ones come back.
  const auto fault = [&](const std::string& step) { return options.fault && options.fault(step); };
  std::vector<fs::path> partials;
  std::vector<std::pair<fs::path, std::string>> installed;  // a new mod in place, and its name
  std::vector<std::pair<fs::path, fs::path>> backups;       // .replaced -> the old folder
  auto undo = [&](Failure failure) {
    std::error_code undo_ec;
    std::vector<fs::path> discard;
    for (const fs::path& path : partials) {
      if (fs::exists(path, undo_ec)) discard.push_back(path);
    }
    // The new mods already in place go aside first, each with one rename (never half removed),
    // the old ones come back, and only then is anything deleted: the old mod is never lost.
    std::string not_restored;
    for (auto it = installed.rbegin(); it != installed.rend(); ++it) {
      const fs::path aside = mods_folder / PathOf(it->second + kImportUndoPartialSuffix);
      std::string error;
      if (fault("undo:" + it->second) || !platform::RenameNoReplace(it->first, aside, error)) continue;
      discard.push_back(aside);
    }
    for (const auto& [backup, original] : backups) {
      std::string error;
      if (fault("restore:" + Utf8Of(original.filename())) || !platform::RenameNoReplace(backup, original, error)) {
        not_restored += (not_restored.empty() ? "" : ", ") + Utf8Of(backup.filename());
      }
    }
    for (const fs::path& path : discard) fs::remove_all(path, undo_ec);
    result.mods.clear();
    if (!not_restored.empty()) {
      old_lost = true;
      failure = {ImportStatus::kCannotWrite,
                 failure.detail + "; the old mod could not be put back: kept as " + not_restored +
                     " in the mods folder. At the next start, if the new mod is in its place it is kept and the "
                     "old copy removed; otherwise the old one is put back"};
    }
    return fail(std::move(failure));
  };
  Writer writer{progress, cancel, totals};
  writer.Report();
  for (size_t m = 0; m < mods.size(); ++m) {
    const fs::path partial = mods_folder / PathOf(mods[m].name + kImportPartialSuffix);
    partials.push_back(partial);
    fs::create_directories(partial, ec);
    if (ec) return undo({ImportStatus::kCannotWrite, Utf8Of(partial) + ": " + ec.message()});
    for (const auto& [entry, relative] : files[m]) {
      if (writer.Stop()) return undo({ImportStatus::kCancelled, ""});
      if (entry->directory) {
        fs::create_directories(partial / relative, ec);
        if (ec) return undo({ImportStatus::kCannotWrite, Utf8Of(partial / relative)});
        continue;
      }
      if (auto failure = WriteFile(*entry, zip.get(), partial / relative, writer)) {
        return undo(*failure);
      }
    }
  }
  for (size_t m = 0; m < mods.size(); ++m) {
    const fs::path target = mods_folder / PathOf(mods[m].name);
    std::string error;
    for (const std::string& old_name : existing[m]) {
      const fs::path old = mods_folder / PathOf(old_name);
      const fs::path backup = mods_folder / PathOf(old_name + kImportReplacedSuffix);
      if (!platform::RenameNoReplace(old, backup, error)) {
        return undo({ImportStatus::kCannotWrite, Utf8Of(old) + ": " + error});
      }
      backups.emplace_back(backup, old);
    }
    if (fault("swap:" + mods[m].name)) return undo({ImportStatus::kCannotWrite, Utf8Of(target) + ": a test's fault"});
    if (!platform::RenameNoReplace(partials[m], target, error)) {
      return undo({ImportStatus::kCannotWrite, Utf8Of(target) + ": " + error});
    }
    installed.emplace_back(target, mods[m].name);
  }
  // All in place: the old folders go (one that cannot is removed by RecoverInterruptedImports at
  // the next start: its mod is there).
  for (const auto& [backup, original] : backups) {
    fs::remove_all(backup, ec);
    result.replaced.push_back(Utf8Of(original.filename()));
  }
  result.status = ImportStatus::kInstalled;
  for (const FoundMod& mod : mods) result.mods.push_back(mod.name);
  return result;
}

}  // namespace torchlight::mods
