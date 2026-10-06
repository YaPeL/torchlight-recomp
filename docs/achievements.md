# PC-compatible achievements research

Initial research date: 2026-10-03; implementation campaign: 2026-10-04. The original research needed no proof of concept. Implementation checkpoints and current coverage follow the original evidence.

## Decision

A PC-compatible implementation is feasible, but **replacing only the Xbox platform write backend is insufficient**. The guest retains the PC 66-entry catalog and several PC event hooks; it exports only 12 Xbox awards through a separate enum-to-platform-ID mapping. The PC final-boss rules and Steam stat collection/persistence need restoration. Use the retained gameplay events and character counters as inputs to a PC rule layer, then give that layer a native backend.

This is analogous to the renderer replacement only at the architectural boundary: platform services can be replaced without changing the rest of gameplay. Achievement rules above that boundary differ between the builds and cannot be supplied by translating Xbox unlock IDs.

The original research session could not create its branch because `.git` was read-only. On 2026-10-04 the authorized implementation campaign created `feature/pc-achievements` and preserved this research as its first commit. All campaign work stays on that branch; `main` is not modified or merged.

## Sources and confidence

The authoritative PC sources are `reference/pc/Torchlight.exe`, `steam_api.dll`, `BuildVer.txt`, and the directory of `Pak.zip`. They were opened only for reading. BuildVer decodes as `17685_201001261433`. PC addresses below are preferred-image x86 VAs, image base `0x00400000`, not file offsets. Guest addresses are PPC VAs and identify `sub_XXXXXXXX` in `generated/default/torchlight_recomp.*.cpp`.

Reference fingerprints:

| File | SHA-256 |
| --- | --- |
| Torchlight.exe | `a783a60725abd20ba0b5cd64f210f9b5baa08f07a0222a56c8f1e5e182b3eec6` |
| steam_api.dll | `8640d75aa8024df84d26101dc11d6f70fd431cff3810a397ab1633283281ecf3` |
| Pak.zip | `5e8acc92af572bd441e476cdef8dc89f4e6b76d3b93df9fb5a5babb565528eee` |

Guest verification used generated PPC instruction comments and the existing ignored loaded-memory artifact `docs/bringup-artifacts/nvidia-minimap/guest-image.bin`. Its base is `0x82000000`; its code call targets agree with the generated functions. Mutable data in that artifact is a snapshot, **not a canonical initial state**. Static literals and the Xbox mapping were checked against generated address construction. Xbox English metadata was decoded from its XDBF at `0x83590480`, using the SDK's documented packed layout.

The PC catalog was recovered from all 66 calls to its achievement constructor `0x005F3B90` in `0x005F4C30`. These are actual registered names, not a list guessed from string presence. Its corresponding guest constructor is `0x823D96F0`, called 66 times by `0x823D9BB8`. All 66 names, stat selectors, and maximum thresholds match. The 36 explicit-event names also match, in the same enum order, e.g. guest static initializer `0x829D7A28` and PC initializer `0x00777D55` onward. The matrix lists the exact case-sensitive names passed to Steam; display names/localizations are not needed to establish equivalence.

No proprietary binaries, asset dumps, disassembly, or decompiled code are included in this document or added to version control. Facts, addresses, identifiers and paraphrased predicates are recorded. No renderer, OGRE, shader, cache, generated source or SDK files were modified. The initial research/ACH-002–004 did not launch the game; the separately recorded ACH-005 run below did.

## Existing Xbox boundary and all award paths

There are three different namespaces: 66 catalog entries, 36 explicit gameplay-event enums, and 12 Xbox platform IDs. Never treat the catalog index or explicit-event enum as an Xbox ID.

| Layer | Evidence | Behavior |
| --- | --- | --- |
| Catalog | guest `0x823D9BB8`; PC `0x005F4C30` | Builds 66 achievement objects with name, stat selector, range and completed flag. |
| Event lookup | guest `0x823DB938`; PC `0x005F4360` | Looks up one of 36 event enums by its registered name. Counter achievements remain in the catalog but are not these enums. |
| PC-style completion | guest `0x823D9930`; PC `0x005F3D10` | Sets completion and queues the achievement object. Guest's force path lacks PC's writes that raise a selected stat to its threshold. |
| Threshold completion | guest `0x823D9860`; PC `0x005F39D0` | Compares stat with max threshold using >=; selector 30 means explicit completion. Retention of this code does not prove live counter collection. |
| Xbox award routing | guest `0x82375500` | Rejects enums >=36; table at `0x820A9850` maps only 12 enums to IDs 1–12. All other entries are -1. |
| Pending Xbox state | `0x83558248`, `0x835582DC`, `0x835582D8` | Twelve pending words, dirty bitmask, asynchronous-write flag. Initialization: `0x82372618`; clearing/reset: `0x82375420`. |
| Submission | `0x82193228`, called at `0x8219487C` in `0x821945B0` | Builds `{user_index, achievement_id}` entries, submits batch, waits/polls overlapped completion, clears submitted bits on success. Full-title/storage state gates this path. |
| Platform wrapper | `0x8287D910` | XUserWriteAchievements-style wrapper: count in r3, entry pointer r4, overlap r5. Calls `__imp__XMsgStartIORequest` at `0x8302647C`, app `0xFB`, message `0x000B0008`, 8-byte `{count, pointer}` request. |
| SDK backend | `rexglue-sdk/src/kernel/xam/apps/xgi_app.cpp`, case B0008 | Reads big-endian request, entry stride 8, ID at +4, calls KernelState::UnlockAchievement. The SDK achievement manager persists Xbox IDs and sends notifications. |
| Metadata/storage | SDK `src/system/kernel_state.cpp`, LoadAchievementsData; `src/system/achievement_manager.cpp` | Loads title XDBF's 12 awards; local Xbox unlock store is separate from guest PC-style completed objects and Steam account stats. |

All direct calls to `0x82375500` in the generated guest were enumerated:

| Caller / call site | Event enum(s) | Xbox ID(s) | Predicate |
| --- | --- | --- | --- |
| `0x82296AD8` / `0x82296B54` | 2 | 1 | Pet town-send branch, guarded by pet byte +508 == 0. |
| `0x82287E78` / `0x82287EAC` | 3 | 2 | Fish-feed routine. Additional PC-style mimic and permanent events remain in the same routine. |
| `0x822D7298` / `0x822D74C4` | dynamic 7–13 | 3,4,5,6,7,9,10 | Boss health test, then unit-name matching: BOSS1, LICH, ROOTGOLEMBOSS, EMBERCOLOSSUS, LAVATROLLBOSS, MEDEA, ALRIC_EVIL. FIRSTHENCHMEN is skipped. ORDRAK takes a separate path with no PC completion rules. |
| `0x82361C78` / `0x82361E40` | 32 | 8 | Successful enchantment; item +792 equals 5. |
| `0x82361C78` / `0x82361E70` | 33 | 11 | Successful enchantment; item +792 equals 10. |
| `0x823CEED0` / `0x823CF45C` | 13 | 10 | Additional Alric-related completion event. Preserve/audit this alternate path rather than assume boss death is the only source. |
| `0x822A0570` / `0x822A0890` | 6 | 12 | Fame rank reaches end of fame table. |

These seven call sites account for all twelve routable IDs; there is one call to the platform write wrapper, from the batch submitter. Boss events take the Xbox queue path without calling guest `0x823D9930`, so intercepting only the retained PC completion queue misses those awards.

Other relevant platform calls:

- `0x8287D8C0` -> `__imp__XamShowAchievementsUI` (`0x8302672C`), called from `0x82353BE8` at `0x82353DD0` and `0x8238CB98` at `0x8238CEF0`. This shows Xbox metadata/UI and needs replacement/suppression in PC mode.
- `0x8287DCE0` polls XGetOverlappedResult-style completion for the write; `0x82882C10` participates in synchronous completion. Preserve completion contracts if bypassing the Xbox queue.
- `__imp__XamUserCreateStatsEnumerator` (`0x8302678C`) appears in wrappers `0x8287DBD0` and `0x8287DB70` (see generated recomp.133.cpp). It is Xbox stats/leaderboard enumeration, not Steam achievement progression.
- `0x8287DA00` / `0x8287DA90` send XGI context message B0006; adjacent property handling is B0007. `0x822D7520` sends leaderboard/stat-shaped character events through `0x82886D00`, with per-character counters and full-title gates. These do not automatically update the retained PC stat manager.
- The generated import list has no direct XUserWriteAchievements/Steam imports and no direct XamUserCreateAchievementEnumerator import. The SDK contains an achievement enumerator implementation, but its mere existence does not establish a game-side caller. SDK metadata loading and its overlay are separate host-side paths.

## PC stats, storage and Steamworks evidence

PC `CSteamStats` RTTI and the UserStatsReceived/UserStatsStored callback types are present. Imports include SteamAPI_Init, Shutdown, RunCallbacks, RegisterCallback, UnregisterCallback, SteamUser, SteamUserStats, SteamUtils and SteamApps. SteamRemoteStorage is also imported for character cloud saves; it is not evidence of achievement storage.

| Operation | PC binary evidence |
| --- | --- |
| Acquire user stats | IAT `0x00A5D4F0` is SteamUserStats; initialization at `0x005F7BE0`. |
| RequestCurrentStats | `0x005F7835` obtains interface, calls vtable +0 and records request flag only on successful return. |
| Receive account stats | Callback registrations at `0x005F6FCA` and `0x005F701A`; typed callback RTTI supports UserStatsReceived/UserStatsStored. `0x005F6CF0` reads named integer/float stats. |
| GetStat | In `0x005F6CF0`, named stat reads use vtable +0x08 for integer and +0x04 for float. |
| SetStat | `0x005F6C00` sends named values through vtable +0x10 for integer, +0x0C for float (this old binary's MSVC overload order). |
| GetAchievement | `0x005F44B0`, call at `0x005F45B6` through vtable +0x18, name plus completion output. Parses account achievements before reconciling local objects. |
| SetAchievement | `0x005F4180`, call at `0x005F41F8` through vtable +0x1C, one name from pending queue. |
| StoreStats | `0x005F4212` / `0x005F421F` for achievements; `0x005F6C99` / `0x005F6CAB` for stats, vtable +0x24. Achievement queue is freed only after a successful StoreStats return. |
| Stat mutation | `0x005F7130` assigns integer stat; `0x005F71D0` adds a delta. They check stat type, remote/account eligibility, active character context and its +0x2428 field ==1; save prior values and mark dirty. The +0x2428 field's complete meaning remains unresolved: restore its predicate, do not invent a cheat/mod policy. |
| Character-derived values | `0x005F6B10` binds player; PLAYER_DEATHS reads player +0x754, PLAYER_GOLD reads +0x3C4. These are character values rather than account lifetime sums. |

The old interface offsets above describe this executable only. A native backend should use the supported Steamworks interface, not call these old offsets or load the proprietary PC DLL into the recomp.

The 31 stat selectors are below. Steam keys occupy a 40-byte descriptor table; static initializers build its strings, so zero bytes in the PE data section do not mean the keys are absent. Guest corresponding descriptor tables are used by `0x823DC258`/`0x823DC2B0` and `0x823D9860`. The guest retains its vector at stat-manager +36 (global `0x8355A294`), but lacks the PC Steam lifecycle and does not have a demonstrated equivalent of its full mutation/notification flow.

| Selector | Key | Role |
| --- | --- | --- |
| 0 | `STAT_DEATHS` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 1 | `STAT_BREAKABLES` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 2 | `STAT_CRITICAL_STRIKES` | Critical hits (not the exploding-kill criterion). |
| 3 | `STAT_MAX_DMG_DONE` | Largest single damage value; update as a maximum. |
| 4 | `STAT_MONSTERS_KILLED` | Killed monsters. |
| 5 | `STAT_DEEPEST_FLOOR` | Deepest dungeon floor; compare zero-based depth 49/99, not character level. |
| 6 | `STAT_FISH_CAUGHT` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 7 | `STAT_ENCHANTER_FAILS` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 8 | `STAT_RECIPES_MADE` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 9 | `STAT_GAMBLE_COUNT` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 10 | `STAT_QUESTS_COMPLETED` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 11 | `STAT_RETIRED_COUNT` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 12 | `STAT_RETIRED_LVLS_TOTAL` | Sum of retired character levels, not current level. |
| 13 | `STAT_GOLD_COLLECTED` | Lifetime collected gold, not gold in pocket. |
| 14 | `STAT_LEVERS_PULLED` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 15 | `STAT_TOTAL_STEPS` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 16 | `STAT_TOTAL_POTIONS_USED` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 17 | `STAT_TOTAL_ITEMS_SOLD` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 18 | `STAT_DEATHS_HARDCORE` | Hardcore deaths; distinct from regular deaths. |
| 19 | `STAT_TROLL_CHMPS` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 20 | `STAT_POTIONS_PET` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 21 | `STAT_WIN_VANQ` | Class win count for Vanquisher. |
| 22 | `STAT_WIN_ALCH` | Class win count for Alchemist. |
| 23 | `STAT_WIN_DESTROYER` | Class win count for Destroyer. |
| 24 | `STAT_EXPLODE_ENEMY` | Exploding-enemy kills; CRITICAL_STRIKE_ON_DEATH uses this selector. |
| 25 | `STAT_QUESTS_COMPLETED_HATCH` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 26 | `STAT_QUESTS_COMPLETED_GARR` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 27 | `STAT_HORSE_TALK` | Account/gameplay progress counter; exact mutation sites must be restored and checked. |
| 28 | `PLAYER_DEATHS` | Current character death count; not lifetime STAT_DEATHS. |
| 29 | `PLAYER_GOLD` | Current character gold balance. |
| 30 | `NONE` | Explicit-event sentinel; no counter comparison. |

PC total counters are loaded from account stats. Do not aggregate character lifetime totals repeatedly on load, reset all account counters on character switch, or apply a current-character death/gold value to a lifetime threshold. Character counters also survive in the recomp, e.g. `0x822D7520` increments fields at `character + 4*(event+456)`; matching each field to a PC stat needs a field-by-field check.

### Recovered PC stat input sites

The PC character-event dispatcher `0x004D6690` is the counterpart of guest `0x822D7520`. Its jump table at `0x004D674C` supplies the following mapping. These are **character event numbers**, distinct from the 36 achievement-event enums. Each mapped branch adds the dispatcher delta through `0x005F71D0`; deaths additionally update the hardcore counter when character +0x9C5 is nonzero. All events also increment a character-local field at +0x740 + event*4. The guest's local fields start at +0x720 instead; copying PC offsets into guest memory would be incorrect.

| Character event | PC account stat selector/key | PC branch VA |
| --- | --- | --- |
| 1 | 13 / STAT_GOLD_COLLECTED; also checks current gold + delta for the pocket-gold event | `0x004D66D7` |
| 3 | 15 / STAT_TOTAL_STEPS | `0x004D6717` |
| 4 | 10 / STAT_QUESTS_COMPLETED | `0x004D671C` |
| 5 | 0 / STAT_DEATHS; additionally 18 / STAT_DEATHS_HARDCORE | `0x004D66B5` |
| 6 | 4 / STAT_MONSTERS_KILLED | `0x004D66B0` |
| 11 | 1 / STAT_BREAKABLES | `0x004D66D2` |
| 12 | 16 / STAT_TOTAL_POTIONS_USED | `0x004D6721` |
| 14 | 6 / STAT_FISH_CAUGHT | `0x004D6726` |
| 15 | 9 / STAT_GAMBLE_COUNT | `0x004D672B` |
| 16 | 8 / STAT_RECIPES_MADE | `0x004D6730` |

Events 2, 7–10 and 13 have no account-stat branch in this dispatcher. The corresponding guest dispatcher keeps Xbox telemetry and local counters but omits these PC account-stat additions. Restoring the mapping at this shared boundary is a strong candidate for the stat implementation ticket, subject to the PC eligibility gate and verification of each event's caller/delta.

Additional direct PC mutation call sites provide starting points for the remaining counters:

| Selector/key | PC mutation call VA | Recovered operation / remaining qualification |
| --- | --- | --- |
| 3 / STAT_MAX_DMG_DONE | `0x00497B6E` | Integer assignment of damage; preserve the surrounding maximum comparison and conversion. |
| 5 / STAT_DEEPEST_FLOOR | `0x004190EF`, `0x0041A232` | Assignment; the first site compares depth with stored value before updating. |
| 6 / STAT_FISH_CAUGHT | `0x004D754C` | Additional add path; trace against character event 14 to avoid double-counting. |
| 7 / STAT_ENCHANTER_FAILS | `0x00577768` | Add on failed enchantment, separate from the explicit item predicate. |
| 11 / STAT_RETIRED_COUNT | `0x005774F2` | Add 1 when retiring. |
| 12 / STAT_RETIRED_LVLS_TOTAL | `0x0057750A` | Add retiring character level (+0xF0 in PC). |
| 14 / STAT_LEVERS_PULLED | `0x004DF4F9` | Add; caller eligibility and duplicate activation need checking. |
| 17 / STAT_TOTAL_ITEMS_SOLD | `0x004D667B` | Add 1 in the sale notification routine. |
| 19 / STAT_TROLL_CHMPS | `0x0048E590` | Add; recover the complete monster/champion qualification. |
| 20 / STAT_POTIONS_PET | `0x004B50AF` | Add; verify pet and potion qualification. |
| 21 / STAT_WIN_VANQ | `0x005EBD9F` | Add 1 on a qualified class-win branch; recover companion class branches and quest eligibility. |
| 24 / STAT_EXPLODE_ENEMY | `0x004A7D4F` | Add; verify exploding-death qualification. |
| 25,26 / STAT_QUESTS_COMPLETED_HATCH, STAT_QUESTS_COMPLETED_GARR | `0x00595889` | Shared add-1 call after separate quest/name tests select 25 or 26. |
| 27 / STAT_HORSE_TALK | `0x00598B8F` | Add; trace which conversation action qualifies. |

These addresses identify actual stat-mutator calls, not complete proofs of the surrounding gameplay predicates. STAT_CRITICAL_STRIKES and the other class-win producers still need tracing. The matrix's “no live PC stat producer established” refers to the recomp's account-stat pipeline, not absence of the above PC reference sites.

Two concrete gaps prevent treating retained data as working PC support:

1. PC `0x005F3D10` raises a selected stat to the achievement threshold before forcing completion; guest `0x823D9930` simply queues/marks it without that stat write. Threshold setter behavior is not preserved by changing the transport.
2. Guest `0x821EF318` clears the PC-style pending vector at achievement-manager +12 once both managers exist. The retained queue is not delivered to Steam, and boss events bypass it. Retained getter/evaluator/registration code is evidence of reusable structure, not end-to-end unlock semantics.

## PC-centric equivalence matrix

Classification refers to the published Xbox set, not whether a dormant PC catalog entry happens to survive in the Xbox binary. **Exact Xbox equivalent** means the same gameplay event/predicate; Xbox storage/sign-in gates are discussed separately. **PC-only** means no award in the twelve-ID Xbox set, even when the guest retains its catalog or event hook. No confirmed differently-conditioned Xbox equivalent or unidentified PC catalog ID was found. Some PC-only predicates and counter mutation paths remain unresolved, called out explicitly below.

Counts: 66 PC entries; 12 exact Xbox equivalents; 54 PC-only; 0 confirmed Xbox equivalents with different semantics; 0 unresolved identities/classifications. All 12 Xbox awards have a PC equivalent; **no Xbox-only achievement is evidenced**. Do not invent an Xbox-only exclusion list. Exclude Xbox numeric IDs, gamerscore, platform queue/UI and Xbox persistence from the PC implementation, not twelve otherwise valid PC gameplay milestones.

`C(n,t)` means the retained catalog/evaluator for stat selector n and max threshold t; its presence does not establish collection or delivery. `E(n)` means explicit gameplay-event enum n. Every row gives the PC catalog constructor call VA so the registered name/threshold can be rechecked independently. All rows are also registered by guest `0x823D9BB8`.

| PC achievement (constructor call VA) | Xbox equivalent | Semantic equivalence / classification and PC condition | Existing recomp trigger | Required work |
| --- | --- | --- | --- | --- |
| `TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL` (`0x005F4D0C`) | — | **PC-only**. Enter the first dungeon through the level-name predicate (PC 0x417021–0x417039). | E(0): 0x82217DA8 / 0x822199F0 | Deliver retained PC completion through native backend. |
| `BREAKABLES` (`0x005F4D73`) | — | **PC-only**. `STAT_BREAKABLES` >= 1,500. | C(1,1500); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `BEAST_OF_BURDEN` (`0x005F4DD7`) | — | **PC-only**. Fill pet inventory: PC 0x4D8058 compares occupied and available slots. | E(1): No direct event caller recovered | Restore/audit PC pet-inventory-full predicate and input slots. |
| `PLAYER_GOLD_COLLECTED` (`0x005F4E3F`) | — | **PC-only**. `STAT_GOLD_COLLECTED` >= 250,000. | C(13,250000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `PLAYER_GOLD_IN_POCKET` (`0x005F4EA7`) | — | **PC-only**. At gold gain, current gold + gain >= 100,000 (not lifetime gold). | E(35): 0x822D7520 / 0x822D77B0 | Remove dependency on Xbox leaderboard gating; preserve pocket-gold check. |
| `CRITICAL_STRIKE_ON_DEATH` (`0x005F4F0C`) | — | **PC-only**. `STAT_EXPLODE_ENEMY` >= 25. | C(24,25); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `MAX_DMG_DONE` (`0x005F4F74`) | — | **PC-only**. `STAT_MAX_DMG_DONE` >= 10,000. | C(3,10000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `REACH_LVL_50` (`0x005F4FD9`) | — | **PC-only**. `STAT_DEEPEST_FLOOR` >= 49; dungeon depth, not character level. | C(5,49); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `REACH_LVL_100` (`0x005F503E`) | — | **PC-only**. `STAT_DEEPEST_FLOOR` >= 99; dungeon depth, not character level. | C(5,99); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `PET_TRAINER` (`0x005F50A2`) | — | **PC-only**. Pet spell-learning event at PC 0x58F5D6; exact spell/slot qualification needs further tracing. | E(31): No direct event caller recovered | Trace PC spell predicate fully; restore missing event hook. |
| `PET_MIMIC` (`0x005F5106`) | — | **PC-only**. Feed transformation whose target name is PET MIMIC. | E(30): 0x82287E78 / 0x82287F08 | Deliver retained PC completion through native backend. |
| `ENCHANTER_SUCCESS_5` (`0x005F516A`) | ID 8 | **Exact Xbox equivalent**. Successful enchantment when this item's enchant count == 5. | E(32): 0x82361C78 / 0x82361E40 | Route PC ID to native backend; retain event predicate. |
| `ENCHANTER_SUCCESS_10` (`0x005F51CE`) | ID 11 | **Exact Xbox equivalent**. Successful enchantment when this item's enchant count == 10. | E(33): 0x82361C78 / 0x82361E70 | Route PC ID to native backend; retain event predicate. |
| `ENCHANTER_FAILURE_FIRST` (`0x005F5232`) | — | **PC-only**. Failed enchantment on an item with zero prior enchantments; not simply first lifetime failure. | E(29): 0x82361C78 / 0x82361E10 | Deliver retained PC completion through native backend. |
| `ENCHANTER_FAILURE_20` (`0x005F5297`) | — | **PC-only**. `STAT_ENCHANTER_FAILS` >= 20. | C(7,20); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `GAMBLER_20` (`0x005F52FC`) | — | **PC-only**. `STAT_GAMBLE_COUNT` >= 20. | C(9,20); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `GAMBLER_50` (`0x005F5361`) | — | **PC-only**. `STAT_GAMBLE_COUNT` >= 50. | C(9,50); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `GAMBLER_100` (`0x005F53C6`) | — | **PC-only**. `STAT_GAMBLE_COUNT` >= 100. | C(9,100); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `MODS_1` (`0x005F542A`) | — | **PC-only**. Loaded mod count >= 1 at startup. | E(26): 0x8231FF28 / 0x82320A68 | Provide PC-equivalent mod-count source; verify dormant guest branch is usable. |
| `MODS_5` (`0x005F548E`) | — | **PC-only**. Loaded mod count >= 5 at startup. | E(27): 0x8231FF28 / 0x82320A94 | Provide PC-equivalent mod-count source; verify dormant guest branch is usable. |
| `MODS_10` (`0x005F54F2`) | — | **PC-only**. Loaded mod count >= 10 at startup. | E(28): 0x8231FF28 / 0x82320AC0 | Provide PC-equivalent mod-count source; verify dormant guest branch is usable. |
| `PLAYER_LEVEL_65` (`0x005F5556`) | — | **PC-only**. Character level >= 65. | E(24): 0x822D1A08 / 0x822D1F7C | Deliver retained PC completion through native backend. |
| `PLAYER_LEVEL_100` (`0x005F55BA`) | — | **PC-only**. Character level >= 100. | E(25): 0x822D1A08 / 0x822D1F98 | Deliver retained PC completion through native backend. |
| `PULLED_LEVERS_100` (`0x005F561F`) | — | **PC-only**. `STAT_LEVERS_PULLED` >= 100. | C(14,100); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `PET_POTIONS_50` (`0x005F5684`) | — | **PC-only**. `STAT_POTIONS_PET` >= 50. | C(20,50); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `PET_SEND_TO_TOWN` (`0x005F56E8`) | ID 1 | **Exact Xbox equivalent**. Pet town-send event; no sale/proceeds requirement in the award branch. | E(2): 0x82296AD8 / 0x82296B54 | Route PC ID to native backend; retain event predicate. |
| `FISH_CAUGHT_50` (`0x005F574D`) | — | **PC-only**. `STAT_FISH_CAUGHT` >= 50. | C(6,50); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `FISH_CAUGHT_100` (`0x005F57B2`) | — | **PC-only**. `STAT_FISH_CAUGHT` >= 100. | C(6,100); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `FISH_CAUGHT_1000` (`0x005F581A`) | — | **PC-only**. `STAT_FISH_CAUGHT` >= 1,000. | C(6,1000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `PET_FEED_FISH_ANY` (`0x005F587E`) | ID 2 | **Exact Xbox equivalent**. Pet fish-feed event. | E(3): 0x82287E78 / 0x82287EAC | Route PC ID to native backend; retain event predicate. |
| `PET_FEED_FISH_PERMANENT` (`0x005F58E2`) | — | **PC-only**. Fish transformation duration matches permanent sentinel. | E(4): 0x82287E78 / 0x82287F2C | Deliver retained PC completion through native backend. |
| `RECIPES_25` (`0x005F5947`) | — | **PC-only**. `STAT_RECIPES_MADE` >= 25. | C(8,25); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `QUESTS_COMPLETE_200` (`0x005F59AF`) | — | **PC-only**. `STAT_QUESTS_COMPLETED` >= 200. | C(10,200); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `QUESTS_COMPLETE_HATCH_50` (`0x005F5A14`) | — | **PC-only**. `STAT_QUESTS_COMPLETED_HATCH` >= 50. | C(25,50); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `QUESTS_COMPLETE_GAR_25` (`0x005F5A79`) | — | **PC-only**. `STAT_QUESTS_COMPLETED_GARR` >= 25. | C(26,25); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `GAMBLE_UNIQUE` (`0x005F5ADD`) | — | **PC-only**. Gambled item passes unique item flag test (54). | E(5): 0x8236B578 / 0x8236B728 | Deliver retained PC completion through native backend. |
| `RETIRE_1` (`0x005F5B41`) | — | **PC-only**. `STAT_RETIRED_COUNT` >= 1. | C(11,1); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `RETIRE_2` (`0x005F5BA6`) | — | **PC-only**. `STAT_RETIRED_COUNT` >= 2. | C(11,2); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `RETIRE_300` (`0x005F5C0E`) | — | **PC-only**. `STAT_RETIRED_LVLS_TOTAL` >= 300. | C(12,300); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `MAX_FAME` (`0x005F5C72`) | ID 12 | **Exact Xbox equivalent**. Fame reaches last fame-table rank. | E(6): 0x822A0570 / 0x822A0890 | Route PC ID to native backend; retain event predicate. |
| `TRAVEL_25000` (`0x005F5CDA`) | — | **PC-only**. `STAT_TOTAL_STEPS` >= 25,000. | C(15,25000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `DIE_500` (`0x005F5D41`) | — | **PC-only**. `STAT_DEATHS` >= 500. | C(0,500); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `KILL_25_TROLL_CHMPS` (`0x005F5DA6`) | — | **PC-only**. `STAT_TROLL_CHMPS` >= 25. | C(19,25); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `KILL_5000_MONSTERS` (`0x005F5E0E`) | — | **PC-only**. `STAT_MONSTERS_KILLED` >= 5,000. | C(4,5000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `KILL_50000_MONSTERS` (`0x005F5E76`) | — | **PC-only**. `STAT_MONSTERS_KILLED` >= 50,000. | C(4,50000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `KILL_BRINK` (`0x005F5EDA`) | ID 3 | **Exact Xbox equivalent**. Kill BOSS1 (Brink). | E(7): 0x822D7298 (dynamic enum) | Restore PC completion at boss-name event; native PC backend. |
| `KILL_LICH` (`0x005F5F3E`) | ID 4 | **Exact Xbox equivalent**. Kill LICH (Overseer). | E(8): 0x822D7298 (dynamic enum) | Restore PC completion at boss-name event; native PC backend. |
| `KILL_ROOT_GOLEM` (`0x005F5FA2`) | ID 5 | **Exact Xbox equivalent**. Kill ROOTGOLEMBOSS. | E(9): 0x822D7298 (dynamic enum) | Restore PC completion at boss-name event; native PC backend. |
| `KILL_EMBER_COLOSSUS` (`0x005F6006`) | ID 6 | **Exact Xbox equivalent**. Kill EMBERCOLOSSUS. | E(10): 0x822D7298 (dynamic enum) | Restore PC completion at boss-name event; native PC backend. |
| `KILL_TROLL_BOSS` (`0x005F606A`) | ID 7 | **Exact Xbox equivalent**. Kill LAVATROLLBOSS (Krag). | E(11): 0x822D7298 (dynamic enum) | Restore PC completion at boss-name event; native PC backend. |
| `KILL_MEDEA` (`0x005F60CE`) | ID 9 | **Exact Xbox equivalent**. Kill MEDEA. | E(12): 0x822D7298 (dynamic enum) | Restore PC completion at boss-name event; native PC backend. |
| `KILL_ALRIC` (`0x005F6132`) | ID 10 | **Exact Xbox equivalent**. Kill ALRIC_EVIL; do not substitute ORDRAK. | E(13): 0x822D7298 and 0x823CEED0 / 0x823CF45C | Restore PC completion at boss-name event; native PC backend. |
| `BEASTSLAYERI` (`0x005F6196`) | — | **PC-only**. Kill ORDRAK, difficulty 0–3 (all four difficulty branches). | E(14): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `BEASTSLAYERII` (`0x005F61FA`) | — | **PC-only**. Kill ORDRAK, difficulty 2 or 3. | E(15): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `BEASTSLAYERIII` (`0x005F625E`) | — | **PC-only**. Kill ORDRAK, difficulty 3. | E(16): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `HARDCORE_VICTOR` (`0x005F62C2`) | — | **PC-only**. Kill ORDRAK, hardcore, difficulty 0. | E(17): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `HARDCORE_HERO` (`0x005F6326`) | — | **PC-only**. Kill ORDRAK, hardcore, difficulty 1. | E(18): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `HARDCORE_CHAMPION` (`0x005F638A`) | — | **PC-only**. Kill ORDRAK, hardcore, difficulty 2. | E(19): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `HARDCORE_GOD` (`0x005F63EE`) | — | **PC-only**. Kill ORDRAK, hardcore, difficulty 3. | E(20): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `SPEEDY` (`0x005F6452`) | — | **PC-only**. Kill ORDRAK with character play time / 3600 <= 8 hours. | E(21): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `SPEED_KING` (`0x005F64B6`) | — | **PC-only**. Kill ORDRAK with character play time / 3600 <= 5 hours. | E(22): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |
| `HAT_TRICK` (`0x005F651A`) | — | **PC-only**. Win with all three classes; cumulative class win counters all > 0. | E(23): 0x823CEED0 / 0x823CF7E0 | Restore class win increments/account counters; retain all-three check. |
| `DRINK_POTIONS` (`0x005F6582`) | — | **PC-only**. `STAT_TOTAL_POTIONS_USED` >= 5,000. | C(16,5000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `SELL_ITEMS` (`0x005F65EA`) | — | **PC-only**. `STAT_TOTAL_ITEMS_SOLD` >= 10,000. | C(17,10000); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `HORSE_TALK` (`0x005F664F`) | — | **PC-only**. `STAT_HORSE_TALK` >= 100. | C(27,100); no live PC stat producer established | Restore PC stat collection, persistence and threshold notification. |
| `PERFECT_VICTORY` (`0x005F66B3`) | — | **PC-only**. Kill ORDRAK with this character's death count == 0. | E(34): Catalog retained; no production event caller recovered | Restore PC ORDRAK predicate and required player inputs; native backend. |

## Differences requiring condition restoration

The most important missing condition block is PC `0x004D8DE0`, corresponding to guest `0x822D7298`. Both match boss names after a health/death check, but the PC routine sends normal bosses to PC completion; the guest routes them to the Xbox mapper. After matching ORDRAK, the PC routine additionally:

- Awards PERFECT_VICTORY when player deaths captured from +0x754 are zero.
- Dispatches difficulty 0,1,2,3 through jump table `0x004D920C`. BEASTSLAYERI is awarded in every branch; BEASTSLAYERII in 2 and 3; BEASTSLAYERIII only in 3. Thus BEASTSLAYERI's predicate includes easy difficulty, irrespective of any interpretation of its label.
- Awards exactly the hardcore achievement for the chosen difficulty, using the captured hardcore flag. It does not award all four hardcore tiers for one hardest-difficulty win.
- Multiplies character elapsed play time by 1/3600 (constant at `0x00A7FE20`) and compares inclusively with 8 and 5 hours (`0x00A68E88`, `0x00A68E94`). Restore the actual play-time input, not wall-clock session uptime.

Guest's ORDRAK branch only calls `0x82374EA0(3)` and exits this routine. The PC-exclusive block is absent. The separate Alric quest/completion function also retains HAT_TRICK, but its class wins must be collected; zero/dormant counters cannot satisfy it.

Other details that matter:

- The PC failure-on-unenchanted-item event survives in `0x82361C78`, but its cumulative failed-enchantment stat is not the same as its explicit ENCHANTER_FAILURE_FIRST event. Five/ten success milestones count enchantments on one item, not global success totals.
- PC has both level 65/100 character events and misleadingly named REACH_LVL_50/100 depth counters. Do not collapse them.
- Maximum fame is checked against the end of the build's fame table. Before claiming content-level parity, compare actual fame data as well as the shared trigger predicate.
- MODS achievements have both constructor entries and startup count branches in the guest; the existing recomp does not thereby gain a PC mod loader or an authoritative PC loaded-mod count.
- Pet mimic and permanent transformation hooks survive, while PET_TRAINER and BEAST_OF_BURDEN have no recovered direct production caller in the guest event lookup xrefs. Their PC caller locations are established; precise qualification/input mapping still needs work.
- Read-time Steam reconciliation in `0x005F44B0` can mark completion from already-earned account achievements. It is a state-load path, not a new gameplay trigger; avoid replaying events or incrementing stats during reconciliation.

## Remaining uncertainty and implementation risks

All 66 registered IDs and their constructor selectors/thresholds are recovered. This investigation is static, not a gameplay validation or a proof of every counter's producer/persistence semantics. Outstanding work is concrete:

1. Complete the surrounding predicates and caller/delta traces for the recovered PC mutation sites, recover the remaining producers, and establish eligibility and save/character-switch behavior. Map them to guest events or fields; the shared character-event dispatcher is a concrete starting point, while fishing requires a duplicate-path audit.
2. Trace PET_TRAINER's spell qualifications and BEAST_OF_BURDEN's occupied/available inventory slots into guest equivalents. Check initial dungeon string and permanent-fish sentinel at the actual data level.
3. Confirm PC stat eligibility field +0x2428 and corresponding guest context predicates, including behavior with mods and cheats. No unsupported policy is inferred here.
4. Check fame-table/content differences and alternate boss/quest completion paths, particularly the duplicate Alric route. Idempotence must prevent duplicate account effects.
5. Determine local/offline persistence scope and Steam account reconciliation. Steam availability must gate submission, not destroy valid earned events. Do not migrate the SDK's twelve-ID Xbox unlock file into PC state automatically.

## Recommended next implementation ticket

**Restore a PC achievement service boundary and the PC final-boss rules, with a local test backend.**

Keep this first ticket bounded; do not claim support for all 66 until the stat producer work is finished. Add an app-owned module with a checked-in factual PC catalog, case-sensitive PC IDs, a neutral event/stat interface, and an idempotent local backend. Use existing app hook facilities and centralized big-endian guest ABI readers; do not edit generated functions or the SDK. Intercept the retained explicit completion path and supplement boss events that bypass it. Restore ORDRAK's difficulty, hardcore, speed and deathless block from the facts above. Disable Xbox award submission/UI for this PC mode while satisfying any guest overlapped completion expectations.

Acceptance criteria:

- Catalog contains exactly the 66 recovered PC names and selector/threshold pairs; no Xbox numeric ID or gamerscore is an output identity.
- Tests cover the four difficulty branches, exact hardcore tier, inclusive 8h/5h boundaries, per-character deathless input, ALRIC_EVIL versus ORDRAK, duplicate events and character switching.
- Pet-send, fish-feed, boss-name, enchant 5/10 and fame inputs reach the neutral boundary; the existing pending-vector clearing path cannot silently discard completed events.
- Local backend persists PC IDs with account/profile scope and preserves events when the eventual Steam backend is unavailable. Document unsupported stat-driven achievements explicitly.
- No renderer/OGRE/shader/cache changes and no PC reference assets or binary code are copied into the repository.

Follow with a separate **restore PC stat producers and all remaining PC event predicates** ticket, then a **native Steam backend and account reconciliation** ticket. The Steam ticket should implement request/receive, read stats/achievements, SetStat/SetAchievement, StoreStats, callbacks and retry behavior against the neutral interface. Connecting Steam directly to XGI B0008 is not an acceptable implementation of the requested PC behavior.

## Reproducing the static checks

Use the existing read-only helpers, writing any private analysis output outside version control:

```sh
objdump -d -Mintel --start-address=0x5f4c30 --stop-address=0x5f6740 reference/pc/Torchlight.exe
objdump -d -Mintel --start-address=0x4d8de0 --stop-address=0x4d9220 reference/pc/Torchlight.exe
objdump -d -Mintel --start-address=0x5f39d0 --stop-address=0x5f3e30 reference/pc/Torchlight.exe
objdump -d -Mintel --start-address=0x5f4180 --stop-address=0x5f4250 reference/pc/Torchlight.exe
python3 tools/guest_re/xref.py docs/bringup-artifacts/nvidia-minimap/guest-image.bin generated/default call 82375500 8287D910
python3 tools/guest_re/xref.py docs/bringup-artifacts/nvidia-minimap/guest-image.bin generated/default call 823D9930 823DB938
rg -n 'DEFINE_REX_FUNC\(sub_(823D9BB8|823D9930|82375500|82193228|822D7298|8287D910)' generated/default
```

Validation for this documentation: all 66 constructor calls and their names/selectors/thresholds were compared; all seven Xbox mapper call sites, twelve table entries and the single write-wrapper caller were enumerated; Steam IAT and virtual call arguments were checked; PC/guest boss branches were compared. This does not replace the acceptance tests above or a later controlled gameplay validation.

## Implementation campaign — ACH-002 checkpoint (2026-10-04)

Added `src/achievements`: exact catalog, neutral rules, resettable local profile persistence, strong guest overrides and independent ABI fixture tests. Enable with `--pc_achievements=local --pc_achievement_profile=NAME`; default `off` preserves the existing runtime until local gameplay validation. State lived at the repository's `out/achievements/NAME.state` until 2026-10-05; it is now the player's data in the user data folder (`~/.local/share/TorchlightRecomp/achievements/NAME.state` on Linux, see achievements-steam.md); canonical paths containing a `reference` component are rejected. Invalid/mismatched existing state disables native mode rather than overwriting it. Explicit unlocks persist immediately; counters checkpoint every ten seconds and on shutdown. A crash can lose uncheckpointed counter progress. No Steam integration is active at this checkpoint.

Guest completion `0x823D9930` is observed after preserving the original call. Mapper `0x82375500` is replaced in native mode before any Xbox queue/overlapped request is created; its ordinary success return is preserved. Xbox UI wrapper is suppressed. ORDRAK is supplemented at `0x822D7298`, preserving the original gameplay call and the original floored-health predicate. Well-established explicit events reaching completion/mapper are supported independently of the Xbox twelve-ID store. The five unresolved PC predicates are suppressed at guest completion ingress until their qualifications are established; their catalog entries and isolated rules remain representable.

Further ABI evidence: PC difficulty is an **integer at shared game context +0x243C**, corresponding to guest +5148 (`0x8234CA14`, SETDIFFICULTY writes in `0x8234BE20`). The PC character **float +0x2C8 is elapsed play time**, corresponding to guest +700: `0x82283398` initializes it, `0x822A3368` restores it from character state +256, and `0x821D3FD8` adds elapsed update delta. Guest death count is +1844 (`0x823DC0E8` and event-5 dispatcher); pocket gold is +940 (`0x822D7788`, `0x823DC0E8`), not the adjacent telemetry fields +912/+916. Hardcore remains +2469. All readers use the centralized big-endian helpers; no PC offsets are used as guest offsets.

Tests exercise all 36 explicit IDs, all 30 counter thresholds, force-completion stat raising, all ORDRAK tiers, exact/just-above five/eight hours, duplicate unlocks, character switching, separate death/gold scopes, save/load, malformed state and profile mismatch. Rule tests do not establish gameplay source coverage: the final matrix below will distinguish hooks from representable rules.

## ACH-003 checkpoint: PC stat collection

The shared character-event hook `0x822D7520` restores selectors 0,1,4,6,8,9,10,13,15,16 and hardcore deaths 18, with context state +5124 ==1. No Xbox title/leaderboard eligibility is used for native progress. The context gate is preserved as a predicate; its broader state-machine meaning is not presented as a cheat/mod policy. Inputs are captured before the original counter mutation, preserving current-gold-plus-delta semantics.

Additional hooks restore failed enchantments (the result of `0x82361C78`), accepted retirement (the successful `0x82361B00` path, capturing level +240 and retired flag +2468 before it may release the character), and maximum applied damage. The latter is at the health-setter call returning to `0x8229CAD4`: nonvolatile r29 is attacker and f30 is applied damage in `0x8229C7B0`, matching PC's block immediately before health modification. The retained flag-28 predicate is queried in a separate PPC context; the caller's registers are preserved. Damage is truncated to an integer and applied as a maximum.

Removed PC additions are restored at the already-qualified UTF-16 comparison call sites: `0x82356904` Horse conversation; `0x82355E90` Male1/Hatch completed quest; `0x82355EAC` Gar completed quest; `0x8229FE60`/`0x8229FE7C` dead champion named Troll/Troll Juggernaut; `0x823CF6A8`/`0x823CF710`/`0x823CF778` qualified Destroyer/Alchemist/Vanquisher win. These compare hooks leave the comparison result and gameplay unchanged. PC `0x005EBC72` through `0x005EBD9F` proves class counters increment only from zero; `ClassWin` follows that predicate and evaluates HAT_TRICK against the three native account counters. Thus alternate Alric delivery cannot increment class wins twice.

Fishing uses exactly the character-event-14 ingress; no extra handler hook is added at the PC-only direct-add address. Guest `0x822D2438` sends that event after successful catch processing, whereas PC `0x004D754C` adds a fish and increments its corresponding character field directly. This avoids restoring the same award input twice. Repeated legitimate catches/steps/quests still increase counters; equal event arguments are not treated as duplicate gameplay occurrences. Unlock delivery, absolute stat assignment, class-win qualification and reconciliation are idempotent.

The app-owned override tests use the actual hooks, a sparse synthetic 4GiB guest arena and doubles for original calls. They verify live boundary routing, preservation of original calls/return values, Xbox suppression, eligibility, retirement pre-quit capture, enchant success/failure, class duplication, named counters and maximum damage. The full executable was also built without starting it.

Unresolved source work remains bounded: deepest-floor field and transition (PC max-set sites established, guest level field not yet confirmed); lever activation subtype/count (PC +0x1F4 ==1 and +0x278 ==0, guest equivalents unconfirmed); pet potion ownership/type qualification (PC requires item flag 33 and pet owner); exploding-death flag (PC sets +0x659 on the qualifying branch); item-sale notification (PC adds one per notification, guest equivalent unconfirmed). These account stats are representable and threshold-tested but not synthesized from guessed guest offsets.

PET_TRAINER's PC branch rejects target types 41/42 and performs the qualified spell-learning call before explicit enum 31; its guest equivalent has not been established. BEAST_OF_BURDEN compares occupancy and capacity of inventory category 0 after the pet-item transfer branch; PC `0x004E5E10` computes category capacity from slot ranges and `0x004E5A30` counts items in that range. Neither a guessed vector length nor the entire pet inventory size is substituted. MODS_1/5/10 await an authoritative PC loaded-mod source. STAT_CRITICAL_STRIKES has no catalog threshold and its producer is still unresolved; it is independent of STAT_EXPLODE_ENEMY. Fame's predicate is wired but fame content parity remains unverified.


## ACH-004 checkpoint: Steam dry-run architecture

See [Steam implementation and manual validation](achievements-steam.md) for the transport lifecycle, identity checks, pending journal, failure cases, SDK limitation and prepared (unexecuted) live procedure. The neutral service remains the sole owner of gameplay semantics. Steam reads/reconciliation never call a gameplay producer. Default native mutation calls are absent at compile time; dry-run additionally gates them before reaching the API. The account profile and expected account must match `steam_<SteamID64>`; generic local profiles cannot silently export to a logged-in account.

The explicit [coverage matrix](achievement-coverage.md) contains all 66 IDs: **54 implemented/unit-tested, 7 partial, 5 blocked**. These counts describe evidenced ingress and deterministic rules, not a claim that a live playthrough has validated 54 achievements. All 66 definitions and all counter predicates are representable. Blocked BEAST_OF_BURDEN, PET_TRAINER and MODS_1/5/10 are disabled at guest ingress; reading an already-unlocked legitimate Steam achievement remains allowed.

Validation: full `torchlight` build succeeds; all 15 registered CTest suites pass, including the four achievement suites (rules/persistence, ABI fixtures, actual bridge overrides with original-call doubles, and fake Steam transport). No game process or Steam account was used. No live read or write was attempted; the SDK-enabled adapter remains uncompiled because no separately licensed SDK is installed. Sanitizer results and final repository audit are recorded in the campaign report.

## Next implementation ticket: ACH-005 — local gameplay validation and remaining PC predicates

Accept this implementation for further **local** validation, not a first live Steam write yet. Start from `feature/pc-achievements`; do not merge or modify main. Run native mode with a disposable local profile and validate first dungeon, pet events, enchantment, retirement, account-vs-character counters, normal bosses and all ORDRAK tiers against guest save/load behavior. Confirm that suppressed Xbox queue/UI do not affect unrelated gameplay. Prove fame table parity from PC assets read-only. Trace the remaining five counter producers, PET_TRAINER, category-0 pet capacity and loaded-mod count; use the specific missing input listed above rather than inserting PC offsets into guest structures. Extend source-specific bridge fixtures for each newly established producer. Acceptance requires evidence-backed updates to the 66-row coverage matrix and no proprietary/reference changes.

In parallel scope of a later ACH-006 ticket: supply a separately licensed Steamworks SDK, compile the adapter against its actual headers/library, validate runtime callback pumping and all 66 reads on the verified AppID/account, and inspect the dry-run plan before enabling one controlled achievement. Complete the read-only checks and gate review in achievements-steam.md first. Native production mutation readiness is deliberately not claimed by fake transport tests.

## Campaign completion report

Completed: native platform-neutral service, exact catalog/metadata, local resettable profile persistence, retained explicit-event hooks, Xbox mapper/UI suppression, missing ORDRAK rules, shared PC character-event mapping, enchant failure, retirement/retired levels, applied maximum damage, troll champions, Horse/Hatch/Gar interactions and class first-win predicates. Steam transport adds identity checks, transactional complete reads, monotonic reconciliation with an earned-delta journal, deterministic dry-run plans, acknowledgment/retry/uncertainty handling and optional SDK integration. Native SDK compilation and live validation remain blocked; the fake API is the tested transport implementation.

Coverage is 54 implemented and unit-tested / 7 partial / 5 blocked. See the complete [66-row matrix](achievement-coverage.md); no live gameplay acceptance is claimed. Runtime guest completion is disabled for the five blocked predicates. No guessed stat producer was added for the six counter achievements lacking proven guest input; MAX_FAME is the seventh partial achievement.

| Unresolved item | Evidence and chosen behavior | Exact next action |
|---|---|---|
| Deepest floor (two awards) | Resolved 2026-10-04: level depth +296 after the game's level loads; see "Deepest floor producer" below. | Validate the stat on demo floors; the unlocks need fix/full-license. |
| Levers | Resolved 2026-10-05: activation end with +604 empty and +500 == 1; see the stat producer inventory. | Validate by pulling levers. |
| Pet potions | Resolved 2026-10-05: potion event 12, CCharacter target with owner +1444; see the stat producer inventory. | Validate by giving potions to the pet. |
| Exploding enemy kills | Resolved 2026-10-05: explosion block of the unit death; see the stat producer inventory. | Validate in a run if an explosion kill happens. |
| Items sold | Resolved 2026-10-04: merchant sale add-gold; see "Items sold producer" below. | Validate by selling a few items in the next run. |
| MAX_FAME | Retained predicate is connected; table parity not proven. | Compare PC and guest fame tables read-only; validate final-rank boundary in gameplay. |
| PET_TRAINER | PC rejects types 41/42 and then performs spell learning. Native ingress disabled. | Locate equivalent guest qualified learning path; test rejected types and successful learning. |
| BEAST_OF_BURDEN | PC compares category-0 occupancy/capacity after pet transfer; total vector size is insufficient. Native ingress disabled. | Map slot-range capacity/occupancy and exact transfer branch. |
| MODS_1/5/10 | Out of scope for the first version (2026-10-05): mods are not restored, so there is no loaded-mod count. Guest completion stays disabled. | None for v1. |
| STAT_CRITICAL_STRIKES | No catalog threshold; producer unresolved independently of explode kills. | Trace PC actual critical-hit increment and equivalent guest damage outcome. |
| Eligibility and duplicates | Preserved game-context state ==1, single fishing ingress, first-class-win idempotence; broader state meaning not guessed. | Validate state gate over load/menu/mod situations and fishing/alternate Alric in a local session. |
| Steam SDK/callbacks | No separately licensed SDK found. Default adapter reports unavailable, fakes tested. | Supply supported SDK externally, compile actual branch, perform read-only verified AppID/account validation. |
| Persistence and concurrency | Linux fsync+rename+directory fsync implemented; uncertain journal blocks resubmission. | Fault-inject journal failures/power-loss; review multiple-process/device account concurrency before production writes. |

Final verification commands (all passed):

```sh
cmake -S . -B out/build/pc-achievements-full -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH=$HOME/rexglue-sdk/out/install/linux-amd64
cmake --build out/build/pc-achievements-full
ctest --test-dir out/build/pc-achievements-full --output-on-failure
cmake -S src/achievements -B out/build/achievements-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build out/build/achievements-sanitized
ctest --test-dir out/build/achievements-sanitized --output-on-failure
```

The full executable linked and **15/15 suites passed**. The three standalone suites passed with ASan+UBSan; bridge overrides were separately tested in the full build against synthetic PPC memory and original-call doubles. `git diff --check` passed. Default CMake cache has empty STEAMWORKS_SDK_ROOT and TORCHLIGHT_STEAM_WRITES=OFF; the resulting executable has no unresolved SteamAPI/SetAchievement/StoreStats symbols. No live Steam initialization, read, write or reset was executed. The prepared manual procedure is in [achievements-steam.md](achievements-steam.md).

Focused checkpoints, in order: `0d47bb2` research; `7d65e0b` local service and explicit-event hooks; `9c0b964` evidenced stat producers; `95f2c15` fail-closed Steam dry-run and pending journal; final documentation/coverage checkpoint follows these. All remain exclusively on `feature/pc-achievements`. Main's head is still `875c469f2ce89a58cc392ded850399fec30275ac`, equal to the captured pre-campaign head. The three PC reference fingerprints above were rechecked unchanged. No reference/proprietary artifacts are tracked; no renderer/OGRE/shader/cache/generated/SDK source edits were made. Build outputs and private disassembly stay ignored/outside version control.

Recommendation: ready for further local gameplay validation with the optional local mode; **not ready for the first live Steam write** until SDK compilation/read-only validation, runtime gameplay acceptance and journal/mutation review are complete. Continue with ACH-005 as specified above and leave this branch unmerged.

## ACH-005 — local validation interrupted and offline follow-up

**Status: gameplay acceptance incomplete.** One isolated actual game run was launched before the user closed it and then explicitly prohibited further game execution. No game was relaunched after that instruction. Offline source audit, diagnostics, regression tests and documentation continued. Steam remained irrelevant: no Steam initialization, reads, writes, reset, SDK compilation or gate enabling occurred. ACH-006 was not started.

### R1: actual execution and complete chains proven

Used the existing extracted Xbox game with Xenos and its existing MNK controller emulation, a private copy of the repository's established save seed, and a fresh `ach005` local profile. No guest memory was written, no native hook was invoked from a debugger, no cheat or production shortcut was added, and no thresholds were changed. The guest resumed its genuine saved character in **Orden Mines Floor 2**. This validates the actual dungeon-load predicate, not a natural first-ever journey from town.

Launch configuration (recorded here for reproducibility only; **do not relaunch until the user permits game execution**):

```sh
env LD_LIBRARY_PATH=$HOME/rexglue-sdk/out/install/linux-amd64/lib \
  ./out/build/pc-achievements-full/torchlight \
  --game_data_root=$HOME/360tools/extracted/extracted --gpu_plugin=xenos \
  --video_driver=x11 --fullscreen=false --mnk_mode=true \
  --user_data_root=$HOME/torchlight-recomp/out/validation/ach005/userdata \
  --pc_achievements=local --pc_achievement_profile=ach005 \
  --pc_achievement_diagnostics=trace
```

Since main `09067cb` (2026-10-05) a build needs no `LD_LIBRARY_PATH`; the user's folders are named `TorchlightRecomp` (`~/.config/TorchlightRecomp/` settings, `~/.local/share/TorchlightRecomp/` saves and profiles, `~/.cache/TorchlightRecomp/`), and every build logs to `~/.local/state/TorchlightRecomp/logs/` (first line: executable and commit; `--log_file=PATH` overrides). The later validation runs in this document used `out/build/pc-achievements-full/logs/` and copies of `~/.local/share/torchlight`, the locations before that change. Validation runs keep `--user_data_root` on a copy of the saves.

All raw runtime logs, inputs, profile snapshots and screenshots stay in ignored `out/validation/ach005`; no proprietary game images, save files or dumps are committed. An initial loader-based launch failed before gameplay because the SDK derived its log directory from the loader executable; using the documented existing library-path launcher resolved that without source changes.

| Chain | Actual observation and persisted result | Limit |
|---|---|---|
| Genuine saved-game dungeon load -> guest completion at return `0x822199F4` -> `sub_823D9930` override -> Service::Unlock -> local state -> immediate Save | At 01:36:11.130 on 2026-10-04, FIRSTLEVEL unlocked once; durable write at 01:36:11.137. Persisted ID is `TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL`. | No game restart or duplicate dungeon load performed. |
| Real in-game movement -> guest character event 3 at return `0x822945CC` -> `sub_822D7520` override -> CharacterEvent/Add selector 15 -> Evaluate -> local checkpoint | 21 separate +1 notifications produced STAT_TOTAL_STEPS=21 and pending delta=21. Four persistence records were emitted overall; final profile contains those exact values and the one unlock. | TRAVEL_25000 was evaluated below threshold; it was not unlocked. No threshold seed/shortcut was used. |
| Actual guest character snapshot -> eligibility/rule inputs | Loaded actor `0x460B62F0`: difficulty 1, hardcore false, deaths 0, gold 941, elapsed play time 1136.6925s initially and advancing with execution. Context eligibility was 1. | Values observed for one character only; no character switching/death scenario validated. |
| Real bootstrap notification -> qualification -> no account mutation | Initial actor `0x462230B0` generated event 1, delta 100 with context eligibility 0. Native account gold remained zero; loaded character totals were not blindly imported. | This is a rejected bootstrap notification, not validation of a real gold pickup. |

R1 evidence comprises one fresh-state record, 22 character-event observations (one rejected bootstrap grant plus 21 steps), one retained completion and four persistence snapshots. The final local state SHA-256 is `f84028bbf40c768f6d6f02ae087197d3ec462c781740a44a2ade70509a6ce85e`. The user closed the window; the SDK logged normal window close then title hard exit with process status 0. There was no crash during the actual game run.

**Real ingress coverage: 2/66**, counting the completed FIRSTLEVEL chain and the TRAVEL_25000 stat producer separately. **Fully unlocked end-to-end gameplay coverage: 1/66.** The stat-producer count does not imply a threshold-crossing scenario was validated. Implementation coverage remains **54/66 implemented, 7 partial, 5 blocked**. These independent statuses appear in the [updated matrix](achievement-coverage.md).

### Discovered lifecycle bug and conservative fix

The observed close sequence led to SDK source inspection: `ReXApp::OnClosing` terminates the guest and calls `std::_Exit(0)` without reaching `OnDestroy`/`OnShutdown`. Our local runtime previously relied on OnShutdown for its final counter flush, so a normal window close could lose up to ten seconds of uncheckpointed account progress. **R1 itself did not demonstrate lost data**: its 21 steps had already checkpointed. The bypass is established by the executed close log and SDK source; loss is reproduced deterministically by the new actual-runtime regression test.

App-owned `TorchlightApp::OnWindowCloseRequested` now calls `PrepareForClose` before the SDK accepts/closes the window. The runtime freezes new native mutations, takes its mutex and saves dirty local state before guest termination. Native mode remains selected, preserving Xbox suppression during closing; it does not fall back to Xbox. No renderer or SDK code is changed and no Steam API is called by this checkpoint. Normal teardown still uses Shutdown. This correction targets accepted user close requests; force-kill/power loss and any future close path bypassing the close-request hook remain separate validation concerns. The repository's current close-request listeners do not veto this accepted request.

`achievement_runtime_test` runs the actual app-owned achievement runtime without initializing ReXApp, guest execution, SDL windows or Steam. It reproduces an uncheckpointed single step inside the ten-second interval, calls the pre-close checkpoint without Shutdown, and verifies exact persisted account/pending values. It also verifies late-notification rejection, repeated close, runtime reload, a different character snapshot, separate hardcore deaths, immediate completion persistence and duplicate completion after reload. This is **deterministic lifecycle validation**, not real game restart/character switching.

### Development diagnostics and duplicate audit

`--pc_achievement_diagnostics=trace` is opt-in; default `off` emits none of the per-event observations. Structured `PCACH` records contain the source, caller address, actor, event/delta, guest-derived difficulty/hardcore/playtime/deaths/gold, qualification, changed account stats, threshold comparisons, new unlocks and completion duplicate detection. Fresh/reloaded state, successful persistence and close checkpoints are observable. Diagnostics have no rule/stat producer and cannot change unlock conditions. Ordinary release output is unaffected by the disabled trace. The real R1 records were parsed as JSON and compared with the actual persisted profile.

Read-only guest audit confirms:

- Fishing success in `0x822D2438` calls the character dispatcher once, returning at `0x822D296C`, with event 14 and delta 1. The native service restores only this shared dispatcher ingress; no second fishing-specific Add exists. Equal arguments from separate legitimate catches remain separate increments. Tests cover this; fishing was **not** exercised live.
- Normal boss completion routes to the mapper at return `0x822D74C8`. Alternate Alric `0x823CEED0` first reaches retained completion at `0x823CF458` and then the mapper at `0x823CF460`, both for KILL_ALRIC. Regression coverage now feeds all those deliveries and asserts one local completion with unchanged stats/pending on repeats. The three first-class-win counters retain their zero-to-one qualification. Alric/boss completion were **not** exercised live.
- Repeated explicit completions are idempotent; gameplay counter deltas are not indiscriminately deduplicated by matching numeric arguments. Every one of the 66 IDs now has save/load/reload/repeated-completion regression coverage. No account increments are replayed by loading local completion.
- No pre-existing safe controlled ORDRAK gameplay mechanism was found in the inspected launch/benchmark tooling. No shortcut was added. The full difficulty/hardcore/5h/8h/death matrix remains deterministic coverage only; even the real ORDRAK ingress is pending.

### Remaining validation and readiness

Unperformed due to the execution prohibition: Xbox-routed pet/boss/enchant event, successful threshold crossing, game-level save/reload and process restart, switching two actual characters, fishing, alternate Alric, normal boss and ORDRAK death, and duplicate achievement notification after actual reload. These do not receive real-game validation merely because their synthetic/actual-runtime tests pass.

The same seven partial achievements remain: CRITICAL_STRIKE_ON_DEATH, REACH_LVL_50, REACH_LVL_100, PULLED_LEVERS_100, PET_POTIONS_50, MAX_FAME and SELL_ITEMS. The same five blocked achievements remain: BEAST_OF_BURDEN, PET_TRAINER and MODS_1/5/10. Critical-strike stat production remains unresolved outside catalog thresholds. None was promoted on inferred semantics; no unresolved producer was implemented in this validation ticket.

The branch can support a separate ACH-006 **offline SDK compile** ticket. Actual read-only runtime integration must respect the user's current game-execution prohibition. ACH-005 acceptance is incomplete; this is not approval for live Steam mutations. Do not automatically start ACH-006 and do not merge this branch or modify main.

### ACH-005 verification and checkpoints

After the bugfix and diagnostics changes, the full executable builds and **16/16 CTest suites pass**, including five achievement suites. A separate sanitized build of all five achievement targets passes **5/5 with ASan+UBSan**, including the actual native runtime lifecycle and synthetic PPC bridge tests. The SDK shared library itself is not sanitizer-instrumented. No game was executed by these tests.

```sh
cmake --build out/build/pc-achievements-full
ctest --test-dir out/build/pc-achievements-full -R achievement --output-on-failure
ctest --test-dir out/build/pc-achievements-full --output-on-failure
cmake -S . -B out/build/ach005-sanitized -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_PREFIX_PATH=$HOME/rexglue-sdk/out/install/linux-amd64 '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build out/build/ach005-sanitized --target achievements_test achievement_guest_abi_test achievement_hooks_test achievement_steam_test achievement_runtime_test
ctest --test-dir out/build/ach005-sanitized -R achievement --output-on-failure
```

Focused commits: `2d215d0` fixes the pre-hard-exit local checkpoint and adds its runtime regression; `b05463a` adds opt-in ingress diagnostics, duplicate-Alric/fishing bridge coverage and all-66 completion persistence tests. The documentation checkpoint follows. The current feature branch stays unmerged. At ACH-005 start, main was already `b0fa2735fdc7898f3132e2de6926d5e41938c1e5`; this ticket did not modify it or incorporate it into the feature branch. `git diff --check` passed. Private gameplay artifacts remain ignored, and no renderer/OGRE/shader/cache/generated/SDK/proprietary files were changed or committed.

### Handoff after ACH-005 (2026-10-04)

The user subsequently explicitly authorized rebasing `feature/pc-achievements` onto updated local main. That rebase completed without conflicts onto `524177f67dc22a0fc06fabb7c65b3461899134bb`; main itself was not modified and the feature branch remains unmerged. The rewritten ACH-005 fix/diagnostics/documentation commits are `b657a6e`, `f255b64`, and `8fdc28a`. After the rebase, the full executable built, all **19/19 CTest tests passed**, and `git diff --check main..HEAD` passed. No game was run after the rebase; the earlier sanitizer results are pre-rebase results.

**Current user intent:** display an in-game achievement-unlocked notification while using the fully offline local backend, without changing any real Steam account. Local mode currently persists achievements and logs new unlocks, but has no visual notification. A proposed next task is a queued, temporary notification with the achievement's display name, emitted only for a new gameplay unlock, never for duplicates or loading existing completion state. This presentation work has been discussed but **not implemented**. RetroAchievements was considered as a design reference: its frontend displays notifications in response to unlock events; integrating its service is not necessary for this request. No GOG binary has been inspected and no GOG-specific offline achievement implementation has been established. Steam Achievement Manager is unrelated to the requested offline presentation and must not be used to mutate accounts.

Starting points for the next agent: `src/achievements/runtime.cpp` detects newly unlocked IDs in Apply; `src/achievements/service.*` and `catalog.h` own rules/state; `src/live/ui_overlay.*` implements the existing host UI overlay. Evaluate a narrow presentation boundary and UI-thread-safe queue rather than changing achievement semantics or renderer behavior. Existing overlay support does not establish that achievement notifications work in native or Xenos mode: neither is implemented/tested yet. Display metadata/icon availability and suitability for distribution need inspection; do not copy proprietary reference assets into commits.

**Execution constraints remain:** stay on `feature/pc-achievements`; do not merge or modify main; keep reference/pc read-only; no real Steam writes/resets; do not execute the game yourself without renewed user authorization. The user offered to perform a future manual run; no such run or permission to resume autonomous execution has occurred. The recorded R1 used Xenos, not the native renderer. A future native run needs `--native_live=only` and the environment's validated display/GPU configuration; the historical R1 command is evidence, not the recommended current native launcher.

A practical manual-validation proposal is to use a copied save directory and disposable `--pc_achievements=local --pc_achievement_profile=ach005_manual --pc_achievement_diagnostics=trace` profile, then enter a dungeon, walk, send the pet to town, catch two fish, feed a fish, save/reload, switch characters if a second one exists, and close/restart immediately after a few steps. Collect full logs and the local state file with approximate action times. Boss/ORDRAK ingress is optional if an existing save is already nearby; no full playthrough or natural sub-five-hour completion is requested. A disposable threshold fixture could test an actual step crossing 24,999 -> 25,000, but **no such fixture has been prepared or run**. Keep fixture-assisted threshold validation separate from naturally earned progress.

Read this document for behavioral evidence and architecture, `docs/achievement-coverage.md` for per-ID implementation versus actual gameplay/lifecycle validation, and `docs/achievements-steam.md` for transport safety. ACH-005 remains incomplete; ACH-006 has not started. Offline notifications do not authorize Steam integration or live mutation.

## Offline unlock notifications (2026-10-04)

**Status: implemented, unit-tested and validated in two only-mode game runs (Spanish), see below.** The branch was rebased onto main `49a33e2` first (one include conflict in `src/torchlight_app.h`; build and all tests green), so it can reuse the render/menu front's `GuestCall` and `guest_abi/game_ui.h` unchanged.

Decision: draw the notification with the game's own UI (CEGUI), not the only-mode ImGui overlay. The CEGUI path works with Xenos and with the native renderer alike (the game draws it), appears in captures, and needs no change in `src/live`; the ImGui overlay treats any open dialog as owning the input in only mode. If the CEGUI path had failed twice, the fallback was an ImGui toast behind the same queue.

Pipeline:

- `runtime.cpp` `Apply` pushes every ID that the applied gameplay action newly unlocked, in catalog order, to a thread-safe `NotificationQueue` (`notifications.h`). Loading saved state, Steam reconciliation (`SteamBackend::Tick`, outside `Apply`), repeated completions, events while closing and shutdown never notify; `Install`/`Shutdown` clear the queue.
- Policy: `local` and `steam-dry-run` always notify. With the real Steam backend (`steam`) the Steam overlay shows its own, so ours is off unless `--pc_achievement_notifications_with_steam=true`.
- Texts: `display_names.h` holds an English description of each of the 66 conditions plus the title, written for this project; they are not the official PC/Xbox names. `data/ui/tl_achievement_strings.txt` translates them to de/fr/es (the video menu's format, loaded with its `MenuStrings`). Language: a language pack if set, else the console language, as the video menu. All texts stay within Latin-1, which the game's UI can draw.
- Presenter: `toast.cpp` overrides `CGameUI::Update` (`0x821A40E0`, every frame on the game thread in menu and in-game states). On the first notification it creates the toast windows with `WindowManager::createWindow`/`setProperty`, as children of the game UI's only GUI sheet (`CGameUI +0x340`), scaled with `kGameUiScaleLayout`. It reuses the game's autosave notice style: `ceguiWidget/ItemText` frame, `facet_panel_bottom` header plate with a `SerifBig` title, `Serif14` body. The windows pass the mouse through, have no tab order and are always on top. No layout file or resource location is used: the game menu's resource-location hook (`sub_8239B998`) is only installed in only mode and belongs to the menu front.
- Timing (`toast_schedule.h`): one toast at a time, five seconds, 0.5 s gap. While the loading screen (`+0x344`) or ratings splash (`+0x358`) is attached to the sheet nothing shows and nothing is dequeued; a toast they interrupt is shown again in full afterwards. FIRSTLEVEL, which unlocks during a dungeon load, therefore appears once the level is visible.

ABI evidence lives in `src/guest_abi/game_ui_sheet.h` (sheet creation and `setGUISheet` callers, the full-screen layouts' attach/detach sites, `Window::isChild` child vector).

Tests: `achievement_notifications_test` (queue order/concurrency, per-mode policy), `achievement_runtime_test` (actual runtime: new unlock once, duplicate, below/at threshold, several unlocks in one event, shutdown, reload, closing), `achievement_display_names_test` (catalog order, unique texts, complete de/fr/es coverage, Latin-1), `achievement_toast_schedule_test`, and sheet/cover fixtures in `achievement_guest_abi_test`. The full build passes 30/30 tests; the standalone `src/achievements` build passes 5/5.

Not validated without a run: that the windows are created and drawn, their position and size at the game's scale, the fonts and header image, legibility over the HUD and the menus, and the loading-screen hold. Proposed validation run (not executed): fresh profile `--pc_achievements=local --pc_achievement_profile=toast_check`, enter a dungeon (FIRSTLEVEL unlocks during the load), and confirm the toast appears after the loading screen for about five seconds; repeat in another language if possible. The log line `achievement toast: built ...` marks the window creation and `achievement toast: <ID>` each display.

Future improvement: read official names at runtime from the user's own files, with our texts as fallback: the Xbox names from the XEX's XDBF (12 awards), the PC names from the user's PC installation when present. Nothing from those files is copied into the repository. Icons are not shown.

### Toast validation runs (2026-10-04)

Two user-driven runs in `--native_live=only` (Wayland, NVIDIA), Spanish, disposable copy of the save directory under ignored `out/validation/toast`, fresh profiles `toast_check` and `toast_check2`, `--pc_achievement_diagnostics=trace`:

- Entering Orden Mines floor 1 unlocked FIRSTLEVEL (22:00:46.221 / 22:06:58.672); the toast windows were built and shown 3.4 s / 3.3 s later, once the loading screen was gone (`achievement toast: built ...`, `achievement toast: TORCHLIGHT_ACHIEVEMENT_FIRSTLEVEL`). No errors, no crash.
- The user confirmed position (top centre), header plate, fonts, Spanish texts and legibility, and that moving and attacking worked while the toast was up (the toast takes no input).
- Two F9 captures taken while it showed replay offscreen with the toast drawn by the native backend: header plate with "Logro desbloqueado" and the body text below, above the HUD. The `ItemText` frame draws no visible background behind the body text; it reads well over the dungeon but may be less legible over bright scenes.
- Not yet validated: other languages, several queued toasts, Xenos mode, a toast interrupted by a later loading screen.
- Follow-up (2026-10-04): a dimming backdrop was added behind the toast text, the game's own `set:widgets2 image:black` at alpha 0.7 as its message panels use; it is validated in the next agreed run.

Follow-up on two observations from these runs, resolved by reading code (no change needed):

- Leaving through the game's own exit does persist. XamLoaderLaunchTitle "exit to dashboard" calls `KernelState::TerminateTitle`; the guest main thread ends, the SDK module thread logs "Execution complete" and requests `QuitFromUIThread`, the SDL loop returns and `windowed_app_main_sdl.cpp` calls `InvokeOnDestroy` -> `OnShutdown` -> `achievements::Shutdown`, which saves dirty state. Unlike the window close button, this path does not go through `_Exit`. (An earlier note here said it skipped the checkpoint; that was wrong.)
- New-character events follow PC semantics. The three quick quest completions come from `sub_823C9378` (quest completion: character event 4 with delta 1 @0x823C93A8, completed flag +33, then activates follow-up quests; its caller is `sub_823CEED0`), the same dispatcher event PC maps to STAT_QUESTS_COMPLETED for any caller; the opening quests complete quickly. The starting gold grant reaches the same character-event-1 ingress with context +5124 == 1 and is counted exactly as PC would count it if +0x2428 corresponds to +5124; that correspondence is the eligibility question already listed as open above, not a new defect.

## Deepest floor producer (2026-10-04)

REACH_LVL_50 and REACH_LVL_100 are now implemented (coverage: **56 implemented, 5 partial, 5 blocked**).

- PC: right after a level load, `0x004190EF` and `0x0041A232` compare the depth of the game's current level (game +0x38, level +0x140) with STAT_DEEPEST_FLOOR and raise it through the eligibility-gated assignment `0x005F7130`.
- Guest level load `sub_82217DA8` is PC `0x415820` (both contain the FIRSTLEVEL completion). It allocates a new level, whose constructor `sub_822E8E30` stores the depth argument at +296, and stores it at game +56 (`@0x82218654`). The HUD floor number is +296 + 1 (next to `L"Floor"` `@0x822197C0`): depth is zero-based, so floors 50 and 100 are depths 49 and 99, matching the catalog thresholds.
- Callers: the game state update `sub_82212950` (`@0x82212F5C`, `@0x82213018`) and the floor transition `sub_82214818` (`@0x82215054`) are PC's two update sites; the editor load `sub_82250B00` (strings `Torched/`, `EDITORRESOURCES`) matches PC `0x4530E8`, which has no update, and is excluded. The only other level creator, `sub_82217248`, always passes depth 0.
- The infinite post-story dungeon on Xbox is `RANDOMDUNGEON` (flag `BOTTOMLESS`, dungeon +161, parsed in `sub_822E7228`). It has no separate load: the transition computes the depth as current depth + floor delta, an explicit target, the character's saved portal depth or 0 for town, and the load only uses the dungeon's per-floor tables with bounds checks. Floor 100 therefore arrives as depth 99 through the same path and field.
- Hook: `sub_82217DA8` runs the original (after a counter flush, see "When counter achievements unlock"), then, for the three game return addresses only, applies `Maximum(STAT_DEEPEST_FLOOR, depth)` when the active player is eligible (`guest_abi/achievements.h` `kGameLevelLoadReturns`, `LevelDepth`). Eligibility at this point was 1 in both toast runs (the FIRSTLEVEL observation happens inside the same load). Diagnostics record a `level-depth` observation with the load's caller.
- Tests (`achievement_hooks_test`): all three game paths, editor excluded, no level, ineligible context, shallower depth, 48 -> 49 unlocking REACH_LVL_50 and 98 -> 99 unlocking REACH_LVL_100, original always run.
- Validation: the demo (license_mask 0) caps dungeon depth, so the unlocks wait for fix/full-license. The next agreed run (with the toast background and another language) includes going down a couple of demo floors to watch STAT_DEEPEST_FLOOR rise.

## Items sold producer (2026-10-04)

SELL_ITEMS is now implemented (coverage: **57 implemented, 4 partial, 5 blocked**).

- PC: player method `0x4D6670` only adds 1 to STAT_TOTAL_ITEMS_SOLD (`0x4D667B`). Its two callers, `0x54A79A` and `0x54AC31`, are the sale branches of one UI handler (shared jump to `0x549FDC`); each plays UI sound 0x17, pays the item's value (`0x4B1780`) through add-gold `0x4860B0`, notifies, then refreshes the menu. One per sale, independent of stack size or price.
- Guest: the merchant sale handler `sub_8236B7B8` (called from `sub_8236A810`, in a vtable at `0x82150758`) plays sound 23 (`sub_82377EE0`), pays `sub_822AB950(item)` through add-gold `sub_8229FB58` (`@0x8236B850`) and moves the item to the merchant, without the notification. It is the only guest call that plays the sale sound and pays an item's value; PC's two branches are one path here. An item with flag 103 leaves before payment.
- Not sales, in PC either: the pet's town-sale loop `sub_8228E580` (gold to the pet's owner; PC's pet sale does not call `0x4D6670`, whose only callers are the two UI branches), enchanting payments (`sub_82361E80`, negative), gambling (`sub_8236B578`) and quest rewards.
- Hook: `sub_8229FB58` runs the original (whose gold still reaches the character-event hook), then, only when returning to `0x8236B854` with an eligible active player, adds 1 to STAT_TOTAL_ITEMS_SOLD; diagnostics record an `item-sold` observation with that caller.
- Tests (`achievement_hooks_test`): one per sale, the other gold sources excluded, ineligible and native-off cases, 9,999 -> 10,000 unlocking SELL_ITEMS, original always run.
- Validation: selling a few items is part of the next agreed run; the trace should show one `"source":"item-sold","caller":"0x8236b854"` record per item and `stats[17]` rising by one each.

## Combined validation run (2026-10-05)

User-driven run in `--native_live=only` after a clean rebuild against the SDK with patches B and C (30/30 tests), German (`language de`), disposable copy of the save directory under ignored `out/validation/combined`, fresh profile `combined_check`, diagnostics on:

- Toast: a new character entering Orden Mines floor 1 unlocked FIRSTLEVEL (12:39:28.904); the toast was built and shown at 12:39:32.063 in German. The F9 capture replays with the header "Erfolg freigeschaltet", the text "Den ersten Dungeon betreten" and the new dimming backdrop behind them, legible over the scene.
- Deepest floor: `level-depth` observations from the floor transition (`0x82215058`) with depth 0, 1, 2 (floors 1-3), STAT_DEEPEST_FLOOR 0 -> 1 -> 2, eligibility 1; a portal back to town recorded depth 0 without lowering it. The new-game load (`0x8221301C`) recorded depth 0. REACH_LVL_50/100 stay locked, as expected in the demo.
- Items sold: four `item-sold` records (`0x8236b854`), each right after its own gold payment (20, 20, 20, 19), STAT_TOTAL_ITEMS_SOLD 0 -> 4; the user confirmed selling one more item than planned. No double counting.
- Persisted profile after the game's own exit: deepest floor 2, items sold 4, FIRSTLEVEL unlocked.

## Stat producer inventory (2026-10-05)

Systematic pass over every PC account-stat mutation instead of one achievement at a time. Coverage after it: **57 implemented, 4 partial, 2 blocked, 3 out of scope** (MODS_1/5/10: mods are not restored in the first version, so there is no loaded-mod count; not a blocker).

PC stats manager: `0x5F6AF0` returns the singleton (the selector is pushed beforehand for the method that follows). Its methods: `add` `0x5F71D0` (15 call sites), `assign` `0x5F7130` (5), `get` `0x5F6BA0`; the others are the Steam request lifecycle (`0x5F7980`, `0x5F77F0`), accessors (`0x5F3E30`, `0x5F6BD0`), registration (`0x5F7270`) and the player binding (`0x5F6B10`). No other code writes account stats.

The guest keeps **no** stat mutation at all: only the readers `sub_823DC258` (int) and `sub_823DC2B0` (float) and the character snapshot `sub_823DC0E8` (PLAYER_DEATHS/PLAYER_GOLD, PC `0x5F6B10`). So no producer is "present"; every PC site is classified by whether its containing function survives ("trimmed": the function exists, the stat call was removed) or not ("absent"). Guest counterparts were found with `tools/guest_re/pc_match.py` (shared string literals, own and callees', weighted by rarity) and then confirmed in the code; it ranks the known pairs first (level load, game update, transition, fishing, quests, horse, class wins, damage, trolls).

| PC site (op, stat) | PC function | Guest function | Class | State |
|---|---|---|---|---|
| `0x4190E8` assign 5 DEEPEST_FLOOR | `0x4188E0` game update | `sub_82212950` | trimmed | implemented (level-load hook) |
| `0x41A22B` assign 5 DEEPEST_FLOOR | `0x4197E0` floor transition | `sub_82214818` | trimmed | implemented |
| `0x48E589` add 19 TROLL_CHMPS | `0x48E4A0` | `sub_8229FD28` | trimmed | implemented |
| `0x497B67` assign 3 MAX_DMG_DONE (as maximum) | `0x497710` damage | `sub_8229C7B0` | trimmed | implemented |
| `0x4A7D48` add 24 EXPLODE_ENEMY | `0x4A7570` unit death | `sub_8229D0C8` | trimmed | implemented (effect-call hook, return `0x8229D88C`) |
| `0x4B50A8` add 20 POTIONS_PET | `0x4B4FB0` potion use | `sub_822B9A60` | trimmed (the cast survives, result discarded) | implemented (event-12 hook, return `0x822B9BB8`, cast and owner check) |
| `0x4D6674` add 17 TOTAL_ITEMS_SOLD | method `0x4D6670`, called from the sale UI `0x54A79A`/`0x54AC31` | method absent; sale `sub_8236B7B8` | trimmed caller | implemented |
| `0x4D66B8` add 0 (+18), `0x4D66DA` add 13, `0x4D673A` shared add 15/10/16/6/9/8 | `0x4D6690` character events | `sub_822D7520` | trimmed | implemented |
| `0x4D7545` add 6 FISH_CAUGHT | `0x4D6FD0` fishing | `sub_822D2438` | trimmed | implemented through character event 14 only (see ACH-003) |
| `0x4DF4F2` add 14 LEVERS_PULLED | `0x4DF190` object activation | `sub_822D8160` | trimmed | implemented (activation-end hook, return `0x822D8540`, checked before the call) |
| `0x5774EB` add 11, `0x577503` add 12 RETIRED | retire/enchant UI | `sub_82361B00` | trimmed | implemented |
| `0x577761` add 7 ENCHANTER_FAILS | same | `sub_82361C78` | trimmed | implemented |
| `0x595882` add 25/26 QUESTS_HATCH/GARR | `0x595550` | `sub_82355988` | trimmed | implemented |
| `0x598B88` add 27 HORSE_TALK | `0x598B40` | `sub_823568D0` | trimmed | implemented |
| `0x5EBD98` add 21-23 class wins | `0x5EB7D0` | `sub_823CEED0` | trimmed | implemented |
| `0x5F3C96`, `0x5F3CEB` assign (threshold raise on forced completion) | `0x5F3C80`, `0x5F3CB0` | `sub_823D9930` | trimmed | implemented in the service |

No PC producer is absent from the guest. STAT_CRITICAL_STRIKES (selector 2) has no PC producer at all, so no achievement depends on it and nothing is to be restored.

Evidence for the three proposed sites:

- Exploding enemies: PC builds the explosion effect (`+0x60` float, `+0xA5 = 1`), calls `0x50E3F0`, `0x5FB340`, `0x48A260(0, 1)` and the unit's `+0x1AC` object (`0x4C3FF0`), sets unit `+0x659 = 1` and adds 1 to selector 24. Guest `sub_8229D0C8` has the same straight-line block: effect `+96`/`+165 = 1`, `sub_821D3F38`, `sub_822238E0` (`@0x8229D888`, return `0x8229D88C`), `sub_821F0210(unit, 0, 1)`, unit `+428` (the same offset) -> `sub_822C36D8`, `stb +1605` (`@0x8229D8AC`). The other `sub_822238E0` call in that function (return `0x8229E198`) is a different effect without those stores.
- Potions given to a pet: per applied potion effect PC checks item flag 33, sends character event 12 (`0x484660`, the guest `sub_82294548`), dynamic-casts the target `CBaseUnit` -> `CCharacter` and adds 1 to selector 20 when its `+0x5B4` is set. Guest `sub_822B9A60` sends event 12 (`@0x822B9BB4`, return `0x822B9BB8`) and still calls `__RTDynamicCast` (`sub_821E1828`, same type descriptors `.?AVCBaseUnit@@` `0x834C27FC`, `.?AVCCharacter@@` `0x834C2814`) but drops the result. PC `+0x5B4` is the pet's owner: PC's pet town sale `0x4924E0` pays `[pet+0x5B4]`, guest `sub_8228E580` pays `[pet+1444]`, so the guest field is `+1444`.
- Levers: PC `0x4DF190` is `CTriggerUnit`'s activation (vtable `0xA80284` slot 79, the only vtable referencing it, so levers, plungers and horns share it; guest `sub_822D8160`, `CTriggerUnit` vtable `0x82004894` slot 77). It ends with "if `[this+0x278] == 0` and `[this+0x1F4] == 1`: add 1 to selector 14", then calls `0x4DE6C0(arg)`; the guest ends with `sub_822D8548(this, arg)` (`@0x822D853C`, return `0x822D8540`) without the check. The constructors (PC `0x4DDDAB`, guest `sub_822D7E78`) place `+0x1F0..+0x201` and `+0x264..+0x288` at the same offsets. `+0x1F4`/`+500` is the number of activations the trigger needs (compared with an activation counter three times in `0x4DE6C0`/`sub_822D8548`). `+0x278` is the length of the `std::wstring` at `+0x264` (MSVC layout: length at +0x14); the guest string starts at `+612` with its length at `+628`. **Correction (2026-10-05):** the first implementation read `+604`, mapped from a `CCharacter` method; the trigger constructor sets `+604` to 1, so no lever could count (the producers run confirmed it: a plunger did not count).

## When counter achievements unlock: PC's flush points (2026-10-05)

PC evaluates counter (threshold) achievements only when its stats manager flushes; explicit, single-action completions are immediate. Native mode now does the same.

- `add` (`0x5F71D0`) and `assign` (`0x5F7130`) record the old value in a pending-changes list (`0x5F70C0`, manager +0x8C) and never notify. They would mark the manager dirty with a 1 s timer only for stats whose descriptor flag +0x24 is set, and the static initializers clear it for all 31 descriptors (`xor ebx,ebx` then `mov ds:0xBF0754+40k, bl`).
- The pending list is evaluated against the listening achievements (`0x5F3B80` -> `0x5F39D0`, stat >= threshold) by `0x5F72F0`, called after a successful StoreStats in the update (`0x5F794C`, after the forced flush `0x5F7980` set the dirty flag; the retry timer is then 120 s) and on a Steam stats read (`0x5F75CE`).
- `0x5F7980` is called at: the start of every level load (`0x41586B` in `0x415820`); game state 2 in the state change `0x4188E0` (`0x41899A`; the state lives at +0x2428, guest +5124); the start of the main-menu level load `0x40ECE0` (`0x40ED0E`); and the app frame `0x40A050` when the game asked to quit (`0x40A1F5`).
- Guest hooks: on entry of the level load `sub_82217DA8` and of the main-menu level load `sub_82217248` (matched by `tools/guest_re/pc_match.py` and their strings), and before `sub_823A1280` when called at `0x82212AA8` (return `0x82212AAC`), the first call of the state-2 branch of `sub_82212950` (`cmpwi r19,2`), where PC flushes right before the parallel `0x5C6B50(&local)` on the same +0x3C/+60 object. The quit point has no guest counterpart (the Xbox build quits by launching the dashboard), so the host's close checkpoint and shutdown flush before the final save; nothing is announced then.
- `Service` keeps PC's pending set (not persisted); explicit completions, forced completions and Steam reads (which, like PC's receive path, evaluate everything) stay immediate. Loading a saved profile also evaluates everything.
- Consequence, matching player reports: REACH_LVL_50 (depth >= 49, i.e. floor 50) is evaluated at the next level load, so it appears when floor 51 starts loading; its toast follows that load. A player guide used as an outside cross-check (not a source of truth, not copied): https://steamcommunity.com/sharedfiles/filedetails/?id=241904961
- Tests: `achievements_test` (deferred counters, explicit/forced immediate, several counters at one flush, Steam read evaluating pending values), `achievement_hooks_test` (floor 50 -> unlock at the next load, explicit immediate meanwhile, state-2 and main-menu flushes, other callers ignored, native off), `achievement_runtime_test` (announcement only at a flush point, exit flush persisted before the close checkpoint).

Other checks against the PC code prompted by that guide: plungers and horns are `CTriggerUnit`s like levers (same activation method); potions given to a pet also count toward DRINK_POTIONS when the player is the one using them (event 12 goes to the user, the pet stat to an owned target); every merchant uses the one sale handler; HAT_TRICK is completed during the third class's win itself (PC increments its local copy of the class counter before checking all three).

## Levers and pet potions disabled (2026-10-05)

A validation run (`potion_check`) showed both producers wrong, each after a second attempt, so both are disabled in code (`kCountLevers`, `kCountPetPotions` in `guest_hooks.cpp`) until their PC predicate is re-established by static reading:

- Levers: with the corrected field (+628), four `lever-pulled` records appeared (15:31:00, 15:31:50, 15:32:22, 15:33:40) while the player pulled no lever or plunger. The condition read so far (one required activation, empty string) admits other `CTriggerUnit` activations.
- Pet potions: the player drank one potion (event 12 and a `pet-potion-check` with owner 0, correct) and then gave one to the pet; the latter left no record at all, so it does not pass through `sub_822B9A60`'s event-12 call. The diagnostic stays active for the follow-up.

## Toasts wait for a playable level (2026-10-05)

In the `potion_check` run the FIRSTLEVEL toast appeared 2.8 s after the unlock, over what the user saw as the loading screen. The log shows the first autosave of the session one second earlier (15:31:07); that autosave opens a full-screen notice (`CGameSaveMenuXenon`, `gamesave_xenon.uilayout`), which the toast's loading check did not cover. `CGameUI` keeps that menu at +0x3D4 and does not add it to its menu list, so the game's own "is a menu open" scan (`sub_82191210`) misses it too.

Every toast now waits for `game_ui_sheet.h` `LevelReady`: the game context state (+5124, read by `CGameUI::Update` from r5) is 1 (in game), neither the loading screen nor the ratings splash is on the sheet, and the autosave notice is closed (its `CDropdownMenu` open flag +0x18). Other menus (inventory, pause) do not hold toasts: the level stays playable and the toast is drawn above them. A toast interrupted by any of these is shown again in full afterwards. Fixture tests cover each condition. Validation in a game run is pending.

## Levers re-enabled with PC's trigger condition (2026-10-05)

Static reading of PC settled which triggers count: every unit of type `TRIGGER` (factory `0x5FB529`) shares `CTriggerUnit`'s activation, and the check reads two unit-data properties: `SPAWNCLASS` (string at +0x264, assigned at `0x4E0B97`; guest string at +612, length +628) and `MAXSTATES` (stored minus one at +0x1F4 by `0x4DF679`, default 2; guest +500). PC counts a trigger with no `SPAWNCLASS` and `MAXSTATES` 2. Classifying the 76 trigger definitions of the installed game (read at analysis time, nothing copied) gives 40 that qualify, including doors, stairs, the mine entrance, portals, shrines and the fishing hole, not only levers, plungers and horns; the coverage row lists the categories. The four counts of the `potion_check` run (mine entrance at 15:31:00, then stairs/doors) are consistent with this. Following the PC code as the source of truth, the producer is enabled again (`kCountLevers`). Plungers are placed by some mine room pieces (entrances, exits and a few corridors), so they appear on some Orden Mines floors, not all.

## Potions on the pet: the path (2026-10-05)

In the `potion_check` run the potion "given" to the pet was moved to its inventory with X, a transfer, not a use, which explains why it never reached the item-use path. Static reading:

- PC "use item on target" `0x549180` (guest `sub_82343B88`): item type +0x200 0 uses it at once through `0x4B4FB0` (guest `sub_822B9A60`), type 1 enters target selection. PC calls it from the inventory UI (`0x549848`, `0x54BAAB`, `0x54BB11`), the pet menu (`0x57CEB0`, `0x57CEC8`, user and target the pet) and the inventory's pet branch (`0x58F5C8`, which rejects unit types 41/42, then uses the item with the pet's owner as user and the pet as target; the PET_TRAINER block). `0x4B4FB0`'s only other caller is the inventory handler `0x54A2EF`. All are UI paths: no AI path uses items through them, so if a pet drinks potions on its own, PC does not count those.
- Guest: the inventory menu `sub_8236BD80` uses an item with the inventory's owner (`*(menu+192)`) as user and target, three of its four calls to `sub_822B9A60`; on the pet's inventory that is the pet. The input handler's `sub_823439E8` uses items with flag 125 (pet food) on the pet and others on the player. The pet branch of `sub_82343EB0` handles command 68, `GUIFEEDPET` (the selected item on the player's pet, rejecting unit types 41/42, as PC's `0x58F5C8`); no layout or code in the Xbox build sends that command.
- All of these reach the event-12 call that the producer hooks, so the producer is enabled again (`kCountPetPotions`). To validate on Xbox: open the pet's inventory, select a potion there and use it (the controls tray labels the use button "use/equip").

### Toast wait reverted (2026-10-05)

After the `final_check` run the user preferred the original behaviour, so both changes above were reverted (`git revert` of `3338667` and `b2a8cc0`): toasts again wait only for the loading screen and the ratings splash (`Covered`), not for the autosave notice or game menus such as the story vignette. The `LevelReady` analysis above stays as reference.

### Last validation of levers and pet potions (2026-10-05, `final_check`)

- Levers: the user pressed one detonator (the mine plunger), counted at 23:36:57. Three more activations counted (23:32:54 just before the first floor load, the mine entrance; 23:33:31 and 23:34:33, doors or stairs). This matches PC's condition: opening a door or taking stairs counts toward PULLED_LEVERS_100, because those trigger units have no SPAWNCLASS and MAXSTATES 2. The trace does not name the unit, so the exact objects behind the three are inferred.
- Pet potions: the user could not make the pet use a potion. Moving it to the pet's inventory (X) is a transfer, the pet inventory offers no use for potions, and `GUIFEEDPET`, the command that uses an item on the pet, is never sent in the Xbox build. PET_POTIONS_50 stays implemented to PC parity but is unreachable on Xbox; decided to leave it documented as is.

## Pet spell and pet bag (2026-10-06)

PET_TRAINER and BEAST_OF_BURDEN are implemented (coverage: **63 implemented, 0 blocked, 3 out of
scope**). Correction to the notes above: BEAST_OF_BURDEN is not tied to the pet transfer; PC checks
the first pet's bag (category 0 items == capacity) in the player's per-frame update `0x4D7C10`
while the game is playing. PET_TRAINER is sent by the pet panel's spell-slot handler `0x58F530`
after the scroll's use. Evidence and guest layout: `guest_abi/achievements.h` (`kPetSpellUseReturn`,
`FirstPet`, `InventoryCategoryFill`); the interface difference and why it is accepted:
achievements-xbox.md, "Differences from PC and why".
