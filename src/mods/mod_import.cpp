#include "mods/mod_import.h"

#include <miniz.h>

#include <algorithm>
#include <fstream>
#include <memory>
#include <optional>
#include <system_error>

#include "mods/mod_list.h"
#include "platform/user_folders.h"

namespace torchlight::mods {

namespace {

namespace fs = std::filesystem;

constexpr const char* kPartialSuffix = ".partial";
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
// colons (drive letters, NTFS streams) or control characters, not "." or "..", and no trailing dot
// or space (Windows drops them, so two names would meet).
bool IsPlainComponent(std::string_view name) {
  if (name.empty() || name == "." || name == "..") return false;
  if (name.back() == '.' || name.back() == ' ') return false;
  return std::none_of(name.begin(), name.end(), [](char c) {
    return c == '/' || c == '\\' || c == ':' || IsControl(c);
  });
}

// A zip entry's name as components, '\' read as a separator; nothing when it is absolute, has a
// drive letter or a colon, or a ".." (it would write outside the mod's folder). Empty and "."
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
    if (part == ".." || part.find(':') != std::string::npos) return std::nullopt;
    if (std::any_of(part.begin(), part.end(), IsControl)) return std::nullopt;
    parts.push_back(part);
  }
  if (parts.empty()) return std::nullopt;
  return parts;
}

bool StartsWith(const std::vector<std::string>& parts, const std::vector<std::string>& prefix) {
  return parts.size() > prefix.size() && std::equal(prefix.begin(), prefix.end(), parts.begin());
}

// Whether the folder at `prefix` is a mod: a mod.dat file or a media folder right in it.
bool IsModAt(const std::vector<Entry>& entries, const std::vector<std::string>& prefix) {
  for (const Entry& entry : entries) {
    if (!StartsWith(entry.parts, prefix)) continue;
    const std::string& first = entry.parts[prefix.size()];
    const bool last = entry.parts.size() == prefix.size() + 1;
    if (last && !entry.directory && EqualNoCase(first, kDescriptorFile)) return true;
    if (EqualNoCase(first, "media") && (!last || entry.directory)) return true;
  }
  return false;
}

// The names right below `prefix`, in first-seen order, and whether each is a folder.
std::vector<std::pair<std::string, bool>> Children(const std::vector<Entry>& entries,
                                                   const std::vector<std::string>& prefix) {
  std::vector<std::pair<std::string, bool>> children;
  for (const Entry& entry : entries) {
    if (!StartsWith(entry.parts, prefix)) continue;
    const std::string& name = entry.parts[prefix.size()];
    const bool folder = entry.directory || entry.parts.size() > prefix.size() + 1;
    auto it = std::find_if(children.begin(), children.end(),
                           [&](const auto& child) { return child.first == name; });
    if (it == children.end()) children.emplace_back(name, folder);
    else it->second = it->second || folder;
  }
  return children;
}

// The mod folders right below `prefix`; the other children go to `skipped`.
std::vector<FoundMod> ModFoldersAt(const std::vector<Entry>& entries,
                                   const std::vector<std::string>& prefix,
                                   std::vector<std::string>& skipped) {
  std::vector<FoundMod> mods;
  std::vector<std::string> others;
  for (const auto& [name, folder] : Children(entries, prefix)) {
    std::vector<std::string> child = prefix;
    child.push_back(name);
    if (folder && IsModAt(entries, child)) mods.push_back({name, child});
    else others.push_back(name);
  }
  if (!mods.empty()) skipped.insert(skipped.end(), others.begin(), others.end());
  return mods;
}

std::vector<FoundMod> FindMods(const std::vector<Entry>& entries, const std::string& root_name,
                               std::vector<std::string>& skipped) {
  if (IsModAt(entries, {})) return {{root_name, {}}};
  auto mods = ModFoldersAt(entries, {}, skipped);
  if (!mods.empty()) return mods;
  const auto children = Children(entries, {});
  if (children.size() == 1 && children.front().second) {
    return ModFoldersAt(entries, {children.front().first}, skipped);
  }
  return {};
}

// ---- the zip --------------------------------------------------------------------------------

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

// The zip's entries, every one checked; a failure on the first that cannot be taken.
std::optional<Failure> ListZip(Zip& zip, std::vector<Entry>& entries) {
  mz_zip_archive* archive = zip.get();
  const mz_uint count = mz_zip_reader_get_num_files(archive);
  for (mz_uint i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(archive, i, &stat)) {
      return Failure{ImportStatus::kUnreadable,
                     mz_zip_get_error_string(mz_zip_get_last_error(archive))};
    }
    // The whole name (the stat's copy stops at 512 bytes).
    std::string name(mz_zip_reader_get_filename(archive, i, nullptr, 0), '\0');
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

std::optional<Failure> ListFolder(const fs::path& folder, std::vector<Entry>& entries) {
  std::error_code ec;
  fs::recursive_directory_iterator it(folder, ec), end;
  for (; !ec && it != end; it.increment(ec)) {
    const fs::directory_entry& item = *it;
    const fs::path relative = item.path().lexically_relative(folder);
    if (item.is_symlink(ec)) {
      return Failure{ImportStatus::kUnsupportedEntry, Utf8Of(relative) + " (symbolic link)"};
    }
    Entry entry;
    for (const fs::path& part : relative) entry.parts.push_back(Utf8Of(part));
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

// The entries of `mod` to write and where below its folder; a mods.dat at its top is left out.
std::vector<std::pair<const Entry*, fs::path>> FilesOf(const std::vector<Entry>& entries,
                                                       const FoundMod& mod) {
  std::vector<std::pair<const Entry*, fs::path>> files;
  for (const Entry& entry : entries) {
    if (!StartsWith(entry.parts, mod.prefix)) continue;
    const size_t depth = entry.parts.size() - mod.prefix.size();
    if (depth == 1 && !entry.directory && EqualNoCase(entry.parts.back(), kListFile)) continue;
    fs::path relative;
    for (size_t i = mod.prefix.size(); i < entry.parts.size(); ++i) {
      relative /= PathOf(entry.parts[i]);
    }
    files.emplace_back(&entry, relative);
  }
  return files;
}

std::string Megabytes(uint64_t bytes) {
  return std::to_string((bytes + (1 << 20) - 1) >> 20) + " MB";
}

}  // namespace

std::string ImportResult::message() const {
  std::string names;
  for (const std::string& mod : mods) names += (names.empty() ? "" : ", ") + mod;
  const std::string about = detail.empty() ? "" : ": " + detail;
  switch (status) {
    case ImportStatus::kInstalled: return "installed " + names;
    case ImportStatus::kCancelled: return "cancelled; nothing installed";
    case ImportStatus::kNoModFound: return "no mod found (no mod.dat or media folder)";
    case ImportStatus::kAlreadyInstalled: return "already installed: " + names;
    case ImportStatus::kUnreadable: return "cannot read the source" + about;
    case ImportStatus::kUnsafeName: return "unsafe name" + about;
    case ImportStatus::kUnsupportedEntry: return "unsupported entry" + about;
    case ImportStatus::kNonAsciiName: return "entry name not in UTF-8 or ASCII" + about;
    case ImportStatus::kNotEnoughSpace: return "not enough free space" + about;
    case ImportStatus::kCannotWrite: return "cannot write" + about;
  }
  return "";
}

ImportResult ImportMod(const std::filesystem::path& source, const std::filesystem::path& data_dir,
                       const ImportProgressCallback& progress, const std::atomic<bool>* cancel) {
  ImportResult result;
  auto fail = [&](Failure failure) {
    result.status = failure.status;
    result.detail = std::move(failure.detail);
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
    if (auto failure = ListFolder(from, entries)) return fail(*failure);
  } else {
    root_name = Utf8Of(from.stem());
    zip = std::make_unique<Zip>();
    std::string error;
    if (!zip->Open(from, error)) {
      return fail({ImportStatus::kUnreadable, Utf8Of(from) + ": " + error});
    }
    if (auto failure = ListZip(*zip, entries)) return fail(*failure);
  }

  const std::vector<FoundMod> mods = FindMods(entries, root_name, result.skipped);
  if (mods.empty()) return fail({ImportStatus::kNoModFound, ""});
  for (const FoundMod& mod : mods) {
    const std::string_view suffix(kPartialSuffix);
    const bool partial_name = mod.name.size() > suffix.size() &&
                              EqualNoCase(mod.name.substr(mod.name.size() - suffix.size()), suffix);
    if (!IsPlainComponent(mod.name) || EqualNoCase(mod.name, kListFile) || partial_name) {
      return fail({ImportStatus::kUnsafeName, mod.name});
    }
  }

  // Names already in the mods folder, in any case (the guest's paths ignore it).
  const fs::path mods_folder = data_dir / "mods";
  fs::create_directories(mods_folder, ec);
  if (ec) return fail({ImportStatus::kCannotWrite, Utf8Of(mods_folder) + ": " + ec.message()});
  for (const auto& existing : fs::directory_iterator(mods_folder, ec)) {
    const std::string name = Utf8Of(existing.path().filename());
    for (const FoundMod& mod : mods) {
      if (EqualNoCase(name, mod.name)) result.mods.push_back(mod.name);
    }
  }
  if (!result.mods.empty()) return fail({ImportStatus::kAlreadyInstalled, ""});

  std::vector<std::vector<std::pair<const Entry*, fs::path>>> files;
  ImportProgress totals;
  for (const FoundMod& mod : mods) {
    files.push_back(FilesOf(entries, mod));
    for (const auto& [entry, relative] : files.back()) {
      if (entry->directory) continue;
      ++totals.files_total;
      totals.bytes_total += entry->size;
    }
  }
  const fs::space_info space = fs::space(mods_folder, ec);
  if (!ec && space.available < totals.bytes_total) {
    result.bytes_needed = totals.bytes_total;
    result.bytes_free = space.available;
    return fail({ImportStatus::kNotEnoughSpace,
                 "needs " + Megabytes(totals.bytes_total) + ", " + Megabytes(space.available) +
                     " free"});
  }

  // Each mod into its .partial, then all renamed; on any failure what this import wrote goes.
  std::vector<fs::path> partials, installed;
  auto undo = [&](Failure failure) {
    for (const fs::path& path : partials) fs::remove_all(path, ec);
    for (const fs::path& path : installed) fs::remove_all(path, ec);
    result.mods.clear();
    return fail(std::move(failure));
  };
  Writer writer{progress, cancel, totals};
  writer.Report();
  for (size_t m = 0; m < mods.size(); ++m) {
    const fs::path partial = mods_folder / PathOf(mods[m].name + kPartialSuffix);
    // A .partial is only ever ours: what an interrupted import left.
    fs::remove_all(partial, ec);
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
    if (!platform::RenameNoReplace(partials[m], target, error)) {
      return undo({ImportStatus::kCannotWrite, Utf8Of(target) + ": " + error});
    }
    installed.push_back(target);
  }
  result.status = ImportStatus::kInstalled;
  for (const FoundMod& mod : mods) result.mods.push_back(mod.name);
  return result;
}

}  // namespace torchlight::mods
