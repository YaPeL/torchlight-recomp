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

// The device the player's replacements folder is mounted as (docs/mods.md 7k): its files are read
// in place of the pak's files at the same path, as if they were inside the pak.
inline constexpr std::string_view kReplacementsDeviceLink = "tlrepl:";
inline constexpr std::string_view kReplacementsDeviceMount = "\\Device\\TorchlightReplacements";
// Whether `path` is on the replacements' device (any case).
bool OnReplacementsDevice(std::string_view path);

// The device the merged icon atlases are mounted as (mods/icon_atlas.h): the app's cache folder of
// files generated at start, served to the guest in place of a mod's imageset.
inline constexpr std::string_view kAtlasDeviceLink = "tlatlas:";
inline constexpr std::string_view kAtlasDeviceMount = "\\Device\\TorchlightAtlases";

// The device the pad prompts' textures are mounted as (mods/pad_prompts.h): the app's cache folder
// of the game's widget textures redrawn at start, read like the replacements folder (before it, so
// the player's own replacements win).
inline constexpr std::string_view kPromptsDeviceLink = "tlprompt:";
inline constexpr std::string_view kPromptsDeviceMount = "\\Device\\TorchlightPrompts";
// Whether `path` is on the pad prompts' device (any case).
bool OnPromptsDevice(std::string_view path);

// The search path the guest's FindFirstFileA is given instead of `path`: WindowsSeparators for a
// path on a mod's device, the replacements' or the prompts', nothing for any other (kept as the Xbox reads it;
// guest_abi xapi_files.h says why).
std::optional<std::string> ModsSearchPath(std::string_view path);

// A resource name without our device, "TLMOD000:/MEDIA/X.DDS" -> "MEDIA/X.DDS", for a name on a
// mod's device, the replacements' or the prompts'; nothing for any other. PC names a mod's resources without a
// device (its mods are folders with relative names), and the game's listener names textures after
// the script's full path, device included (guest_abi game_ui.h kScriptListenerHandleEvent).
std::optional<std::string> WithoutOurDevice(std::string_view name);

}  // namespace torchlight::hooks
