# PC achievement transport, safety and validation

The service uses the original 66 case-sensitive IDs and 28 account stat keys. Current-character deaths/gold are inputs to gameplay predicates; they are not submitted as lifetime stats. Xbox numeric IDs/gamerscore never enter this transport. Steam AppID **41500** is recovered from the PC restart call: immediate `0xA21C` before `SteamAPI_RestartAppIfNecessary`, PC VA `0x005F8792`.

## Build and runtime

The ordinary build uses an unavailable native adapter. No independently licensed Steamworks SDK is installed in this environment, so the SDK-enabled branch is prepared but **not compiled or live-validated**. Nothing loads or links `reference/pc/steam_api.dll`. Supply the Valve SDK externally with `-DSTEAMWORKS_SDK_ROOT=/path/to/licensed/sdk`; it must contain `public/steam/steam_api.h` and the Linux64 redistribution library. Reference directories are rejected. This campaign targets the existing Linux host; other platform Steam library discovery remains future work.

`TORCHLIGHT_STEAM_WRITES` is **OFF** by default: native SetAchievement, SetStat and StoreStats calls are excluded by preprocessing. Building with the option ON merely makes mutation code available; all runtime/identity gates still apply. No SDK-enabled or write-enabled build was run tonight.

Runtime flags:

- Default `--pc_achievements=off` keeps native achievement hooks inactive.
- `--pc_achievements=local --pc_achievement_profile=NAME` runs deterministic local state only.
- `--pc_achievements=steam-dry-run --pc_achievement_profile=steam_ID --pc_steam_account=ID` permits Steam initialization and reads, where ID is the expected SteamID64. Writes remain disabled even if the separate write flag is supplied.
- Unlock notifications (the in-game toast, see achievements.md) show in `local` and `steam-dry-run`. In `steam` mode they are off by default, since the Steam overlay shows its own; `--pc_achievement_notifications_with_steam=true` turns ours on too.
- Future controlled mutation additionally requires mode `steam`, `--pc_steam_writes=enabled`, the compile gate, and successful identity/read/persistence checks. None of these mutation configurations was executed.

State is the player's data: `achievements/<profile>.state` in the user data folder (`platform::DataDir()`; on Linux `~/.local/share/TorchlightRecomp/achievements/`), passed to `achievements::Install` by the app; until 2026-10-05 it lived in the repository's ignored `out/achievements`. Canonical paths containing `reference` are rejected. Bad existing state is never overwritten. Native mode is opt-in pending local gameplay acceptance. A checkpoint runs before accepted window-close requests (the SDK then hard-exits and bypasses OnShutdown); normal teardown also flushes. Non-unlock counters checkpoint every ten seconds. Abrupt termination can lose recent uncheckpointed progress. On Linux the format flushes the temporary file with fsync, renames it, then fsyncs the parent directory before reporting persistence success. Windows replacement/durability semantics remain unvalidated; native SDK discovery currently targets Linux64.

## Lifecycle and safety checks

The adapter uses the supported Steamworks C++ interfaces and callbacks, not recovered PC vtable slots. Steam itself supplies active AppID via GetAppID, account identity via GetSteamID, login state, and app subscription. Installation directory is never identity evidence. Both transport and native mutation methods recheck AppID/account/login/subscription; the expected nonzero account must match the persisted `steam_ID` profile. Identity transitions are logged. Wrong identity prevents reads/reconciliation/writes; an old account's callback cannot acknowledge another account's batch. [Steam identity API](https://partner.steamgames.com/doc/api/ISteamUtils), [Steam user API](https://partner.steamgames.com/doc/api/ISteamUser), [app subscription API](https://partner.steamgames.com/doc/api/ISteamApps).

After initialization the backend requests stats and processes the receive callback, then reads all 66 achievement states and all 28 account stats transactionally. Old SDKs with RequestCurrentStats use that async request. Current SDKs may preload stats and omit the deprecated method; the adapter handles that API shape and still requires every read to succeed. This template compatibility has not been established by compilation against an installed SDK. Callbacks are pumped after the retained guest update `0x821EF318`, under the achievement runtime mutex. Shutdown releases Steam only if initialization succeeded. [Steamworks stat lifecycle](https://partner.steamgames.com/doc/api/ISteamUserStats).

Writes require the compile gate, explicit runtime gate, matching account/profile, active AppID 41500, login/subscription, successful full read and successful pre-write journal save. Set* calls themselves are gated: avoiding StoreStats alone is insufficient because Steam can flush cached mutations on exit. Neither ClearAchievement nor ResetAllStats is part of this adapter. [Steam achievement implementation guidance](https://partner.steamgames.com/doc/features/achievements).

Dry-run logs stable sorted plans:

```text
WOULD SetAchievement(PET_SEND_TO_TOWN)
WOULD SetStat(STAT_FISH_CAUGHT, 12)
WOULD StoreStats()
```

No corresponding Set* or Store call is made. Repeated identical plans are not spammed; pending state is not falsely acknowledged. Steam-unavailable mode retries initialization while keeping local gameplay progress. Request/read failures retry after five seconds. Request timeout is ten seconds. A Store result is acknowledged only by its matching success callback, not by StoreStats returning true.

## Conservative pending/reconciliation model

Local profile state version 2 persists absolute stat values, unlocks, actual earned additive deltas, and an in-flight/uncertainty marker. Character snapshots and threshold force-completion do not append deltas. Class win counters represent first-win booleans and merge by maximum. Max damage/depth and forced minimum values also merge by maximum. For additive counters, a complete Steam read forms `max(local absolute, remote + unacknowledged earned deltas)`, with saturating int32 arithmetic; it never calls Add or replays a gameplay event. Repeating a read does not append a delta. Remote unlocks are unioned with local unlocks and account predicates evaluated. Old v1 state migrates as absolute minima with zero inferred deltas: offline additive history cannot be recovered from that format and is not guessed. A generic `local` profile cannot be rebound/exported to a Steam account.

Before any native mutation, the in-flight marker is saved. The batch retains a snapshot of its deltas. On a successful Stored callback only those snapshot deltas are removed, preserving progress earned while the batch was in flight. Confirmed remote state advances to the accepted absolute batch. Explicit transient Stored failures preserve deltas and the durable uncertainty marker, and retry the same absolute targets within the current process; they do not add a delta onto Steam's already-modified client cache.

A partial Set failure, a false Store return, a missing store callback after sixty seconds, or startup with an in-flight marker makes acceptance uncertain. The backend permits read-only reconciliation but blocks subsequent writes and **does not add the uncertain deltas to remote stats**. It never assumes a network timeout means rejection. This intentionally leaves crash-window progress unresolved instead of double-counting. An account transition while a store is pending, or a schema/invalid-parameter rejection, also leaves a durable block. Even a definitive rejection retains the marker until a successful retry: the Steam client cache may auto-flush after exit. Manual journal review against current server values is required; no automatic reset or resend is supplied. A failed post-ack journal save blocks further writes in that session.

Remaining production limitations: hardware/filesystem durability guarantees and sudden power-loss recovery have not been fault-injection tested; concurrent devices advancing the same account after a read are not covered by a distributed transaction; a Steam ID/account switch during initialization has not been live-tested; SDK callback/API compatibility is uncompiled. Existing backend tests establish deterministic state behavior, not production Steam readiness. Keeping writes disabled is the chosen behavior until these are reviewed.

## Automated validation

The fake API, never the native Steam client, exercises unavailable Steam and later recovery, all identity failures, profile mismatch, request failure/timeout, callback failure and retry, complete/incomplete reads, already-unlocked and duplicate events, deterministic dry-run, SetAchievement/SetStat plans, successful store acknowledgment, explicit failure retry, partial Set/false Store uncertainty, store timeout, wrong/duplicate callbacks, schema rejection, account switching, persistence failure, compile-capability denial, events during in-flight store, and uncertain restart. Local persistence tests cover journal round-trip and v1 migration. No test initializes Steam or changes a live account.

## Prepared manual procedure — separate future live-write ticket

1. Compile with a separately licensed supported SDK and native writes still OFF. Verify the actual SDK branch builds, the library is independent of reference/pc, and current callbacks/read behavior pass. Launch through the legitimate Torchlight Steam app context. Record logged identity: AppID 41500, the expected SteamID64, logged in and subscribed. A path or steam_appid.txt alone is not sufficient.
2. Use `steam-dry-run` with the matching account-scoped profile. Confirm all 66 achievement reads and all 28 stat reads succeed. Save a read-only before-state report and local journal backup. Resolve any old in-flight marker through manual review first; do not delete evidence of uncertain submissions.
3. Choose exactly one currently locked controlled achievement, preferably PET_SEND_TO_TOWN. If it is already unlocked, select another supported milestone or an authorized development account. Do not clear/reset a live account. Validate the one trigger locally first. Inspect the dry-run plan; stop if other achievements or unexpected stats are pending. Review incidental stat changes explicitly rather than hiding them.
4. Only after read-only acceptance, build with `TORCHLIGHT_STEAM_WRITES=ON` and deliberately use `--pc_achievements=steam --pc_achievement_profile=steam_ID --pc_steam_account=ID --pc_steam_writes=enabled`. Reverify the Steam identity and the batch before generating one gameplay trigger. A first test should use a profile whose desired state was reconciled and contains no unrelated pending awards.
5. Expected operations: one SetAchievement for the selected original ID, any previously reviewed incidental SetStat operations, one StoreStats, then the matching successful UserStatsStored callback. Confirm the pending journal was saved before calls and acknowledged afterward. On partial acceptance, failure without a definitive callback, wrong identity or timeout, stop writes and retain the journal.
6. Perform a new read-only session and verify the server achievement plus reviewed stat values against the before-state. A success return from SetAchievement or StoreStats alone is not post-write verification.
7. Retest duplicate delivery without clearing achievements: it should produce no additional achievement write. Resetting a disposable **local** test profile is supported by Service::Reset; do not reset the Steam account or reuse that reset profile for export. Live unlock reset is intentionally unsupported. Reuse an already-unlocked achievement only for idempotence/read verification; use another authorized locked achievement/account for a new positive test.

No step of this live procedure was executed during the campaign. ACH-006 is restricted to SDK compilation and read-only integration validation; it does not authorize any of steps 4–7 involving live mutation. ACH-005 did not initialize Steam or alter transport/mutation gates.
