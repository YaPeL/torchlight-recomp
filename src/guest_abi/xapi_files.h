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
// [confirmed] FindFirstFileA(r3 = path, r4 = WIN32_FIND_DATAA*) -> handle, -1 on failure. Its native
// search (sub_82883A68) splits the path at its last '\' (backwards scan, cmplwi 92): without one it
// returns 0xC000000D and opens nothing (0x82883BDC); a pattern of exactly "*.*" is made empty, i.e.
// match all (before 0x82883B40). It opens the folder with NtOpenFile (bl 0x83026DBC: access
// 0x00100001, share 3, options 0x4021) and lists it with NtQueryDirectoryFile through the import
// table (bctrl; FileName = the pattern, RestartScan 0); on success (bge 0x8287E130) r4 is filled
// from the native entry.
inline constexpr GuestFunction kFindFirstFile{0x8287E0C8, Confidence::kConfirmed};
// [confirmed] FindFirstFileA's native search, called only from it (@0x8287E118): r3 = the
// OBJECT_ATTRIBUTES FindFirstFileA builds on its stack (RootDirectory -3 at +0, @0x8287E0FC;
// ObjectName at +4, an ANSI_STRING {u16 Length, u16 MaximumLength, char* Buffer} made from the
// path by RtlInitAnsiString @0x8287E0EC; Attributes 0x40 at +8), r4 the native entry (0x148
// bytes), r6 the handle. Returns the NTSTATUS; on a negative one FindFirstFileA passes it to
// kSetLastNtError (@0x8287E124), fills nothing and returns -1 (@0x8287E128), so the status alone
// decides what the caller sees.
inline constexpr GuestFunction kNativeSearch{0x82883A68, Confidence::kConfirmed};
// [confirmed] Sets the thread's last error from an NTSTATUS (r3): RtlNtStatusToDosError
// (bl 0x830269AC) stored at KTHREAD (r13+0x100) +0x160 unless r13+0x150 is set (@0x82882BE8..
// @0x82882BF8).
inline constexpr GuestFunction kSetLastNtError{0x82882BD8, Confidence::kConfirmed};
}  // namespace find

}  // namespace torchlight::guest_abi::xapi
