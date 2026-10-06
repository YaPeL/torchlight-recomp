# Achievement services other than Steam (research, 2026-10-06)

Document only: nothing here is implemented or planned. The recomp offers two sets today
(achievements-xbox.md): the Xbox 360 set, kept by the runtime's own achievement manager, and the PC
set, kept locally (`<data folder>/achievements/<profile>.state`) with an optional, separately gated
Steam backend (achievements-steam.md). The question was whether another service could carry the PC
set or the Xbox set.

## RetroAchievements

- What it is: community-made achievement sets for emulated games, checked by the `rcheevos` library
  against the emulated machine's memory each frame, with an account and a public profile.
- Systems: about 54 as of March 2026; Xbox 360 is not one of them (no emulator integration is
  maintained for it).
- Standalone support (games that call RetroAchievements' API directly, without an emulator) exists
  but is approved case by case and excludes this project explicitly: "Decompilations,
  recompilations, and unofficial ports cannot receive standalone support." It also asks for games at
  least 10 years old with no non-bugfix updates in the last 5 years, a Hardcore mode that blocks
  cheats, and a set designed by a RetroAchievements developer.
- Conclusion: not available for a static recompilation, today. If the rule ever changes, the set
  would be theirs (designed by their developers), not PC's or Xbox's lists.
- Sources: [standalone support](https://docs.retroachievements.org/general/standalone-support.html),
  [xenia issue on RetroAchievements](https://github.com/xenia-project/xenia/issues/2219).

## GOG Galaxy

- Achievements are defined by the game's developer in GOG's Developer Portal and work when the user
  owns the game on GOG and is signed in to Galaxy.
- Torchlight's GOG product belongs to its publisher; a third-party executable cannot register or
  write that product's achievements.
- Conclusion: not usable.
- Source: [GOG Galaxy SDK, statistics and achievements](https://docs.gog.com/sdk-stats-and-achievements/).

## Epic Online Services

- Free for any developer and any storefront, achievements included; a product is created in Epic's
  developer portal.
- It would have to be a new product of this project, not Torchlight's, carrying a list we define:
  unofficial, separate from any player's existing progress, and publishing the game's name and
  achievement texts under our account, which conflicts with the project's rule of not shipping game
  content.
- Conclusion: technically possible, not advisable.
- Source: [Epic Online Services licensing](https://onlineservices.epicgames.com/licensing).

## Trackers (Exophase, TrueAchievements and similar)

- They read achievements from the platforms (Steam, Xbox, PlayStation, ...); they offer no API for a
  game to write unlocks.
- Conclusion: not a target; they would show the Steam unlocks if the Steam backend writes them.

## Xbox network

- Writing Xbox achievements needs a title registered with Microsoft and an Xbox sign-in; the Xbox
  360 title's services are not reachable from the recomp.
- Conclusion: not usable. The Xbox set stays local, in the runtime's achievement manager.

## Summary

| Service | Usable | Why |
| --- | --- | --- |
| Local (today) | Yes | Both sets, no account |
| Steam (gated, today) | Only with the player's own Steam copy | Writes PC's official achievements; see achievements-steam.md |
| RetroAchievements | No | Recompilations are excluded; no Xbox 360 support |
| GOG Galaxy | No | Achievements belong to the publisher's GOG product |
| Epic Online Services | Not advisable | Would be an unofficial new product with our own list |
| Trackers | No | Read-only |
| Xbox network | No | Requires Microsoft title registration |

The local sets remain the default. If a public profile is ever wanted, the realistic options are
the existing Steam backend (for players who own Torchlight on Steam) or an export of the local state
(for example a page or a file listing unlocks and dates), which needs no third party.
