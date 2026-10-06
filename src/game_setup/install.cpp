#include "game_setup/install.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <vector>

#include <rex/filesystem.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

namespace torchlight::game_setup {

namespace {

namespace fs = std::filesystem;
using rex::X_STATUS;

// UTC date and time for a folder name.
std::string Stamp() {
  return std::format("{:%Y%m%d-%H%M%S}",
                     std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
}

// Copies a folder tree; false with `error` on failure or when cancelled.
bool CopyFolder(const fs::path& from, const fs::path& to, const PhaseProgress& progress,
                Message& error) {
  std::error_code ec;
  uint64_t total = 0, done = 0;
  for (const auto& entry : fs::recursive_directory_iterator(from, ec)) {
    if (entry.is_regular_file(ec)) total += entry.file_size(ec);
  }
  if (ec) {
    error = {std::string(kTextCannotReadFolder),
             {{"path", from.string()}, {"error", ec.message()}}};
    return false;
  }
  std::vector<char> buffer(1 << 20);
  for (const auto& entry : fs::recursive_directory_iterator(from, ec)) {
    const fs::path target = to / fs::relative(entry.path(), from);
    if (entry.is_directory(ec)) {
      fs::create_directories(target, ec);
      continue;
    }
    if (!entry.is_regular_file(ec)) continue;
    fs::create_directories(target.parent_path(), ec);
    std::ifstream in(entry.path(), std::ios::binary);
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!in || !out) {
      error = {std::string(kTextCannotCopy), {{"path", entry.path().string()}}};
      return false;
    }
    while (in) {
      in.read(buffer.data(), std::streamsize(buffer.size()));
      const std::streamsize got = in.gcount();
      if (got <= 0) break;
      out.write(buffer.data(), got);
      done += uint64_t(got);
      if (progress && !progress(Phase::kCopying, done, total)) {
        error = {std::string(kCancelled), {}};
        return false;
      }
    }
    if (in.bad() || !out) {
      error = {std::string(kTextCannotCopy), {{"path", entry.path().string()}}};
      return false;
    }
  }
  return true;
}

// Extracts an STFS package with the runtime's reader.
bool ExtractPackage(const fs::path& package, const fs::path& to, const PhaseProgress& progress,
                    Message& error) {
  rex::filesystem::StfsContainerDevice device("", package);
  if (!device.Initialize()) {
    error = {std::string(kTextCannotReadPackage), {{"path", package.string()}}};
    return false;
  }
  std::vector<rex::filesystem::Entry*> files, pending = {device.ResolvePath("/")};
  uint64_t total = 0, done = 0;
  while (!pending.empty()) {
    rex::filesystem::Entry* entry = pending.back();
    pending.pop_back();
    if (!entry) continue;
    for (const auto& child : entry->children()) {
      if (child->attributes() & rex::filesystem::kFileAttributeDirectory) {
        pending.push_back(child.get());
      } else {
        files.push_back(child.get());
        total += child->size();
      }
    }
  }
  std::error_code ec;
  std::vector<uint8_t> buffer(1 << 20);
  for (rex::filesystem::Entry* entry : files) {
    // Entry paths are guest paths ("music\\song.ogg").
    std::string relative = entry->path();
    std::replace(relative.begin(), relative.end(), '\\', '/');
    const fs::path target = to / fs::path(relative).relative_path();
    fs::create_directories(target.parent_path(), ec);
    rex::filesystem::File* in = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in) != X_STATUS_SUCCESS || !in) {
      error = {std::string(kTextCannotReadEntry), {{"file", entry->path()}}};
      return false;
    }
    std::unique_ptr<rex::filesystem::File, void (*)(rex::filesystem::File*)> file(
        in, [](rex::filesystem::File* f) { f->Destroy(); });
    std::ofstream out(target, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = {std::string(kTextCannotWrite), {{"path", target.string()}}};
      return false;
    }
    for (size_t offset = 0; offset < entry->size();) {
      const size_t want = std::min(buffer.size(), entry->size() - offset);
      size_t got = 0;
      if (file->ReadSync(std::span<uint8_t>(buffer.data(), want), offset, &got) !=
              X_STATUS_SUCCESS ||
          got == 0) {
        error = {std::string(kTextCannotReadEntry), {{"file", entry->path()}}};
        return false;
      }
      out.write(reinterpret_cast<const char*>(buffer.data()), std::streamsize(got));
      offset += got;
      done += got;
      if (progress && !progress(Phase::kCopying, done, total)) {
        error = {std::string(kCancelled), {}};
        return false;
      }
    }
    if (!out) {
      error = {std::string(kTextCannotWrite), {{"path", target.string()}}};
      return false;
    }
  }
  return true;
}

}  // namespace

std::optional<PackageFacts> ReadPackageFacts(const std::filesystem::path& package) {
  const auto header = rex::filesystem::StfsContainerDevice::ReadPackageHeader(package);
  if (!header) return std::nullopt;
  PackageFacts facts;
  facts.magic_valid = header->header.is_magic_valid();
  facts.content_type =
      uint32_t(static_cast<rex::system::XContentType>(header->metadata.content_type));
  facts.title_id = header->metadata.execution_info.title_id;
  return facts;
}

InstallResult Install(Source source, const std::filesystem::path& from,
                      const std::filesystem::path& game_dir, std::span<const GameFile> files,
                      const PhaseProgress& progress) {
  InstallResult result;
  fs::path dir = game_dir;
  if (!dir.has_filename()) dir = dir.parent_path();  // "…/game/" -> "…/game"
  const fs::path partial = fs::path(dir.string() + ".partial");
  std::error_code ec;
  // A .partial is only ever ours: what an interrupted install left.
  fs::remove_all(partial, ec);
  fs::create_directories(partial, ec);
  if (ec) {
    result.error = {std::string(kTextCannotCreate),
                    {{"path", partial.string()}, {"error", ec.message()}}};
    return result;
  }
  const bool copied = source == Source::kPackage
                          ? ExtractPackage(from, partial, progress, result.error)
                          : CopyFolder(from, partial, progress, result.error);
  if (copied) {
    result.error = VerifyGameFolder(partial, files, [&](uint64_t done, uint64_t total) {
      return !progress || progress(Phase::kChecking, done, total);
    });
  }
  if (!result.error.empty()) {
    fs::remove_all(partial, ec);
    return result;
  }
  if (fs::exists(dir, ec)) {
    const fs::path aside = fs::path(dir.string() + ".old-" + Stamp());
    fs::rename(dir, aside, ec);
    if (ec) {
      result.error = {std::string(kTextCannotMoveAside),
                      {{"path", dir.string()}, {"error", ec.message()}}};
      fs::remove_all(partial, ec);
      return result;
    }
    result.moved_to = aside.string();
  }
  fs::rename(partial, dir, ec);
  if (ec) {
    result.error = {std::string(kTextCannotFinish),
                    {{"path", dir.string()}, {"error", ec.message()}}};
  }
  return result;
}

}  // namespace torchlight::game_setup
