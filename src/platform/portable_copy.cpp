#include "platform/portable_copy.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#include "platform/storage_mode.h"
#include "platform/user_folders.h"

namespace fs = std::filesystem;

namespace torchlight::platform {
namespace {

struct Item {
  fs::path from, to;
  uint64_t size = 0;
  bool folder = false;
};

std::string Utf8(const fs::path& path) {
  const std::u8string text = path.u8string();
  return std::string(text.begin(), text.end());
}

// The items of `source`, in order (a folder before what it holds). False with `error` when a folder
// cannot be read: a copy that silently lost part of the saves would be worse than none.
bool ListSource(const CopySource& source, std::vector<Item>& items, std::string& error) {
  std::error_code ec;
  if (!fs::is_directory(source.from, ec)) return true;
  items.push_back({{}, source.to, 0, true});
  for (fs::recursive_directory_iterator it(source.from, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& path = it->path();
    if (std::find(source.leave_out.begin(), source.leave_out.end(), path) != source.leave_out.end()) {
      it.disable_recursion_pending();
      continue;
    }
    std::error_code entry_ec;
    const fs::file_status status = it->symlink_status(entry_ec);
    if (entry_ec) {
      error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
      return false;
    }
    const fs::path to = source.to / fs::relative(path, source.from, entry_ec);
    if (entry_ec) {
      error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
      return false;
    }
    if (fs::is_directory(status)) {
      items.push_back({{}, to, 0, true});
    } else if (fs::is_regular_file(status)) {
      const uint64_t size = it->file_size(entry_ec);
      if (entry_ec) {
        error = "cannot read " + Utf8(path) + ": " + entry_ec.message();
        return false;
      }
      items.push_back({path, to, size, false});
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
  return true;
}

}  // namespace

std::vector<CopySource> UserFolderSources() {
  return UserFolderSources(PlainUserFolder(UserFolderKind::kConfig),
                           PlainUserFolder(UserFolderKind::kData),
                           PlainUserFolder(UserFolderKind::kCache),
                           PlainUserFolder(UserFolderKind::kGameData));
}

std::vector<CopySource> UserFolderSources(const fs::path& config, const fs::path& data,
                                          const fs::path& cache, const fs::path& game) {
  std::vector<CopySource> sources;
  // The choice of the user's folders says nothing about a portable folder.
  if (!config.empty()) sources.push_back({config, "config", {config / kUserFoldersChoiceFile}});
  // The game's files may be below the saves' folder (Linux): they have their own place.
  if (!data.empty()) sources.push_back({data, "data", {game}});
  if (!cache.empty() && cache != data) sources.push_back({cache, "cache", {}});
  if (!game.empty()) sources.push_back({game, "game", {}});
  return sources;
}

std::vector<CopySource> PortableFolderSources(const fs::path& portable) {
  std::vector<CopySource> sources;
  for (const char* name : {"config", "data", "cache", "game"}) {
    sources.push_back({portable / name, name, {}});
  }
  return sources;
}

bool SourcesHoldFiles(const std::vector<CopySource>& sources) {
  for (const CopySource& source : sources) {
    std::error_code ec;
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
  for (const char* name : {"config", "data", "game"}) {
    if (fs::is_directory(folder / name, ec)) return true;
  }
  return false;
}

CopyResult CopyIntoPortable(const std::vector<CopySource>& sources, const fs::path& portable,
                            const CopyProgress& progress, std::string& error) {
  std::error_code ec;
  if (fs::exists(portable, ec)) {
    error = Utf8(portable) + " already exists";
    return CopyResult::kFailed;
  }
  std::vector<Item> items;
  for (const CopySource& source : sources) {
    if (!ListSource(source, items, error)) return CopyResult::kFailed;
  }
  uint64_t total = 0;
  for (const Item& item : items) total += item.size;

  fs::path partial = portable;
  partial += ".partial";
  // A partial folder is only ever an interrupted copy of ours.
  fs::remove_all(partial, ec);
  if (ec) {
    error = "cannot remove " + Utf8(partial) + ": " + ec.message();
    return CopyResult::kFailed;
  }
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
