// The guest's file functions from the Xbox library it links (XAPI, statically linked). Of them
// only the directory search parses a path itself; the others (NtCreateFile callers sub_8287E038,
// sub_82885858; NtOpenFile callers sub_8287DE58, sub_82882F58, sub_828835E8, sub_828836B8,
// sub_82885778; NtQueryFullAttributesFile sub_82882D48) hand the whole path to the kernel
// (RtlInitAnsiString), whose path resolution takes '/' and '\' alike.
//
// The search's '\'-only split is fixed for the mods' device only (hooks/find_file_hooks.cpp), not
// for every path: the game's other searches with '/' are OGRE's recursive
// FileSystemArchive::findFiles (OgreFileSystem.cpp:146-160 in 1.7.0, through _findfirst
// sub_82862F80), which lists subfolders with "<dir>/*" and so never entered a subfolder on the
// Xbox. Made global, it would (a run's diagnostics showed) start indexing
// game:\RTShaderLib\materials\RTShaderSystem.material, which the Xbox game never loaded; the
// other such searches (game:\music, game:\programs, tlhost:, SAVE: through sub_823A1010) have no
// subfolders.

#pragma once

#include <cstdint>

#include "guest_abi/guest_functions.h"
#include "guest_abi/ogre_layout.h"

namespace torchlight::guest_abi::xapi {

using functions::GuestFunction;

namespace find {
// [confirmed] FindFirstFileA(r3 = path, r4 = WIN32_FIND_DATAA*) -> handle, -1 on failure: calls
// kFindFirstNative with the path; on success (bge 0x8287E130) kFindDataFromNative into r4.
inline constexpr GuestFunction kFindFirstFile{0x8287E0C8, Confidence::kConfirmed};
// [confirmed] FindNextFileA(r3 = handle, r4 = WIN32_FIND_DATAA*) -> 1, or 0 when the search ends
// or fails: kFindNextNative, then on success (bge 0x8287E18C) kFindDataFromNative into r4.
inline constexpr GuestFunction kFindNextFile{0x8287E158, Confidence::kConfirmed};
// [confirmed] The native search under kFindFirstFile -> NTSTATUS. Splits the path at its last '\'
// (backwards scan, cmplwi 92): without one it returns 0xC000000D and opens nothing (0x82883BDC);
// a pattern of exactly "*.*" is made empty, i.e. match all (before 0x82883B40). Opens the folder
// with NtOpenFile (bl 0x83026DBC: access 0x00100001, share 3, options 0x4021) and lists it with
// NtQueryDirectoryFile through the import table (bctrl; FileName = the pattern, RestartScan 0).
inline constexpr GuestFunction kFindFirstNative{0x82883A68, Confidence::kConfirmed};
// [confirmed] The native search under kFindNextFile -> NTSTATUS (NtQueryDirectoryFile, bctrl).
inline constexpr GuestFunction kFindNextNative{0x82883BF0, Confidence::kConfirmed};
// [confirmed] Native entry -> WIN32_FIND_DATAA (sub_82883F88): attributes to +0 (from +56), the
// name to +44 (from +64, length +60), NUL-terminated.
inline constexpr GuestFunction kFindDataFromNative{0x82883F88, Confidence::kConfirmed};
namespace find_data {
inline constexpr Field kAttributes{0, Confidence::kConfirmed};
inline constexpr Field kFileName{44, Confidence::kConfirmed};
inline constexpr uint32_t kFileNameCapacity = 260;
inline constexpr uint32_t kAttributeDirectory = 0x10;  // FILE_ATTRIBUTE_DIRECTORY
}  // namespace find_data
}  // namespace find

}  // namespace torchlight::guest_abi::xapi
