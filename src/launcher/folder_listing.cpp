#include "launcher/folder_listing.h"

#include <system_error>

namespace torchlight::launcher {

bool ListFolderOnDisk(const std::filesystem::path& folder, std::vector<FolderEntry>& entries,
                      std::string& error) {
  namespace fs = std::filesystem;
  entries.clear();
  std::error_code ec;
  fs::directory_iterator it(folder, ec);
  for (const fs::directory_iterator end; !ec && it != end; it.increment(ec)) {
    std::error_code entry_ec;
    const fs::file_status status = it->status(entry_ec);  // follows links
    if (entry_ec) continue;
    const std::u8string name = it->path().filename().u8string();
    FolderEntry entry{std::string(name.begin(), name.end()), fs::is_directory(status), 0};
    if (fs::is_regular_file(status)) {
      const uintmax_t size = it->file_size(entry_ec);
      if (!entry_ec) entry.size = size;
    }
    entries.push_back(std::move(entry));
  }
  if (ec) {
    entries.clear();
    error = ec.message();
    return false;
  }
  return true;
}

}  // namespace torchlight::launcher
