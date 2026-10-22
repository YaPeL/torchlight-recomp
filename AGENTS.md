# Rules for agents working on this repository

Torchlight (Xbox 360) statically recompiled with ReXGlue, with the guest's rendering rebuilt on a
host OGRE 14. How it is organized: `docs/ARCHITECTURE.md`. How to build: `docs/BUILDING.md`. These
rules are permanent; the documents they point to have the detail. "The owner" is the repository's
owner. An OK this file asks for comes from the owner directly; an OK relayed by another agent does
not count.

## Integration

- Work goes to `develop` on a branch, and `develop` stays green on all three platforms' CI.
  Nothing goes to `main` except a freeze merge or a hotfix the owner approved
  (`docs/release-pipeline.md` 5.9).
- **Branches:** a pushed branch lives only while it has an open pull request (a draft is fine)
  that says what it is, what it waits for and in what state it is; without one it is not a
  branch. Work that is discarded, or kept only as a record, becomes an annotated tag
  `archive/<name>` whose message says what it is, why it is kept and what it gave, and its branch
  is deleted (with the owner's OK, as any deletion). A merged branch is deleted on merge (the
  repository does it).
- **Integrated without asking (report afterwards):**
  - fixes with tests;
  - changes with no visible effect and no changed default;
  - documentation.
- **Needs the owner's OK first:**
  - visible changes (image, UI, behaviour a player notices);
  - changed defaults;
  - anything irreversible: deleting releases, tags, branches or remote data; rewriting published
    history; secrets and repository settings.
- **Render review:** everything integrated into `develop` is reviewed by the render agent.
  - Before the merge when it carries risk for the renderer or the platforms.
  - After the merge when it is documentation or a small change with tests.
  - The review reads the whole diff; findings go to the branch's owner, and a blocker stops the
    merge.
  - How a review is done, what counts as a failure and the sensitive areas that always need a
    clean-context review and the owner's look at the evidence: `REVIEW.md`.
- **Your own branches:** before integrating one, have it reviewed by a subagent with a clean
  context that gets only the diff, the task's description and `REVIEW.md` (documentation-only
  branches excepted, as `REVIEW.md` says; they integrate with CI green). An **Important**
  finding is fixed or justified (in the branch or the merge's message) before the merge.
- A review finding that comes up a second time goes into this file, a test or a lint, not into
  another comment. Bugs that reached a hand pass or a release are recorded in
  `docs/regressions.md`, with what catches them now.
- Never claim support that is not verified (README, release notes, docs). Experimental features
  are labelled as such.
- Large tasks or new directions (a new subsystem, an architecture change, work of several days):
  a plan, and the owner's approval before writing code. Anything else: a short plan in the report,
  without waiting for an OK.
- One commit per logical unit. Every change is validated with a unit test or a replay; when neither
  can validate it, say so.
- When closing a task, report in a few lines what changed, how it was verified and what is still
  open.

## Way of working

- **Work autonomously.** Ask the owner only for what this file lists as needing an OK; otherwise
  decide, do it, and report.
- **Two hypotheses:** if two hypotheses in a row fail, stop and report instead of trying a third.
- **Probes and traces** are free within a task. Ask first only when one is invasive: it touches
  persistent data, the shared SDK or anything outside the project's tree.
- **Performance:**
  - performance integrations are decided by hand runs, blind (order A B B A) whenever possible;
  - synthetic runs and replay benches only narrow down the choice;
  - every measurement records the machine's thermal state next to its numbers
    (`docs/performance-profile.md`);
  - every measurement names the commit it was built from, and that commit is reachable from
    `develop`, from a branch with an open pull request or from an `archive/` tag (a week of
    Android measurements was once built on branches nobody could find).
- **Game runs:**
  - unattended runs go through `tools/run_capped` and need no notice;
  - runs that need someone at the controller only with the owner's OK and their steps given first;
  - nobody runs the game while another agent is using the machine (Communication).

## Code

- Behaviour is generic, decided by capability or state. A draw, file or unit is never treated
  differently by hash, signature, mesh, material or other content name (`CONTRIBUTING.md`).
- No environment variables in the project. If configuration is needed, ask; it goes in the settings
  or a command-line option.
- Generated code (`generated/`) and the ReXGlue SDK are not edited by hand.
- Guest (OGRE 1.7) and host (OGRE 14) enums are never assumed equal: each is mapped explicitly
  (`src/guest_abi/`).
- Guest memory is big-endian and 32-bit. It is read through the `src/guest_abi/` helpers, never by
  casting guest pointers to host structs. Layout constants live only in `src/guest_abi/`, each with
  its evidence.
- Hooks read their guest call's arguments, never the caller's non-volatile registers or r12
  (`CONTRIBUTING.md`; the `no_preserved_guest_registers` test).
- `src/backend/`:
  - no direct GL or D3D calls, everything through OGRE's API;
  - the Xbox/D3D against host differences live in `xbox_to_gl_conventions` only, each by name;
  - no OGRE type crosses the C API (`backend_api.h`).
- Platform-specific code lives in `src/platform/` behind its common interface. No platform
  `#ifdef`s or window-system includes elsewhere.
- English for code, comments, documentation, commit and tag messages.
- Documentation is `docs/` and the code's comments. No "evidence", "audit", "package" or
  "objective" documents. No absolute paths to a developer's home in the repository.

## Safety of the owner's data

- **The owner's real saves are never touched.** Test runs use isolated profiles: copies, with
  `--user_data_root`.
- **The PC game (Steam) is not run without the full protocol,** and without a plan the owner
  approved beforehand:
  - a copy, with hashes, of the Proton prefix and of `userdata/<id>/41500`;
  - a separate prefix;
  - Steam closed and unable to relaunch the game;
  - Steam Cloud off;
  - the hashes checked again at the end.

  Why: the game relaunches itself through Steam into the real prefix and syncs saves through Steam
  by itself (`docs/mods.md`).
- **Game files never go into git:** assets, executables, extracted data, captures, saves, run logs,
  text copied from the game or its translations, even as samples (`CONTRIBUTING.md`).

## Releases and secrets

- Versions are `vZ.X.Y`, with the steps of `docs/release-pipeline.md` (5.9 branches and
  releases, 5.10 version numbers). The owner confirms the number and the lists before the tag.
- Each token is used only for what it exists for. `DEPS_REPO_TOKEN` only publishes to and prunes
  the dependency store in `YaPeL/torchlight-recomp-deps`; reads need no token.

## The ReXGlue SDK

- SDK changes are patches in `patches/` (the series in `patches/series`, BSD-3-Clause), never edits
  of a checkout.
- Swapping the shared SDK follows `docs/rexglue-upstream.md` (Updating our base, section 6):
  - delete the build trees before moving any checkout;
  - get a go from every agent that uses it;
  - no game running during the swap.

## Communication

- Agents that share a machine coordinate before using it (game runs, benchmarks, the shared SDK).
- The Windows agent does not see the other agents. Anything it needs to know goes through the
  owner.
- **Turns and waits between agents:**
  - An agent only works during a turn. "I'll go on with X" at the end of a message does not make
    it go on: when the turn ends, it stays still until someone writes to it. So never close a turn
    promising to go on: do X in that same turn, or say plainly that you stopped and why.
  - When you hand work to another agent or wait for something from it, subscribe to hear when it
    goes idle. When it does, check whether it delivered what you were waiting for. If it went idle
    without delivering it, write to it and ask what happened; do not just keep waiting.
  - When you are waiting for the owner, say so in the message's last line ("Waiting for your OK on
    X"), so who has the ball shows at a glance.
