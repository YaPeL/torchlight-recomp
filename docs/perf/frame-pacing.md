# Frame pacing: even frames

How evenly frames reach the screen, what holds it back, and the plan to lock the guest's vblank to
the display ("Even frames"). Measured with the even-frames count (`src/live/frame_pacing.h`): each
frame's time on screen in display refreshes, from the window system's presentation times
(`src/platform/presentation_time.h`, Wayland and Android with `TORCHLIGHT_PRESENTATION_TIME`).
"Even" is the share of frames held the most common number of refreshes.

## 1. Where it stands (POCO X7, 120 Hz, vsync on, 2026-10-29)

The host's fps cap is the guest's vblank rate (`patches/rexglue-guest-vblank-rate.patch`): a
timer on the guest's tick clock, not locked to the display. Even where the game holds the cap
exactly, the timer drifts against the display's refreshes:

| Scene | Cap | fps | Refreshes a frame stays (share of frames) | Even |
| --- | --- | --- | --- | --- |
| Town, walking (`town.txt`) | 60 | 58.4 | 1: 5 %, 2: 83 %, 3: 11 % | 83 % |
| Town, walking | 40 | 40.0 | 2: 15 %, 3: 68 %, 4: 15 % | 68 % |
| Short fight (`fight.txt`) | 60 | 45.9 | 2: 45 %, 3: 53 % | 53 % |
| Short fight | 40 | 39.1 | 2: 14 %, 3: 72 %, 4: 14 % | 72 % |
| Long fight (`fight5.txt`) | 60 | 36.5 | 2: 13 %, 3: 55 %, 4: 25 %, 5: 6 % | 55 % |

At a steady 40.0 fps a third of the frames stay 2 or 4 refreshes instead of 3. A cap of 40 (an
option since #61, not a default) makes frames more even where the game holds more than 40, and
less where it does not (a scene at 41 fps falls to 30 with the cap at 40: a frame that misses the
guest's 25 ms vblank waits for the next).

The cap of 30 did not limit at all (47.7 fps at rest): the game's own adaptive vsync sets the
presentation interval to IMMEDIATE whenever its lastFPS is under 31 (`guest_abi/adaptive_vsync.h`),
which a cap of 30 always is. Under a host cap of 31 or less, `hooks/adaptive_vsync.h` keeps the
presents waiting for the vblank; over 31 the game decides as before.

## 2. The plan: the guest's vblank from the display's refreshes

As DemonsSoulsRecomp's "Even frames" (PR #166, MIT): the cap is a divisor of the refresh rate and
the game's vblank is ticked on every n-th display refresh, so each frame is held n refreshes.

**Where it plugs in.**
- The SDK (the only part there, in the vblank patch, designed and agreed before ReXGlue is
  touched): an external mode for the guest vblank, `SetGuestVblankExternal(bool)` and
  `SignalGuestVblank()` from any thread, waking the vblank thread to mark the vblank; a watchdog
  that falls back to the timer when no signal comes within 2.5 periods (the display asleep, the app
  in the background).
- `src/platform/display_vblank.h`, a new interface: the display's refreshes as a callback (time,
  period, count) on a thread of its own; null where a platform has none (the timer stays). Per
  platform: Android, `AChoreographer_postVsyncCallback` (API 33; else `postFrameCallback64`), the
  period re-read on each callback (Android changes the rate on its own); Windows,
  `IDXGIOutput::WaitForVBlank` on the window's output (GL and Direct3D 11 alike); macOS, the
  CVDisplayLink of `display_refresh_mac.mm`; Wayland, a predicted refresh corrected by the
  presentation feedback; X11, `GLX_OML_sync_control` (or `GLX_SGI_video_sync`) on a context of the
  platform module's own.
- The live mode: counts the refreshes and signals the guest's vblank on every n-th. The divider
  is pure and tested.

**The cap as a divisor.** n = round(refresh / cap), the cap shown as refresh / n: on 120 Hz, 30 is
every 4th refresh, 40 every 3rd, 60 every 2nd. With Even frames on, the settings offer only
divisors; n is computed again when the period changes (Android, another monitor).

**A frame that misses.** With the vblank every n refreshes, a late frame waits a whole guest
period (2n refreshes). Better: the vblank every refresh and the presentation interval n through
the adaptive vsync hook (the guest takes 1 to 3), so a late frame is held n + 1. That needs the
game's own refresh rate (60, from `VdQueryVideoMode`, in its flip scheduling) to be the display's.

**The backend's present.** Swap interval 1 (the CVDisplayLink wait on macOS) and a frame queue of
one: guest and backend follow the same refreshes, so each frame stays n refreshes while the
backend's work fits in one.

**Latency.** Expected the same or lower (the queue waits caused by drift go away); measured as
swap-to-screen against today's, at most one refresh more.

**VRR displays.** No lock: vsync on and a cap just under the refresh (refresh × (1 − refresh /
3600), as Demon's Souls does), on the timer; a choice of the user's, since detecting VRR is not
reliable on every platform.

**Steps**, each measured with the even-frames count:
1. The adaptive vsync under the cap (`hooks/adaptive_vsync.h`): the cap of 30 holds 30.0 fps at
   rest and never more than 30 in a fight.
2. The SDK's external vblank, driven by a host thread at the timer's rate: the guest's frame
   times unchanged, the watchdog taking over when the signals stop.
3. Android's Choreographer source and the divider: at 30, 40 and 60, 95 % or more of the frames
   held 4, 3 and 2 refreshes (today 68 % at 40, 75-80 % at 60), the same presented rate, latency
   at most one refresh more.
4. The presentation interval n: in a fight, frames held 2n mostly replaced by n + 1.
5. macOS, Windows, Wayland and X11, each with the same measure (only Wayland and Android have
   presentation times today; the others get them first).
6. The settings: "Even frames" and "VRR".

**Risks.** The game's adaptive vsync and its fixed 60 Hz in the flip scheduling; refresh rate
changes and monitor moves; display sleep (the watchdog); the compositor's extra refresh in a
window; Choreographer's looper thread; NVIDIA without OML on X11; the SDK patch coordinated with
ReXGlue.
