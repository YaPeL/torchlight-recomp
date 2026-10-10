# Changelog

User-facing changes for the release notes, newest first. Developers: add a line under
"Unreleased" for anything a player or tester would notice.

## Unreleased

### Fixed

- Linux on Wayland: the game was shown with 16-bit colour (RGB565), which made gradients band.
  The window now uses 8 bits per channel, like the other platforms.

### Changed

- The frame counter (F3) and the log also show the frames that reach the screen, and the game's
  frames dropped when the renderer falls behind, next to the game's own frame rate.

## v0.1.0-beta (2026-10-06)

First public beta for Linux and Windows.
