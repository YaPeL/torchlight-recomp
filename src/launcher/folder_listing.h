// Reads a folder from the disk for the launcher's file browser (file_browser_model.h, ListFolder):
// std::filesystem, the same on every platform. Names in UTF-8 on every platform.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "launcher/file_browser_model.h"

namespace torchlight::launcher {

// The folder's entries (folders, through links too, and files with their sizes; any order). An
// entry that cannot be inspected is left out; false with the system's reason when the folder cannot
// be read.
bool ListFolderOnDisk(const std::filesystem::path& folder, std::vector<FolderEntry>& entries,
                      std::string& error);

}  // namespace torchlight::launcher
