// The file name a picked document is copied under (platform_android.cpp OpenablePickedFile), read
// from its content URI. Plain string work, tested on every platform (document_name_test.cpp).

#pragma once

#include <string>

namespace torchlight::platform {

// The document's own file name where its URI ends in its path, as the external storage and
// downloads providers write it (".../document/primary%3ADownload%2FMyMod.zip",
// ".../document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2FMyMod.zip"); "picked_file" when it
// names no file with an extension ("msf:1234") or the name could be read as a path. The mods'
// import names a mod at a zip's root after it (mods/mod_import.h).
std::string PickedDocumentName(const std::string& uri);

}  // namespace torchlight::platform
