// Development only: --dev_deterministic_time makes two runs of the same input script see the same
// time, so their commands can be compared (docs/dev-input-script.md). The C runtime's time()
// returns a fixed date (rand() is seeded with it), and the game's counter (QueryPerformanceCounter,
// its frequency and GetTickCount) moves a fixed step per guest frame (virtual_clock.h) instead of
// with real time. Compiled in only with TORCHLIGHT_DEV_INPUT, and on only with an input script,
// which steps the clock once per guest frame.

#pragma once

namespace torchlight::dev {

// From InstallInputScript, before the guest runs: on when the cvar asks for it.
void InstallDeterministicTime();
bool DeterministicTimeOn();
// Once per guest frame (the input script's runner).
void AdvanceDeterministicTime();
// The virtual seconds since the start (0 when off).
double DeterministicSeconds();

}  // namespace torchlight::dev
