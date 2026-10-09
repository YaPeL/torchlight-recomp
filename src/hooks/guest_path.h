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

// The device a mod's folder is mounted as, by its place in the registration order: "tlmod<NNN>:",
// three digits (kMaxModDevices). One device per mod, so the folder's own name (commas, accents, any
// script) never reaches the guest's paths, which the kernel checks as the Xbox does (no '"', '+',
// ',', '<', '>', '|'). Fixed width because the file system matches devices and links by prefix,
// the first registered winning: "tlmod1:" would also take "tlmod10:"'s paths.
inline constexpr size_t kMaxModDevices = 1000;
std::string ModDeviceLink(size_t index);
// The device's mount path, "\Device\TorchlightMod<NNN>", fixed width for the same reason.
std::string ModDeviceMount(size_t index);
// Whether `path` is on a mod's device ("tlmod<N>:", any case).
bool OnModDevice(std::string_view path);

// The search path the guest's FindFirstFileA is given instead of `path`: WindowsSeparators for a
// path on a mod's device, nothing for any other (kept as the Xbox reads it; guest_abi
// xapi_files.h says why).
std::optional<std::string> ModsSearchPath(std::string_view path);

}  // namespace torchlight::hooks
