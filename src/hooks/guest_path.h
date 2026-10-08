// Paths as the guest's own file code reads them. The game was written for Windows, where '/' and
// '\' both separate folders; the Xbox library it links (XAPI) splits some paths only at '\'.

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace torchlight::hooks {

// `path` with Windows separators: every '/' made '\' and repeated separators made one (a leading
// "\\" is kept). Nothing when the path is already so (no copy needed).
std::optional<std::string> WindowsSeparators(std::string_view path);

}  // namespace torchlight::hooks
