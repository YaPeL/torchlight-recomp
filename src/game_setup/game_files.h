// The game's files the first start installs (docs/release-pipeline.md, REL.4): what a valid
// install holds, checked by size and SHA-256 against a table made from the one supported package
// (Torchlight XBLA, version 1.0.140.0, title 0x58410A7E). Facts about the files, not the files.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>

#include "game_setup/setup_text.h"

namespace torchlight::game_setup {

struct GameFile {
  std::string_view path;    // relative, with "/"
  uint64_t size;
  std::string_view sha256;  // 64 lower-case hex digits
};

// The supported package's files (game_files_table.inc, made by tools/game_files_table.py).
std::span<const GameFile> GameFiles();

inline constexpr std::string_view kXexPath = "default.xex";
inline constexpr uint32_t kTitleId = 0x58410A7E;
inline constexpr uint32_t kContentTypeArcadeTitle = 0x000D0000;

// Work done so far in bytes, of `total`; returns false to cancel.
using Progress = std::function<bool(uint64_t done, uint64_t total)>;

// What a package's header says (rex::filesystem::StfsHeader, read by install.h).
struct PackageFacts {
  bool magic_valid = false;  // LIVE, CON or PIRS
  uint32_t content_type = 0;
  uint32_t title_id = 0;
};
// Empty when the package is the supported game; else the message for the user.
Message CheckPackage(const PackageFacts& facts);

// SHA-256 of a file as lower-case hex; empty when it cannot be read or `progress` cancels. Bytes
// read are added to `done` (of `total`) as it goes.
std::string Sha256File(const std::filesystem::path& file, const Progress& progress = {},
                       uint64_t* done = nullptr, uint64_t total = 0);

// Empty when `dir` holds every file of `files` with its size and SHA-256 (other files may be
// there too); else the message for the user; kCancelled when `progress` cancels.
Message VerifyGameFolder(const std::filesystem::path& dir, std::span<const GameFile> files,
                             const Progress& progress = {});
// `dir`'s default.xex has the table's size and SHA-256.
bool XexMatches(const std::filesystem::path& dir, std::span<const GameFile> files);
// The check at every start: every file of `files` is in `dir` with its size, and default.xex has
// its SHA-256 (a few milliseconds: the other files are not read). Empty when it holds; else the
// message (missing, another size, default.xex differs). An install is renamed into place only
// once complete (install.h), so this finds a folder emptied or damaged afterwards, or a
// --game_data_root that is not a whole game.
Message QuickCheckGameFolder(const std::filesystem::path& dir, std::span<const GameFile> files);

// The text of the message a cancelled operation returns (never shown).
inline constexpr std::string_view kCancelled = "cancelled";
inline bool Cancelled(const Message& message) { return message.text == kCancelled; }

// The messages' English texts, for the translation check (setup_text_test).
inline constexpr std::string_view kTextNotPackage =
    "This file is not an Xbox 360 package (no LIVE, CON or PIRS header).";
inline constexpr std::string_view kTextNotArcade =
    "This package is not an Xbox Live Arcade game (content type {value}).";
inline constexpr std::string_view kTextNotTorchlight =
    "This package is not Torchlight (title ID {value}, expected {expected}).";
inline constexpr std::string_view kTextMissing =
    "The game files are incomplete: {file} is missing.";
inline constexpr std::string_view kTextOtherSize =
    "The game files do not match the supported version (Torchlight XBLA 1.0.140.0): {file} has "
    "another size.";
inline constexpr std::string_view kTextDiffers =
    "The game files do not match the supported version (Torchlight XBLA 1.0.140.0): {file} "
    "differs.";
inline constexpr std::string_view kTextCannotRead = "Cannot read {file}.";

}  // namespace torchlight::game_setup
