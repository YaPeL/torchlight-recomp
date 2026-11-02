#include "platform/portable_copy.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "platform/durable_file.h"
#include "platform/storage_mode.h"
#include "platform/user_folders.h"

namespace fs = std::filesystem;

namespace torchlight::platform {
namespace {

struct Item {
  fs::path from, to;
  uint64_t size = 0;
  bool folder = false;
  bool durable = true;
};

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// The items of `source`, in order (a folder before what it holds). False with `error` when a folder
// cannot be read: a copy that silently lost part of the saves would be worse than none.
bool ListSource(const CopySource& source, std::vector<Item>& items, std::string& error) {
  std::error_code ec;
  const fs::file_status status = fs::status(source.from, ec);
  if (status.type() == fs::file_type::not_found) {
    // Nothing there, unless a link pointing to what is not there (a card that is not in).
    std::error_code link_ec;
    if (fs::is_symlink(fs::symlink_status(source.from, link_ec))) {
      error = Utf8(source.from) + " is a link to what cannot be reached";
      return false;
    }
    return true;
  }
  if (ec) {
    error = "cannot read " + Utf8(source.from) + ": " + ec.message();
    return false;
  }
  if (!fs::is_directory(status)) {
    error = Utf8(source.from) + " is not a folder";
    return false;
  }
  items.push_back({{}, source.to, 0, true, source.durable});
  if (!source.only.empty()) {
    for (const fs::path& file : source.only) {
      std::error_code entry_ec;
      const fs::path path = source.from / file;
      if (!fs::exists(path, entry_ec)) continue;
      const uint64_t size = fs::file_size(path, entry_ec);
      if (entry_ec) {
        error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
        return false;
      }
      items.push_back({path, source.to / file, size, false, source.durable});
    }
    return true;
  }
  for (fs::recursive_directory_iterator it(source.from, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& path = it->path();
    if (std::find(source.leave_out.begin(), source.leave_out.end(), path) != source.leave_out.end()) {
      it.disable_recursion_pending();
      continue;
    }
    std::error_code entry_ec;
    const fs::file_status entry = it->symlink_status(entry_ec);
    if (entry_ec) {
      error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
      return false;
    }
    const fs::path to = source.to / fs::relative(path, source.from, entry_ec);
    if (entry_ec) {
      error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
      return false;
    }
    if (fs::is_directory(entry)) {
      items.push_back({{}, to, 0, true, source.durable});
    } else if (fs::is_regular_file(entry)) {
      const uint64_t size = it->file_size(entry_ec);
      if (entry_ec) {
        error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
        return false;
      }
      items.push_back({path, to, size, false, source.durable});
    } else {
      error = Utf8(path) + " is a link or a special file, which cannot be copied";
      return false;
    }
  }
  if (ec) {
    error = "cannot read " + Utf8(source.from) + ": " + ec.message();
    return false;
  }
  return true;
}

bool CopyFile(const Item& item, const fs::path& partial, uint64_t& done, uint64_t total,
              const CopyProgress& progress, bool& cancelled, std::string& error) {
  std::ifstream in(item.from, std::ios::binary);
  std::ofstream out(partial / item.to, std::ios::binary);
  if (!in || !out) {
    error = "cannot copy " + Utf8(item.from);
    return false;
  }
  std::vector<char> buffer(1 << 20);
  uint64_t copied = 0;
  while (in) {
    in.read(buffer.data(), std::streamsize(buffer.size()));
    const std::streamsize got = in.gcount();
    if (got > 0) out.write(buffer.data(), got);
    if (!out) {
      error = "cannot write " + Utf8(partial / item.to) + " (is the disk full?)";
      return false;
    }
    copied += uint64_t(got);
    done += uint64_t(got);
    if (progress && !progress(done, total)) {
      cancelled = true;
      return false;
    }
  }
  if (in.bad() || copied != item.size) {
    error = "cannot read " + Utf8(item.from) + " whole";
    return false;
  }
  out.close();
  if (!out) {
    error = "cannot write " + Utf8(partial / item.to) + " (is the disk full?)";
    return false;
  }
  // A power cut after the rename must not leave empty files in a folder taken as whole.
  if (item.durable && !FlushFileToDisk(partial / item.to, error)) {
    error = "cannot write " + Utf8(partial / item.to) + ": " + error;
    return false;
  }
  return true;
}

}  // namespace

std::vector<CopySource> UserFolderSources() {
  return UserFolderSources(PlainUserFolder(UserFolderKind::kConfig),
                           PlainUserFolder(UserFolderKind::kData),
                           PlainUserFolder(UserFolderKind::kCache),
                           PlainUserFolder(UserFolderKind::kGameData),
                           PlainUserFolder(UserFolderKind::kState) / "logs");
}

std::vector<CopySource> UserFolderSources(const fs::path& config, const fs::path& data,
                                          const fs::path& cache, const fs::path& game,
                                          const fs::path& logs) {
  const auto clean = [](const fs::path& path) {
    return path.has_filename() ? path : path.parent_path();
  };
  const fs::path config_folder = clean(config), data_folder = clean(data), cache_folder = clean(cache),
                 game_folder = clean(game), logs_folder = clean(logs);
  std::vector<CopySource> sources;
  const bool shared_config = !config_folder.empty() && config_folder == data_folder;
  const fs::path settings = "settings.toml", runtime = "torchlight.toml";
  if (!config_folder.empty()) {
    CopySource source{config_folder, "config", {config_folder / kUserFoldersChoiceFile}};
    if (shared_config) source.only = {settings, runtime};
    sources.push_back(source);
  }
  if (!data_folder.empty()) {
    CopySource source{data_folder, "data", {}};
    if (shared_config) {
      source.leave_out = {data_folder / settings, data_folder / runtime,
                          data_folder / kUserFoldersChoiceFile};
    }
    sources.push_back(source);
  }
  if (!cache_folder.empty() && cache_folder != data_folder && cache_folder != config_folder) {
    CopySource source{cache_folder, "cache", {}};
    source.durable = false;
    sources.push_back(source);
  }
  if (!game_folder.empty()) sources.push_back({game_folder, "game", {}});
  // Each file once: what a source's folder holds of another's stays out, and the logs of all.
  const auto below = [](const fs::path& inner, const fs::path& outer) {
    const fs::path relative = inner.lexically_relative(outer);
    return !relative.empty() && *relative.begin() != ".." && relative != ".";
  };
  for (CopySource& source : sources) {
    for (const fs::path& other : {config_folder, data_folder, cache_folder, game_folder, logs_folder}) {
      if (!other.empty() && other != source.from && below(other, source.from)) {
        source.leave_out.push_back(other);
      }
    }
  }
  return sources;
}

std::vector<CopySource> PortableFolderSources(const fs::path& portable) {
  std::vector<CopySource> sources;
  for (const char* name : {"config", "data", "cache", "game"}) {
    CopySource source{portable / name, name, {}};
    source.durable = std::string_view(name) != "cache";
    sources.push_back(source);
  }
  return sources;
}

bool SourcesHoldFiles(const std::vector<CopySource>& sources) {
  for (const CopySource& source : sources) {
    std::error_code ec;
    for (const fs::path& file : source.only) {
      if (fs::is_regular_file(source.from / file, ec)) return true;
    }
    if (!source.only.empty()) continue;
    for (fs::recursive_directory_iterator it(source.from, ec), end; !ec && it != end;
         it.increment(ec)) {
      if (std::find(source.leave_out.begin(), source.leave_out.end(), it->path()) !=
          source.leave_out.end()) {
        it.disable_recursion_pending();
        continue;
      }
      std::error_code entry_ec;
      if (it->is_regular_file(entry_ec)) return true;
    }
  }
  return false;
}

bool LooksLikePortableFolder(const fs::path& folder) {
  std::error_code ec;
  // Not the game alone: the user's own folder has game/ too, with the saves elsewhere.
  return fs::is_regular_file(folder / "config" / "settings.toml", ec) ||
         (fs::is_regular_file(folder / "game" / "default.xex", ec) &&
          fs::is_directory(folder / "data", ec));
}

CopyResult CopyIntoPortable(const std::vector<CopySource>& sources, const fs::path& portable,
                            const CopyProgress& progress, std::string& error) {
  std::error_code ec;
  // An empty one is what nobody has used (an attempt that came to nothing).
  if (fs::is_directory(portable, ec) && fs::is_empty(portable, ec)) fs::remove(portable, ec);
  if (fs::exists(portable, ec)) {
    error = Utf8(portable) + " already exists";
    return CopyResult::kFailed;
  }
  fs::path partial = portable;
  partial += ".partial";
  for (const CopySource& source : sources) {
    const fs::path absolute = fs::absolute(source.from, ec).lexically_normal();
    for (const fs::path& target : {portable, partial}) {
      const fs::path relative = absolute.lexically_relative(fs::absolute(target, ec).lexically_normal());
      if (!relative.empty() && *relative.begin() != "..") {
        error = "cannot copy " + Utf8(source.from) + " into itself";
        return CopyResult::kFailed;
      }
    }
  }
  // A partial folder is only ever an interrupted copy of ours; before listing, in case a source
  // holds it.
  fs::remove_all(partial, ec);
  if (ec) {
    error = "cannot remove " + Utf8(partial) + ": " + ec.message();
    return CopyResult::kFailed;
  }
  std::vector<Item> items;
  for (const CopySource& source : sources) {
    if (!ListSource(source, items, error)) return CopyResult::kFailed;
  }
  uint64_t total = 0;
  for (const Item& item : items) total += item.size;

  const auto discard = [&] {
    std::error_code ignored;
    fs::remove_all(partial, ignored);
  };
  if (!fs::create_directories(partial, ec) || ec) {
    error = "cannot create " + Utf8(partial) + ": " + ec.message();
    return CopyResult::kFailed;
  }
  uint64_t done = 0;
  bool cancelled = false;
  for (const Item& item : items) {
    if (item.folder) {
      fs::create_directories(partial / item.to, ec);
      if (ec) {
        error = "cannot create " + Utf8(partial / item.to) + ": " + ec.message();
        discard();
        return CopyResult::kFailed;
      }
    } else if (!CopyFile(item, partial, done, total, progress, cancelled, error)) {
      discard();
      return cancelled ? CopyResult::kCancelled : CopyResult::kFailed;
    }
  }
  fs::rename(partial, portable, ec);
  if (ec) {
    error = "cannot finish " + Utf8(portable) + ": " + ec.message();
    discard();
    return CopyResult::kFailed;
  }
  return CopyResult::kDone;
}

}  // namespace torchlight::platform
